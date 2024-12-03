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

/*! \file f1ap_cu_interface_management.c
 * \brief f1ap interface management for CU
 * \author EURECOM/NTUST
 * \date 2018
 * \version 0.1
 * \company Eurecom
 * \email: navid.nikaein@eurecom.fr, bing-kai.hong@eurecom.fr
 * \note
 * \warning
 */

#include "f1ap_common.h"
#include "f1ap_encoder.h"
#include "f1ap_itti_messaging.h"
#include "f1ap_cu_interface_management.h"
#include "f1ap_default_values.h"
#include "lib/f1ap_interface_management.h"
#include "lib/f1ap_lib_common.h"

int CU_handle_RESET_ACKNOWLEDGE(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu)
{
  // AssertFatal(1 == 0, "Not implemented yet\n");
}

int CU_send_RESET_ACKNOWLEDGE(sctp_assoc_t assoc_id, const f1ap_reset_ack_t *ack)
{
  // AssertFatal(1 == 0, "Not implemented yet\n");
}

/**
 * @brief F1AP Setup Request decoding (9.2.1.4 of 3GPP TS 38.473) and transfer to RRC
 */
int CU_handle_F1_SETUP_REQUEST(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu)
{
  // LOG_D(F1AP, "CU_handle_F1_SETUP_REQUEST\n");
  // DevAssert(pdu != NULL);
  /* F1 Setup Request == Non UE-related procedure -> stream 0 */
  if (stream != 0) {
    // LOG_W(F1AP, "[SCTP %d] Received f1 setup request on stream != 0 (%d)\n",
    //       assoc_id, stream);
    SM_Logs(LOG_INFO,_F1AP_,"Received F1 Setup Request");
  }
  f1ap_setup_req_t msg = {0};
  /* Decode */
  if (!decode_f1ap_setup_request(pdu, &msg)) {
    // LOG_E(F1AP, "cannot decode F1AP Setup Request\n");
    SM_Logs(LOG_ERROR,_F1AP_,"Error while decoding F1 Setup Request");

    free_f1ap_setup_request(&msg);
    return -1;
  }
  SM_Logs(LOG_DEBUG,_F1AP_,"F1 Setup Request decoding \033[0;32mSuccessful\033[0m ");

  SM_Logs(LOG_DEBUG, _F1AP_, "F1 Setup Request Details:");
  SM_Logs(LOG_DEBUG, _F1AP_, "|----------------------------|---------------------------|");

  /* General Parameters */
  SM_Logs(LOG_DEBUG, _F1AP_, "| Transaction ID             | %-25lu |", msg.transaction_id);
  SM_Logs(LOG_DEBUG, _F1AP_, "| gNB DU ID                  | %-25lu |", msg.gNB_DU_id);
  SM_Logs(LOG_DEBUG, _F1AP_, "| gNB DU Name                | %-25s |", msg.gNB_DU_name ? msg.gNB_DU_name : "N/A");
  SM_Logs(LOG_DEBUG, _F1AP_, "| RRC Version                | %d.%d.%-20d |", msg.rrc_ver[0], msg.rrc_ver[1], msg.rrc_ver[2]);
  SM_Logs(LOG_DEBUG, _F1AP_, "| Number of Cells Available  | %-25d |", msg.num_cells_available);
  SM_Logs(LOG_DEBUG, _F1AP_, "|----------------------------|---------------------------|");

  /* Cell-Specific Parameters */
  for (int i = 0; i < msg.num_cells_available; i++) {
      SM_Logs(LOG_DEBUG, _F1AP_, "|---------- Cell %d ----------|                           |", i + 1);
      f1ap_served_cell_info_t *cell_info = &msg.cell[i].info;

      SM_Logs(LOG_DEBUG, _F1AP_, "| NR Cell ID                 | %-25lu |", cell_info->nr_cellid);
      SM_Logs(LOG_DEBUG, _F1AP_, "| NR PCI                     | %-25d |", cell_info->nr_pci);
      SM_Logs(LOG_DEBUG, _F1AP_, "| MNC Digit Length           | %-25d |", cell_info->plmn.mnc_digit_length);
      SM_Logs(LOG_DEBUG, _F1AP_, "| PLMN MCC                   | %-25d |", cell_info->plmn.mcc);
      SM_Logs(LOG_DEBUG, _F1AP_, "| PLMN MNC                   | %-25d |", cell_info->plmn.mnc);

      if (cell_info->tac) {
          SM_Logs(LOG_DEBUG, _F1AP_, "| TAC                        | %-25u |", *cell_info->tac);
      } else {
          SM_Logs(LOG_DEBUG, _F1AP_, "| TAC                        | Not Present               |");
      }

      SM_Logs(LOG_DEBUG, _F1AP_, "| Number of Slices Supported | %-25d |", cell_info->num_ssi);
      SM_Logs(LOG_DEBUG, _F1AP_, "| Mode                       | %-25s |", cell_info->mode == F1AP_MODE_FDD ? "FDD" : "TDD");

      if (cell_info->mode == F1AP_MODE_FDD) {
          SM_Logs(LOG_DEBUG, _F1AP_, "| FDD UL Frequency ARFCN     | %-25d |", cell_info->fdd.ul_freqinfo.arfcn);
          SM_Logs(LOG_DEBUG, _F1AP_, "| FDD UL Band                | %-25d |", cell_info->fdd.ul_freqinfo.band);
          SM_Logs(LOG_DEBUG, _F1AP_, "| FDD DL Frequency ARFCN     | %-25d |", cell_info->fdd.dl_freqinfo.arfcn);
          SM_Logs(LOG_DEBUG, _F1AP_, "| FDD DL Band                | %-25d |", cell_info->fdd.dl_freqinfo.band);
          SM_Logs(LOG_DEBUG, _F1AP_, "| FDD UL Bandwidth SCS       | %-25d |", cell_info->fdd.ul_tbw.scs);
          SM_Logs(LOG_DEBUG, _F1AP_, "| FDD UL Bandwidth NRB       | %-25d |", cell_info->fdd.ul_tbw.nrb);
          SM_Logs(LOG_DEBUG, _F1AP_, "| FDD DL Bandwidth SCS       | %-25d |", cell_info->fdd.dl_tbw.scs);
          SM_Logs(LOG_DEBUG, _F1AP_, "| FDD DL Bandwidth NRB       | %-25d |", cell_info->fdd.dl_tbw.nrb);
      } else {
          SM_Logs(LOG_DEBUG, _F1AP_, "| TDD Frequency ARFCN        | %-25d |", cell_info->tdd.freqinfo.arfcn);
          SM_Logs(LOG_DEBUG, _F1AP_, "| TDD Band                   | %-25d |", cell_info->tdd.freqinfo.band);
          SM_Logs(LOG_DEBUG, _F1AP_, "| TDD Bandwidth SCS          | %-25d |", cell_info->tdd.tbw.scs);
          SM_Logs(LOG_DEBUG, _F1AP_, "| TDD Bandwidth NRB          | %-25d |", cell_info->tdd.tbw.nrb);
      }

      if (cell_info->measurement_timing_config) {
          SM_Logs(LOG_DEBUG, _F1AP_, "| Measurement Timing Config  | Present (Length: %d bytes) |", cell_info->measurement_timing_config_len);
      } else {
          SM_Logs(LOG_DEBUG, _F1AP_, "| Measurement Timing Config  | Not Present               |");
      }

      f1ap_gnb_du_system_info_t *sys_info = msg.cell[i].sys_info;
      if (sys_info) {
          SM_Logs(LOG_DEBUG, _F1AP_, "| MIB                        | Present(Length: %d bytes)  |", sys_info->mib_length);
          SM_Logs(LOG_DEBUG, _F1AP_, "| SIB1                       | Present(Length: %d bytes)|", sys_info->sib1_length);
      } else {
          SM_Logs(LOG_DEBUG, _F1AP_, "| System Info                | Not Present               |");
      }

      SM_Logs(LOG_DEBUG, _F1AP_, "|----------------------------|---------------------------|");
  }



  /* Send to RRC (ITTI) */
  SM_Logs(LOG_DEBUG,_F1AP_,"Forwarding F1 Setup Request to RRC Task.");
  MessageDef *message_p = itti_alloc_new_message(TASK_CU_F1, 0, F1AP_SETUP_REQ);
  message_p->ittiMsgHeader.originInstance = assoc_id;
  f1ap_setup_req_t *req = &F1AP_SETUP_REQ(message_p);
  *req = msg; /* "move" message into ITTI, RRC thread will free it */
  itti_send_msg_to_task(TASK_RRC_GNB, GNB_MODULE_ID_TO_INSTANCE(instance), message_p);
  return 0;
}

