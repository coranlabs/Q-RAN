/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file openair2/F1AP/f1ap_cu_task.c
 * \brief data structures for F1 interface modules
 * \author EURECOM/NTUST
 * \date 2018
 * \version 0.1
 * \company Eurecom
 * \email: navid.nikaein@eurecom.fr, raymond.knopp@eurecom.fr, bing-kai.hong@eurecom.fr
 * \note
 * \warning
 */

#include "f1ap_common.h"
#include "f1ap_cu_interface_management.h"
#include "f1ap_cu_rrc_message_transfer.h"
#include "f1ap_cu_ue_context_management.h"
#include "lib/f1ap_rrc_message_transfer.h"
#include "f1ap_cu_paging.h"
#include "f1ap_cu_task.h"
#include <openair3/ocp-gtpu/gtp_itf.h>

// Fixme: Uniq dirty DU instance, by global var, datamodel need better management
instance_t CUuniqInstance = 0;

static instance_t cu_task_create_gtpu_instance(eth_params_t *IPaddrs)
{
  openAddr_t tmp = {0};
  strncpy(tmp.originHost, IPaddrs->my_addr, sizeof(tmp.originHost) - 1);
  sprintf(tmp.originService, "%d", IPaddrs->my_portd);
  sprintf(tmp.destinationService, "%d", IPaddrs->remote_portd);
  return gtpv1Init(tmp);
}

static void cu_task_handle_sctp_association_ind(instance_t instance,
                                                sctp_new_association_ind_t *sctp_new_association_ind,
                                                eth_params_t *IPaddrs)
{
  // save the assoc id
  f1ap_cudu_inst_t *f1ap_cu_data = getCxt(instance);
  // we don't need the assoc_id, subsequent messages (the first being F1 Setup
  // Request), will deliver the assoc_id
  f1ap_cu_data->sctp_in_streams = sctp_new_association_ind->in_streams;
  f1ap_cu_data->sctp_out_streams = sctp_new_association_ind->out_streams;
}

static void cu_task_send_ssl_handshake_req(instance_t instance, sctp_assoc_t assoc_id)
{
  MessageDef *message_p = NULL;
  sctp_ssl_do_handshake_t *sctp_ssl_do_handshake_p = NULL;

  // f1ap_cudu_inst_t *f1ap_du_data = getCxt(instance);

  message_p = itti_alloc_new_message(TASK_CU_F1, 0, SCTP_SSL_DO_HANDSHAKE);
  sctp_ssl_do_handshake_p = &message_p->ittiMsg.sctp_ssl_do_handshake;
  sctp_ssl_do_handshake_p->assoc_id = assoc_id;
  itti_send_msg_to_task(TASK_SCTP, instance, message_p);
}

static void cu_task_handle_sctp_association_resp(instance_t instance, sctp_new_association_resp_t *sctp_new_association_resp)
{
  // DevAssert(sctp_new_association_resp != NULL);

  enum sctp_state_e state = sctp_new_association_resp->sctp_state;
  if (state != SCTP_STATE_ESTABLISHED) {
    f1ap_cudu_inst_t *f1ap_cu_data = getCxt(instance);
    // AssertFatal(f1ap_cu_data != NULL, "illegal state: SCTP shutdown for non-existing F1AP endpoint\n");
    // LOG_I(F1AP, "Received SCTP state %d for assoc_id %d, removing endpoint\n", state, sctp_new_association_resp->assoc_id);
    SM_Logs(LOG_INFO, _F1AP_, "Handling sctp association response");
    /* inform RRC that the DU is gone */
    MessageDef *message_p = itti_alloc_new_message(TASK_CU_F1, 0, F1AP_LOST_CONNECTION);
    message_p->ittiMsgHeader.originInstance = sctp_new_association_resp->assoc_id;
    itti_send_msg_to_task(TASK_RRC_GNB, instance, message_p);
    return;
  }
  cu_task_send_ssl_handshake_req(instance, sctp_new_association_resp->assoc_id);
}

static void cu_task_handle_sctp_data_ind(instance_t instance, sctp_data_ind_t *sctp_data_ind)
{
  int result;
  // DevAssert(sctp_data_ind != NULL);
  f1ap_handle_message(instance,
                      sctp_data_ind->assoc_id,
                      sctp_data_ind->stream,
                      sctp_data_ind->buffer,
                      sctp_data_ind->buffer_length);
  result = itti_free(TASK_UNKNOWN, sctp_data_ind->buffer);
  // AssertFatal (result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
}