/**
 * @brief F1AP Setup Response encoding (9.2.1.5 of 3GPP TS 38.473) and message transfer
 */
int CU_send_F1_SETUP_RESPONSE(sctp_assoc_t assoc_id, f1ap_setup_resp_t *f1ap_setup_resp)
{
  uint8_t  *buffer=NULL;
  uint32_t len = 0;

  /* Encode F1 Setup Response */
  F1AP_F1AP_PDU_t *pdu = encode_f1ap_setup_response(f1ap_setup_resp);
  /* Free after encode */
  free_f1ap_setup_response(f1ap_setup_resp);

  /* encode */
  if (f1ap_encode_pdu(pdu, &buffer, &len) < 0) {
    // LOG_E(F1AP, "Failed to encode F1 setup response\n");
    SM_Logs(LOG_ERROR,_F1AP_,"Error while encoding F1 Setup Response");
    ASN_STRUCT_FREE(asn_DEF_F1AP_F1AP_PDU, pdu);
    return -1;
  }
  SM_Logs(LOG_DEBUG,_F1AP_,"F1AP Setup Response eecoding \033[0;32mSuccessful\033[0m");
  
  SM_Logs(LOG_DEBUG,_F1AP_,"Forwarding F1AP Setup Response to SCTP Task.");
  ASN_STRUCT_FREE(asn_DEF_F1AP_F1AP_PDU, pdu);
  f1ap_itti_send_sctp_data_req(assoc_id, buffer, len);
  SM_Logs(LOG_INFO, _F1AP_, " F1AP Setup Response Transmisson Status [from CU]: \033[0;32mSuccessfull\033[0m");

  return 0;
}

/**
 * @brief F1 Setup Failure encoding and transmission
 */
int CU_send_F1_SETUP_FAILURE(sctp_assoc_t assoc_id, const f1ap_setup_failure_t *fail)
{
  // LOG_D(F1AP, "CU_send_F1_SETUP_FAILURE\n");
  uint8_t *buffer = NULL;
  uint32_t len = 0;
  /* Encode F1 Setup Failure */
  F1AP_F1AP_PDU_t *pdu = encode_f1ap_setup_failure(fail);
  if (f1ap_encode_pdu(pdu, &buffer, &len) < 0) {
    // LOG_E(F1AP, "Failed to encode F1 setup failure\n");
    SM_Logs(LOG_ERROR,_F1AP_,"Error while encoding F1 Setup Failure");
    ASN_STRUCT_FREE(asn_DEF_F1AP_F1AP_PDU, pdu);
    return -1;
  }
  SM_Logs(LOG_DEBUG,_F1AP_,"F1AP Setup Failure encoding \033[0;32mSuccessful\033[0m");
  
  ASN_STRUCT_FREE(asn_DEF_F1AP_F1AP_PDU, pdu);
  SM_Logs(LOG_DEBUG,_F1AP_,"Forwarding F1 Setup Failure to SCTP Task.");
  f1ap_itti_send_sctp_data_req(assoc_id, buffer, len);
  SM_Logs(LOG_INFO, _F1AP_, "F1AP Setup Failure Transmisson Status [from CU]: \033[0;32mSuccessfull\033[0m");
  return 0;
}

/**
 * @brief Decode and send F1 gNB-DU Configuration Update message to RRC
 */