static void cu_task_send_sctp_init_req(instance_t instance, char *my_addr)
{
  // 1. get the itti msg, and retrive the nb_id from the message
  // 2. use RC.rrc[nb_id] to fill the sctp_init_t with the ip, port
  // 3. creat an itti message to init
  size_t addr_len = strlen(my_addr) + 1;
  // LOG_I(F1AP, "F1AP_CU_SCTP_REQ(create socket) for %s len %ld\n", my_addr, addr_len);
  SM_Logs(LOG_INFO, _F1AP_, "Sending sctp init request");
  MessageDef *message_p = itti_alloc_new_message_sized(TASK_CU_F1, 0, SCTP_INIT_MSG, sizeof(sctp_init_t) + addr_len);
  sctp_init_t *init = &SCTP_INIT_MSG(message_p);
  init->port = F1AP_PORT_NUMBER;
  init->ppid = F1AP_SCTP_PPID;
  char *addr_buf = (char *)(init + 1); // address after ITTI message end, allocated above
  init->bind_address = addr_buf;
  memcpy(addr_buf, my_addr, addr_len);
  itti_send_msg_to_task(TASK_SCTP, instance, message_p);
  SM_Logs(LOG_INFO, _F1AP_, "Sent sctp init request");

}

void *F1AP_CU_task(void *arg)
{
  MessageDef *received_msg = NULL;
  int result;
  // LOG_I(F1AP, "Starting F1AP at CU\n");
  SM_Logs(LOG_INFO, _F1AP_, "Starting the F1AP CU task.");
  // no RLC in CU, initialize mem pool for PDCP
  // itti_mark_task_ready(TASK_CU_F1);
  eth_params_t *IPaddrs;

  // Hardcoded instance id!
  IPaddrs = &RC.nrrrc[0]->eth_params_s;
  SM_Logs(LOG_INFO, _F1AP_, "Started TASK_CU_F1 1\n");

  const int instance = 0;
  createF1inst(instance, NULL, NULL);
  cu_task_send_sctp_init_req(instance, IPaddrs->my_addr);
  SM_Logs(LOG_INFO, _F1AP_, "Sent SCTP initialization request.");
  if (RC.nrrrc[instance]->node_type != ngran_gNB_CUCP) {
    getCxt(instance)->gtpInst = cu_task_create_gtpu_instance(IPaddrs);
    // AssertFatal(getCxt(instance)->gtpInst > 0, "Failed to create CU F1-U UDP listener");
  } else {
    // LOG_I(F1AP, "In F1AP connection, don't start GTP-U, as we have also E1AP\n");
  }
  // Fixme: fully inconsistent instances management
  // dirty global var is a bad fix
  CUuniqInstance = getCxt(instance)->gtpInst;

  while (1) {
    itti_receive_msg(TASK_CU_F1, &received_msg);
    sctp_assoc_t assoc_id = ITTI_MSG_ORIGIN_INSTANCE(received_msg);
    // LOG_D(F1AP, "CU Task Received %s for instance %ld: sending SCTP message via assoc_id %d\n",
    // ITTI_MSG_NAME(received_msg), ITTI_MSG_DESTINATION_INSTANCE(received_msg), assoc_id);
    SM_Logs(LOG_DEBUG, _F1AP_, "Message received in F1AP CU task: ID=%02x.", ITTI_MSG_ID(received_msg) + 40);
    switch (ITTI_MSG_ID(received_msg)) {
      case SCTP_NEW_ASSOCIATION_IND:
        cu_task_handle_sctp_association_ind(ITTI_MSG_ORIGIN_INSTANCE(received_msg),
                                            &received_msg->ittiMsg.sctp_new_association_ind,
                                            IPaddrs);
        break;

      case SCTP_NEW_ASSOCIATION_RESP:
        cu_task_handle_sctp_association_resp(ITTI_MSG_DESTINATION_INSTANCE(received_msg),
                                             &received_msg->ittiMsg.sctp_new_association_resp);
        break;

      case SCTP_DATA_IND:
        SM_Logs(LOG_DEBUG, _F1AP_, "SCTP data received from SCTP CU task");
        cu_task_handle_sctp_data_ind(ITTI_MSG_DESTINATION_INSTANCE(received_msg), &received_msg->ittiMsg.sctp_data_ind);
        break;

      case F1AP_RESET_ACK:
        CU_send_RESET_ACKNOWLEDGE(assoc_id, &F1AP_RESET_ACK(received_msg));
        break;

      case F1AP_SETUP_RESP: // from rrc
        SM_Logs(LOG_INFO, _F1AP_, "F1AP Setup response received from RRC. Sending response to peer.");
        CU_send_F1_SETUP_RESPONSE(assoc_id, &F1AP_SETUP_RESP(received_msg));
        break;

      case F1AP_SETUP_FAILURE:
        SM_Logs(LOG_ERROR, _F1AP_, "F1AP Setup failure received. Notifying peer.");
        CU_send_F1_SETUP_FAILURE(assoc_id, &F1AP_SETUP_FAILURE(received_msg));
        break;

      case F1AP_GNB_CU_CONFIGURATION_UPDATE: // from rrc
        SM_Logs(LOG_DEBUG, _F1AP_, "F1AP GNB CU Configuration Update Message received. Sending to peer.");
        CU_send_gNB_CU_CONFIGURATION_UPDATE(assoc_id, &F1AP_GNB_CU_CONFIGURATION_UPDATE(received_msg));
        break;

      case F1AP_GNB_DU_CONFIGURATION_UPDATE_ACKNOWLEDGE:
        SM_Logs(LOG_DEBUG, _F1AP_, "F1AP GNB DU Configuration Update Message received. Sending to peer.");
        CU_send_gNB_DU_CONFIGURATION_UPDATE_ACKNOWLEDGE(assoc_id, &F1AP_GNB_DU_CONFIGURATION_UPDATE_ACKNOWLEDGE(received_msg));
        break;
      case F1AP_DL_RRC_MESSAGE: // from rrc
        SM_Logs(LOG_DEBUG, _F1AP_, "F1AP Downlink RRC message received. Sending to peer.");
        CU_send_DL_RRC_MESSAGE_TRANSFER(assoc_id, &F1AP_DL_RRC_MESSAGE(received_msg));
        free_dl_rrc_message_transfer(&F1AP_DL_RRC_MESSAGE(received_msg));
        break;

      case F1AP_UE_CONTEXT_SETUP_REQ: // from rrc
        SM_Logs(LOG_INFO, _F1AP_, "F1AP UE context setup request received. Forwarding to peer.");
        CU_send_UE_CONTEXT_SETUP_REQUEST(assoc_id, &F1AP_UE_CONTEXT_SETUP_REQ(received_msg));
        break;

      case F1AP_UE_CONTEXT_MODIFICATION_REQ:
        SM_Logs(LOG_INFO, _F1AP_, "F1AP UE context modification request received. Sending to peer.");
        CU_send_UE_CONTEXT_MODIFICATION_REQUEST(assoc_id, &F1AP_UE_CONTEXT_MODIFICATION_REQ(received_msg));
        break;

      case F1AP_UE_CONTEXT_RELEASE_CMD: // from rrc
        SM_Logs(LOG_INFO, _F1AP_, "F1AP UE context release command received. Forwarding to peer.");
        CU_send_UE_CONTEXT_RELEASE_COMMAND(assoc_id, &F1AP_UE_CONTEXT_RELEASE_CMD(received_msg));
        break;

      case F1AP_PAGING_IND:
        SM_Logs(LOG_INFO, _F1AP_, "F1AP Paging indication received. Sending to peer.");
        CU_send_Paging(assoc_id, &F1AP_PAGING_IND(received_msg));
        break;

      case F1AP_UE_CONTEXT_MODIFICATION_CONFIRM:
        CU_send_UE_CONTEXT_MODIFICATION_CONFIRM(assoc_id, &F1AP_UE_CONTEXT_MODIFICATION_CONFIRM(received_msg));
        break;

      case F1AP_UE_CONTEXT_MODIFICATION_REFUSE:
        CU_send_UE_CONTEXT_MODIFICATION_REFUSE(assoc_id, &F1AP_UE_CONTEXT_MODIFICATION_REFUSE(received_msg));
        break;

      case TERMINATE_MESSAGE:
        SM_Logs(LOG_WARN, _F1AP_, "Terminate message received. Exiting F1AP CU task.");
        // LOG_W(F1AP, " *** Exiting F1AP thread\n");
        itti_exit_task();
        break;

      default:
        SM_Logs(LOG_WARN, _F1AP_, "Unhandled message received: ID=%d.\n", ITTI_MSG_ID(received_msg));
        // LOG_E(F1AP, "CU Received unhandled message: %d:%s\n",
        //       ITTI_MSG_ID(received_msg), ITTI_MSG_NAME(received_msg));
        break;
    } // switch

    result = itti_free(ITTI_MSG_ORIGIN_ID(received_msg), received_msg);
    if (result != EXIT_SUCCESS) {
      SM_Logs(LOG_ERROR, _F1AP_, "Failed to free memory for received message.");
    }
    // AssertFatal (result == EXIT_SUCCESS, "Failed to free memory (%d)!\n", result);
    received_msg = NULL;
  } // while

  SM_Logs(LOG_INFO, _F1AP_, "Destroying F1AP CU task instance.");
  destroyF1inst(instance);

  return NULL;
}