int CU_handle_gNB_DU_CONFIGURATION_UPDATE(instance_t instance, sctp_assoc_t assoc_id, uint32_t stream, F1AP_F1AP_PDU_t *pdu)
{
  // LOG_D(F1AP, "[SCTP %d] CU_handle_gNB_DU_CONFIGURATION_UPDATE\n", assoc_id);
  // DevAssert(pdu != NULL);
  /* gNB DU Configuration Update == Non UE-related procedure -> stream 0 */
  if (stream != 0) {
    // LOG_W(F1AP, "[SCTP %d] Received f1 setup request on stream != 0 (%d)\n", assoc_id, stream);
    SM_Logs(LOG_INFO,_F1AP_,"Received Du Configuration Update");
  }
  /* Decode */
  f1ap_gnb_du_configuration_update_t msg = {0};
  if (!decode_f1ap_du_configuration_update(pdu, &msg)) {
    SM_Logs(LOG_ERROR,_F1AP_,"Error while decoding F1AP DU Configuration Update");
    // LOG_E(F1AP, "cannot decode F1AP gNB-DU Configuration Update\n");
    free_f1ap_du_configuration_update(&msg);
    return -1;
  }
  SM_Logs(LOG_DEBUG,_F1AP_,"F1AP DU Configuration Update decoding \033[0;32mSuccessful\033[0m ");
  
  SM_Logs(LOG_DEBUG,_F1AP_,"Forwarding F1AP DU Configuration Update to RRC Task.");

  /* Send to RRC */
  MessageDef *message_p = itti_alloc_new_message(TASK_CU_F1, 0, F1AP_GNB_DU_CONFIGURATION_UPDATE);
  message_p->ittiMsgHeader.originInstance = assoc_id;
  f1ap_gnb_du_configuration_update_t *req = &F1AP_GNB_DU_CONFIGURATION_UPDATE(message_p); // RRC thread will free it
  *req = msg; // copy F1 message to ITTI
  free_f1ap_du_configuration_update(&msg);
  // LOG_D(F1AP, "Sending F1AP_GNB_DU_CONFIGURATION_UPDATE ITTI message \n");
  itti_send_msg_to_task(TASK_RRC_GNB, GNB_MODULE_ID_TO_INSTANCE(instance), message_p);
  return 0;
}

int CU_send_gNB_DU_CONFIGURATION_UPDATE_ACKNOWLEDGE(sctp_assoc_t assoc_id, f1ap_gnb_du_configuration_update_acknowledge_t *msg)
{
  uint8_t *buffer;
  uint32_t len;
  /* Encode F1 gNB-CU Configuration Update message */
  F1AP_F1AP_PDU_t *pdu = encode_f1ap_du_configuration_update_acknowledge(msg);
  /* Encode F1AP PDU */
  if (f1ap_encode_pdu(pdu, &buffer, &len) < 0) {
    ASN_STRUCT_FREE(asn_DEF_F1AP_F1AP_PDU, pdu);
    SM_Logs(LOG_ERROR,_F1AP_,"Error while encoding F1AP DU Configuration Update Ack");
    // LOG_E(F1AP, "Failed to encode F1 gNB-DU Configuration Update Acknowledge\n");
    return -1;
  }
  
  SM_Logs(LOG_DEBUG,_F1AP_,"F1AP DU Configuration Update Ack encoding \033[0;32mSuccessful\033[0m");
  

  // LOG_DUMPMSG(F1AP, LOG_DUMP_CHAR, buffer, len, "F1AP gNB-DU CONFIGURATION UPDATE : ");
  ASN_STRUCT_FREE(asn_DEF_F1AP_F1AP_PDU, pdu);
  SM_Logs(LOG_DEBUG,_F1AP_,"Forwarding F1AP DU Configuration Update Ack to SCTP Task.");
  f1ap_itti_send_sctp_data_req(assoc_id, buffer, len);
  SM_Logs(LOG_INFO, _F1AP_, "F1AP DU Configuration Update Ack Transmisson Status [from CU]: \033[0;32mSuccessfull\033[0m");

  return 0;
}

/**
 * @brief F1 gNB-CU Configuration Update message encoding and transfer
 */
int CU_send_gNB_CU_CONFIGURATION_UPDATE(sctp_assoc_t assoc_id, f1ap_gnb_cu_configuration_update_t *msg)
{
  SM_Logs(LOG_INFO, _F1AP_, "Initiating the transmission of GNB CU Configuration Update to the Distributed Unit (DU).");
  /* complete F1AP message */
  msg->transaction_id = F1AP_get_next_transaction_identifier(0, 0); // note: has to be done in the caller
  uint8_t  *buffer;
  uint32_t  len;
  /* Encode F1 gNB-CU Configuration Update message */
  F1AP_F1AP_PDU_t *pdu = encode_f1ap_cu_configuration_update(msg);
  /* Free after encoding */
  free_f1ap_cu_configuration_update(msg);
  /* Encode F1AP PDU */
  if (f1ap_encode_pdu(pdu, &buffer, &len) < 0) {
    ASN_STRUCT_FREE(asn_DEF_F1AP_F1AP_PDU, pdu);
    SM_Logs(LOG_ERROR,_F1AP_,"Error while encoding F1AP CU Configuration Update");
    // LOG_E(F1AP, "Failed to encode F1 gNB-CU Configuration Update\n");
    return -1;
  }
  
  SM_Logs(LOG_DEBUG,_F1AP_,"F1AP CU Configuration Update encoding \033[0;32mSuccessful\033[0m ");
  


  // LOG_DUMPMSG(F1AP, LOG_DUMP_CHAR, buffer, len, "F1AP gNB-CU CONFIGURATION UPDATE : ");
  ASN_STRUCT_FREE(asn_DEF_F1AP_F1AP_PDU, pdu);
  SM_Logs(LOG_DEBUG,_F1AP_,"Forwarding F1AP CU Configuration Update to SCTP Task.");
  f1ap_itti_send_sctp_data_req(assoc_id, buffer, len);
  SM_Logs(LOG_INFO, _F1AP_, "F1AP CU Configuration Update Transmisson Status [from CU]: \033[0;32mSuccessfull\033[0m");

  return 0;
}

/**
 * @brief gNB CU Configuration Update Acknowledge decoding (9.2.1.11 of 3GPP TS 38.473)
 *        and handling by CU
 */
int CU_handle_gNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(instance_t instance,
                                                      sctp_assoc_t assoc_id,
                                                      uint32_t stream,
                                                      F1AP_F1AP_PDU_t *pdu)
{
  SM_Logs(LOG_INFO, _F1AP_, "Processing the GNB CU Configuration Update Acknowledge received from the Distributed Unit (DU).");
  // DevAssert(pdu != NULL);
  // if (stream != 0)
    // LOG_W(F1AP, "[SCTP %d] Received gNB CU Configuration Update Acknowledge on stream != 0 (%d)\n", assoc_id, stream);
  // Decode
  f1ap_gnb_cu_configuration_update_acknowledge_t msg = {0};
  if (!decode_f1ap_cu_configuration_update_acknowledge(pdu, &msg)) {
    // LOG_E(F1AP, "Failed to decode gNB CU Configuration Update Acknowledge\n");
    SM_Logs(LOG_ERROR,_F1AP_,"Error while decoding F1AP CU Configuration Update Ack");
    return -1;
  }

    SM_Logs(LOG_DEBUG,_F1AP_,"F1AP CU Configuration Update Ack decoding \033[0;32mSuccessful\033[0m ");
  
  SM_Logs(LOG_DEBUG,_F1AP_,"Forwarding F1AP CU Configuration Update Ack to RRC Task.");


  // Allocate ITTI message and send to RRC
  MessageDef *message_p = itti_alloc_new_message(TASK_CU_F1, 0, F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE);
  message_p->ittiMsgHeader.originInstance = assoc_id;
  f1ap_gnb_cu_configuration_update_acknowledge_t *ack = &F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(message_p);
  *ack = msg; // copy decoded message to ITTI, the RRC thread will free it
  itti_send_msg_to_task(TASK_RRC_GNB, GNB_MODULE_ID_TO_INSTANCE(instance), message_p);
  return 0;
}
