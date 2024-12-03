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

#include "mac_rrc_dl_handler.h"

#include "mac_proto.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "openair2/F1AP/f1ap_common.h"
#include "openair2/LAYER2/nr_rlc/nr_rlc_oai_api.h"
#include "F1AP_CauseRadioNetwork.h"
#include "NR_HandoverPreparationInformation.h"
#include "openair3/ocp-gtpu/gtp_itf.h"
#include "openair2/LAYER2/nr_pdcp/nr_pdcp_oai_api.h"
#include "lib/f1ap_interface_management.h"

#include "executables/softmodem-common.h"

#include "uper_decoder.h"
#include "uper_encoder.h"

// Standarized 5QI values and Default Priority levels as mentioned in 3GPP TS 23.501 Table 5.7.4-1
const uint64_t qos_fiveqi[26] = {1, 2, 3, 4, 65, 66, 67, 71, 72, 73, 74, 76, 5, 6, 7, 8, 9, 69, 70, 79, 80, 82, 83, 84, 85, 86};
const uint64_t qos_priority[26] = {20, 40, 30, 50, 7, 20, 15, 56, 56, 56, 56, 56, 10,
                                   60, 70, 80, 90, 5, 55, 65, 68, 19, 22, 24, 21, 18};

static instance_t get_f1_gtp_instance(void)
{
  const f1ap_cudu_inst_t *inst = getCxt(0);
  if (!inst)
    return -1; // means no F1
  return inst->gtpInst;
}

static int drb_gtpu_create(instance_t instance,
                           uint32_t ue_id,
                           int incoming_id,
                           int outgoing_id,
                           int qfi,
                           in_addr_t tlAddress, // only IPv4 now
                           teid_t outgoing_teid,
                           gtpCallback callBack,
                           gtpCallbackSDAP callBackSDAP,
                           gtpv1u_gnb_create_tunnel_resp_t *create_tunnel_resp)
{
  gtpv1u_gnb_create_tunnel_req_t create_tunnel_req = {0};
  create_tunnel_req.incoming_rb_id[0] = incoming_id;
  create_tunnel_req.pdusession_id[0] = outgoing_id;
  memcpy(&create_tunnel_req.dst_addr[0].buffer, &tlAddress, sizeof(uint8_t) * 4);
  create_tunnel_req.dst_addr[0].length = 32;
  create_tunnel_req.outgoing_teid[0] = outgoing_teid;
  create_tunnel_req.outgoing_qfi[0] = qfi;
  create_tunnel_req.num_tunnels = 1;
  create_tunnel_req.ue_id = ue_id;

  // we use gtpv1u_create_ngu_tunnel because it returns the interface
  // address and port of the interface; apart from that, we also might call
  // newGtpuCreateTunnel() directly
  return gtpv1u_create_ngu_tunnel(instance, &create_tunnel_req, create_tunnel_resp, callBack, callBackSDAP);
}

bool DURecvCb(protocol_ctxt_t *ctxt_pP,
              const srb_flag_t srb_flagP,
              const rb_id_t rb_idP,
              const mui_t muiP,
              const confirm_t confirmP,
              const sdu_size_t sdu_buffer_sizeP,
              unsigned char *const sdu_buffer_pP,
              const pdcp_transmission_mode_t modeP,
              const uint32_t *sourceL2Id,
              const uint32_t *destinationL2Id)
{
  // The buffer comes from the stack in gtp-u thread, we have a make a separate buffer to enqueue in a inter-thread message queue
  uint8_t *sdu = malloc16(sdu_buffer_sizeP);
  memcpy(sdu, sdu_buffer_pP, sdu_buffer_sizeP);
  du_rlc_data_req(ctxt_pP, srb_flagP, false, rb_idP, muiP, confirmP, sdu_buffer_sizeP, sdu);
  return true;
}

static bool check_plmn_identity(const f1ap_plmn_t *check_plmn, const f1ap_plmn_t *plmn)
{
  return plmn->mcc == check_plmn->mcc && plmn->mnc_digit_length == check_plmn->mnc_digit_length && plmn->mnc == check_plmn->mnc;
}

static void du_clear_all_ue_states()
{
  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);

  NR_UE_info_t *UE = *mac->UE_info.list;

  instance_t f1inst = get_f1_gtp_instance();

  while (UE != NULL) {
    int rnti = UE->rnti;
    nr_mac_release_ue(mac, rnti);
    // free all F1 contexts
    if (du_exists_f1_ue_data(rnti))
      du_remove_f1_ue_data(rnti);
    newGtpuDeleteAllTunnels(f1inst, rnti);
    UE = *mac->UE_info.list;
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);
}

void f1_reset_cu_initiated(const f1ap_reset_t *reset)
{
  // LOG_I(MAC, "F1 Reset initiated by CU\n");
  SM_Logs(LOG_INFO, _MAC_, "F1 Reset operation has been initiated by the Central Unit (CU). Evaluating reset parameters.");

  f1ap_reset_ack_t ack = {0};
  if(reset->reset_type == F1AP_RESET_ALL) {
    SM_Logs(LOG_WARN, _MAC_, "Resetting all UE contexts and associated states as per the CU's directive.");
    du_clear_all_ue_states();
    ack = (f1ap_reset_ack_t) {
      .transaction_id = reset->transaction_id
    };
  } else {
    // reset->reset_type == F1AP_RESET_PART_OF_F1_INTERFACE
    // AssertFatal(1==0, "Not implemented yet\n");
    SM_Logs(LOG_ERROR, _MAC_, "Partial reset of the F1 interface is currently unsupported. Operation aborted.");
    SM_Log_Assert(_MAC_, false, "Partial F1 reset functionality is not implemented.");
  }

  gNB_MAC_INST *mac = RC.nrmac[0];
  mac->mac_rrc.f1_reset_acknowledge(&ack);
}

void f1_reset_acknowledge_du_initiated(const f1ap_reset_ack_t *ack)
{
  (void) ack;
  // AssertFatal(false, "%s() not implemented yet\n", __func__);
}

void f1_setup_response(const f1ap_setup_resp_t *resp)
{
  // LOG_I(MAC, "received F1 Setup Response from CU %s\n", resp->gNB_CU_name);
  SM_Logs(LOG_INFO,_MAC_, "F1 Setup Response successfully received from Central Unit (CU). Proceeding with configuration of network entities and resource allocation.");

  // LOG_I(MAC, "CU uses RRC version %d.%d.%d\n", resp->rrc_ver[0], resp->rrc_ver[1], resp->rrc_ver[2]);
  SM_Logs(LOG_INFO,_MAC_, "Initiating RRC protocol sequence with Central Unit. Utilizing latest RRC Version for session establishment and control signaling");


  if (resp->num_cells_to_activate == 0) {
    // LOG_W(NR_MAC, "no cell to activate: cell remains blocked\n");
    SM_Logs(LOG_WARN, _MAC_, "No cells to activate. The cell remains blocked.");
    return;
  }

  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  const f1ap_setup_req_t *setup_req = mac->f1_config.setup_req;
  const f1ap_served_cell_info_t *du_cell = &setup_req->cell[0].info;

  // AssertFatal(resp->num_cells_to_activate == 1, "can only handle one cell, but %d activated\n", resp->num_cells_to_activate);

  SM_Log_Assert(_MAC_, resp->num_cells_to_activate == 1, "Only one cell can be handled. Attempted to activate %d cells.",resp->num_cells_to_activate);

  const served_cells_to_activate_t *cu_cell = &resp->cells_to_activate[0];

  // AssertFatal(du_cell->nr_cellid  == cu_cell->nr_cellid, "CellID mismatch: DU %ld vs CU %ld\n", du_cell->nr_cellid, cu_cell->nr_cellid);
  // AssertFatal(check_plmn_identity(&du_cell->plmn, &cu_cell->plmn), "PLMN mismatch\n");
  // AssertFatal(du_cell->nr_pci == cu_cell->nrpci, "PCI mismatch: DU %d vs CU %d\n", du_cell->nr_pci, cu_cell->nrpci);

  // AssertFatal(cu_cell->num_SI == 0, "handling of CU-provided SIBs: not implemented\n");

  SM_Log_Assert(_MAC_, du_cell->nr_cellid == cu_cell->nr_cellid, "Cell ID mismatch. DU Cell ID: %ld, CU Cell ID: %ld.",du_cell->nr_cellid, cu_cell->nr_cellid);

  SM_Log_Assert(_MAC_, check_plmn_identity(&du_cell->plmn, &cu_cell->plmn), "PLMN identity mismatch between DU and CU.");

  SM_Log_Assert(_MAC_, du_cell->nr_pci == cu_cell->nrpci, "PCI mismatch. DU PCI: %d, CU PCI: %d.", du_cell->nr_pci, cu_cell->nrpci);

  SM_Log_Assert(_MAC_, cu_cell->num_SI == 0, "Handling of CU-provided SIBs is not implemented.");

  
  mac->f1_config.setup_resp = malloc(sizeof(*mac->f1_config.setup_resp));
  // AssertFatal(mac->f1_config.setup_resp != NULL, "out of memory\n");
  SM_Log_Assert(_MAC_, mac->f1_config.setup_resp != NULL, "Memory allocation failed for F1 Setup Response.");
  // Copy F1AP message
  *mac->f1_config.setup_resp = cp_f1ap_setup_response(resp);
  NR_SCHED_UNLOCK(&mac->sched_lock);

  // NOTE: Before accepting any UEs, we should initialize the UE states.
  // This is to handle cases when DU loses the existing SCTP connection,
  // and reestablishes a new connection to either a new CU or the same CU.
  // This triggers a new F1 Setup Request from DU to CU as per the specs.
  // Reinitializing the UE states is necessary to avoid any inconsistent states
  // between DU and CU.
  // NOTE2: do not reset in phy_test, because there is a pre-configured UE in
  // this case. Once NSA/phy-test use F1, this might be lifted, because
  // creation of a UE will be requested from higher layers.

  // TS38.473 [Sec 8.2.3.1]: "This procedure also re-initialises the F1AP UE-related
  // contexts (if any) and erases all related signalling connections
  // in the two nodes like a Reset procedure would do."

  SM_Logs(LOG_INFO, _MAC_, "F1 Setup Response successfully processed. Network configuration is ready.");
  if (!get_softmodem_params()->phy_test) {
    // LOG_I(MAC, "Clearing the DU's UE states before, if any.\n");
    SM_Logs(LOG_DEBUG, _MAC_, "Clearing all UE states in the DU to ensure consistency with the CU.");
    du_clear_all_ue_states();
  }
}

void f1_setup_failure(const f1ap_setup_failure_t *failure)
{
  // LOG_E(MAC, "the CU reported F1AP Setup Failure, is there a configuration mismatch?\n");
  SM_Logs(LOG_CRTERR, _MAC_, "The Central Unit (CU) reported an F1AP Setup Failure. This may indicate a configuration mismatch.");
  exit(1);
}

void gnb_du_configuration_update_acknowledge(const f1ap_gnb_du_configuration_update_acknowledge_t *ack)
{
  (void)ack;
  // LOG_I(MAC, "received gNB-DU configuration update acknowledge\n");
  SM_Logs(LOG_INFO, _MAC_, "Received gNB-DU configuration update acknowledgment. Configuration update was successful.");
}

static NR_RLC_BearerConfig_t *get_bearerconfig_from_srb(const f1ap_srb_to_be_setup_t *srb)
{
  long priority = srb->srb_id == 2 ? 3 : 1; // see 38.331 sec 9.2.1
  e_NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration bucket =
      NR_LogicalChannelConfig__ul_SpecificParameters__bucketSizeDuration_ms5;
  return get_SRB_RLC_BearerConfig(get_lcid_from_srbid(srb->srb_id), priority, bucket);
}

static int handle_ue_context_srbs_setup(NR_UE_info_t *UE,
                                        int srbs_len,
                                        const f1ap_srb_to_be_setup_t *req_srbs,
                                        f1ap_srb_to_be_setup_t **resp_srbs,
                                        NR_CellGroupConfig_t *cellGroupConfig)
{
  // DevAssert(req_srbs != NULL && resp_srbs != NULL && cellGroupConfig != NULL);
  SM_Log_Assert(_MAC_, req_srbs != NULL && resp_srbs != NULL && cellGroupConfig != NULL, "Invalid input parameters: request SRBs, response SRBs, or Cell Group Config is NULL.");

  *resp_srbs = calloc(srbs_len, sizeof(**resp_srbs));
  // AssertFatal(*resp_srbs != NULL, "out of memory\n");
  SM_Log_Assert(_MAC_, *resp_srbs != NULL, "Memory allocation failed for SRB response structure.");

  SM_Logs(LOG_INFO, _MAC_, "Starting SRB setup for UE with RNTI %04x. Number of SRBs: %d.", UE->rnti, srbs_len);

  for (int i = 0; i < srbs_len; i++) {
    const f1ap_srb_to_be_setup_t *srb = &req_srbs[i];
    NR_RLC_BearerConfig_t *rlc_BearerConfig = get_bearerconfig_from_srb(srb);
    SM_Logs(LOG_DEBUG, _RLC_, "Adding SRB ID %d to RLC for UE with RNTI %04x.", srb->srb_id, UE->rnti);
    nr_rlc_add_srb(UE->rnti, srb->srb_id, rlc_BearerConfig);

    int priority = rlc_BearerConfig->mac_LogicalChannelConfig->ul_SpecificParameters->priority;
    nr_lc_config_t c = {.lcid = rlc_BearerConfig->logicalChannelIdentity, .priority = priority};
    nr_mac_add_lcid(&UE->UE_sched_ctrl, &c);

    (*resp_srbs)[i].srb_id = srb->srb_id;
    (*resp_srbs)[i].lcid = c.lcid;

    if (rlc_BearerConfig->logicalChannelIdentity == 1) {
      // CU asks to add SRB1: when creating a cellGroupConfig, we always add it
      // (see get_initial_cellGroupConfig())
      const struct NR_CellGroupConfig__rlc_BearerToAddModList *addmod = cellGroupConfig->rlc_BearerToAddModList;
      // DevAssert(addmod->list.count >= 1 && addmod->list.array[0]->logicalChannelIdentity == 1);
      SM_Log_Assert(_MAC_, addmod->list.count >= 1 && addmod->list.array[0]->logicalChannelIdentity == 1,"Initial configuration mismatch: SRB1 is not correctly configured in CellGroupConfig.");
      ASN_STRUCT_FREE(asn_DEF_NR_RLC_BearerConfig, rlc_BearerConfig);
    } else {
      int ret = ASN_SEQUENCE_ADD(&cellGroupConfig->rlc_BearerToAddModList->list, rlc_BearerConfig);
      // DevAssert(ret == 0);
      SM_Log_Assert(_MAC_, ret == 0, "Failed to add RLC Bearer Configuration to CellGroupConfig.");
      SM_Logs(LOG_DEBUG, _MAC_, "Added RLC Bearer Configuration for logical channel ID %d to CellGroupConfig.",rlc_BearerConfig->logicalChannelIdentity);
    }
  }
  return srbs_len;
}

static NR_RLC_BearerConfig_t *get_bearerconfig_from_drb(const f1ap_drb_to_be_setup_t *drb)
{
  const NR_RLC_Config_PR rlc_conf = drb->rlc_mode == F1AP_RLC_MODE_AM ? NR_RLC_Config_PR_am : NR_RLC_Config_PR_um_Bi_Directional;
  long priority = 13; // hardcoded for the moment
  return get_DRB_RLC_BearerConfig(get_lcid_from_drbid(drb->drb_id), drb->drb_id, rlc_conf, priority);
}

static int get_non_dynamic_priority(int fiveqi)
{
  for (int i = 0; i < sizeofArray(qos_fiveqi); ++i)
    if (qos_fiveqi[i] == fiveqi)
      return qos_priority[i];
  // AssertFatal(false, "illegal 5QI value %d\n", fiveqi);
  return 0;
}

static NR_QoS_config_t get_qos_config(const f1ap_qos_characteristics_t *qos_char)
{
  NR_QoS_config_t qos_c = {0};
  switch (qos_char->qos_type) {
    case DYNAMIC:
      qos_c.priority = qos_char->dynamic.qos_priority_level;
      qos_c.fiveQI = qos_char->dynamic.fiveqi > 0 ? qos_char->dynamic.fiveqi : 0;
      break;
    case NON_DYNAMIC:
      qos_c.fiveQI = qos_char->non_dynamic.fiveqi;
      qos_c.priority = get_non_dynamic_priority(qos_char->non_dynamic.fiveqi);
      break;
    default:
      // AssertFatal(false, "illegal QoS type %d\n", qos_char->qos_type);
      break;
  }
  return qos_c;
}

static int handle_ue_context_drbs_setup(NR_UE_info_t *UE,
                                        int drbs_len,
                                        const f1ap_drb_to_be_setup_t *req_drbs,
                                        f1ap_drb_to_be_setup_t **resp_drbs,
                                        NR_CellGroupConfig_t *cellGroupConfig)
{
  // DevAssert(req_drbs != NULL && resp_drbs != NULL && cellGroupConfig != NULL);
  SM_Log_Assert(_MAC_,req_drbs != NULL && resp_drbs != NULL && cellGroupConfig != NULL,"Required parameters for DRB setup are missing.");
  instance_t f1inst = get_f1_gtp_instance();
  SM_Logs(LOG_INFO, _MAC_, "Starting DRB setup for user with RNTI %04x and %d DRBs.", UE->rnti, drbs_len);

  /* Note: the actual GTP tunnels are created in the F1AP breanch of
   * ue_context_*_response() */
  *resp_drbs = calloc(drbs_len, sizeof(**resp_drbs));
  SM_Log_Assert(_MAC_,*resp_drbs != NULL, "Memory allocation failed for response DRBs.");
  // AssertFatal(*resp_drbs != NULL, "out of memory\n");
  for (int i = 0; i < drbs_len; i++) {
    const f1ap_drb_to_be_setup_t *drb = &req_drbs[i];
    f1ap_drb_to_be_setup_t *resp_drb = &(*resp_drbs)[i];
    NR_RLC_BearerConfig_t *rlc_BearerConfig = get_bearerconfig_from_drb(drb);
    nr_rlc_add_drb(UE->rnti, drb->drb_id, rlc_BearerConfig);

    SM_Logs(LOG_DEBUG, _MAC_, "Added DRB %d to RLC for user with RNTI %04x.", drb->drb_id, UE->rnti);
    nr_lc_config_t c = {.lcid = rlc_BearerConfig->logicalChannelIdentity, .nssai = drb->nssai};
    int prio = 100;
    for (int q = 0; q < drb->drb_info.flows_to_be_setup_length; ++q) {
      c.qos_config[q] = get_qos_config(&drb->drb_info.flows_mapped_to_drb[q].qos_params.qos_characteristics);
      prio = min(prio, c.qos_config[q].priority);
    }
    c.priority = prio;
    nr_mac_add_lcid(&UE->UE_sched_ctrl, &c);

    SM_Logs(LOG_DEBUG, _MAC_, "Configured LCID %d with priority %d for user with RNTI %04x.",c.lcid, c.priority, UE->rnti);

    *resp_drb = *drb;
    // just put same number of tunnels in DL as in UL
    // DevAssert(drb->up_ul_tnl_length == 1);
    resp_drb->up_dl_tnl_length = drb->up_ul_tnl_length;

    if (f1inst >= 0) { // we actually use F1-U
      int qfi = -1; // don't put PDU session marker in GTP
      gtpv1u_gnb_create_tunnel_resp_t resp_f1 = {0};
      int ret = drb_gtpu_create(f1inst,
                                UE->rnti,
                                drb->drb_id,
                                drb->drb_id,
                                qfi,
                                drb->up_ul_tnl[0].tl_address,
                                drb->up_ul_tnl[0].teid,
                                DURecvCb,
                                NULL,
                                &resp_f1);
      // AssertFatal(ret >= 0, "Unable to create GTP Tunnel for F1-U\n");

      SM_Log_Assert(_MAC_,ret >= 0, "Failed to create GTP tunnel.");

      SM_Logs(LOG_INFO, _MAC_, "Created GTP tunnel for DRB %d. DL TEID: %u, DL Address: %u.", drb->drb_id, resp_f1.gnb_NGu_teid[0], *(uint32_t *)resp_f1.gnb_addr.buffer);

      memcpy(&resp_drb->up_dl_tnl[0].tl_address, &resp_f1.gnb_addr.buffer, 4);
      resp_drb->up_dl_tnl[0].teid = resp_f1.gnb_NGu_teid[0];
    }

    int ret = ASN_SEQUENCE_ADD(&cellGroupConfig->rlc_BearerToAddModList->list, rlc_BearerConfig);
    // DevAssert(ret == 0);
    SM_Log_Assert(_MAC_,ret == 0, "Failed to add RLC Bearer Configuration.");
  }
  return drbs_len;
}

static int handle_ue_context_drbs_release(NR_UE_info_t *UE,
                                          int drbs_len,
                                          const f1ap_drb_to_be_released_t *req_drbs,
                                          NR_CellGroupConfig_t *cellGroupConfig)
{
  // DevAssert(req_drbs != NULL && cellGroupConfig != NULL);
  SM_Log_Assert(_MAC_,req_drbs != NULL && cellGroupConfig != NULL,"Required parameters for DRB release are missing.");
  instance_t f1inst = get_f1_gtp_instance();

  cellGroupConfig->rlc_BearerToReleaseList = calloc(1, sizeof(*cellGroupConfig->rlc_BearerToReleaseList));
  // AssertFatal(cellGroupConfig->rlc_BearerToReleaseList != NULL, "out of memory\n");
  SM_Log_Assert(_MAC_,cellGroupConfig->rlc_BearerToReleaseList != NULL, "Memory allocation failed  DRB context release.");


  /* Note: the actual GTP tunnels are already removed in the F1AP message
   * decoding */
  for (int i = 0; i < drbs_len; i++) {
    const f1ap_drb_to_be_released_t *drb = &req_drbs[i];

    long lcid = get_lcid_from_drbid(drb->rb_id);
    int idx = 0;
    while (idx < cellGroupConfig->rlc_BearerToAddModList->list.count) {
      const NR_RLC_BearerConfig_t *bc = cellGroupConfig->rlc_BearerToAddModList->list.array[idx];
      if (bc->logicalChannelIdentity == lcid)
        break;
      ++idx;
    }
    if (idx < cellGroupConfig->rlc_BearerToAddModList->list.count) {
      nr_mac_remove_lcid(&UE->UE_sched_ctrl, lcid);
      nr_rlc_release_entity(UE->rnti, lcid);
      if (f1inst >= 0)
        newGtpuDeleteOneTunnel(f1inst, UE->rnti, drb->rb_id);
      asn_sequence_del(&cellGroupConfig->rlc_BearerToAddModList->list, idx, 1);
      long *plcid = malloc(sizeof(*plcid));
      SM_Log_Assert(_MAC_,plcid != NULL, "Memory allocation failed for logical channel ID.");
      // AssertFatal(plcid, "out of memory\n");
      *plcid = lcid;
      int ret = ASN_SEQUENCE_ADD(&cellGroupConfig->rlc_BearerToReleaseList->list, plcid);
      // DevAssert(ret == 0);
      SM_Log_Assert(_MAC_,ret == 0, "Failed to add logical channel to the release list.");

      SM_Logs(LOG_DEBUG, _MAC_, "Logical channel ID %ld successfully prepared for release.", lcid);

    }
  }
  return drbs_len;
}

static NR_UE_NR_Capability_t *get_nr_cap(const NR_UE_CapabilityRAT_ContainerList_t *clist)
{
  for (int i = 0; i < clist->list.count; i++) {
    const NR_UE_CapabilityRAT_Container_t *c = clist->list.array[i];
    if (c->rat_Type != NR_RAT_Type_nr) {
      SM_Logs(LOG_WARN, _MAC_, "Failed to decode NR UE capability. Skipping this entry.");
      // LOG_W(NR_MAC, "ignoring capability of type %ld\n", c->rat_Type);
      continue;
    }

    NR_UE_NR_Capability_t *cap = NULL;
    asn_dec_rval_t dec_rval = uper_decode(NULL,
                                          &asn_DEF_NR_UE_NR_Capability,
                                          (void **)&cap,
                                          c->ue_CapabilityRAT_Container.buf,
                                          c->ue_CapabilityRAT_Container.size,
                                          0,
                                          0);
    if (dec_rval.code != RC_OK) {
      LOG_W(NR_MAC, "cannot decode NR UE capability, ignoring\n");
      ASN_STRUCT_FREE(asn_DEF_NR_UE_NR_Capability, cap);
      continue;
    }
    SM_Logs(LOG_INFO, _MAC_, "Successfully decoded NR UE capability.");
    return cap;
  }
  return NULL;
}

static NR_UE_NR_Capability_t *get_ue_nr_cap(int rnti, uint8_t *buf, uint32_t len)
{
  if (buf == NULL || len == 0){
    SM_Logs(LOG_WARN, _MAC_, "Invalid capability data for UE with RNTI %04x. No processing will be performed.", rnti);
    return NULL;
  }

  NR_UE_CapabilityRAT_ContainerList_t *clist = NULL;
  asn_dec_rval_t dec_rval = uper_decode(NULL, &asn_DEF_NR_UE_CapabilityRAT_ContainerList, (void **)&clist, buf, len, 0, 0);
  if (dec_rval.code != RC_OK) {
    SM_Logs(LOG_WARN, _MAC_, "Failed to decode UE capability container list for UE with RNTI %04x. Ignoring capabilities.", rnti);
    // LOG_W(NR_MAC, "cannot decode UE capability container list of UE RNTI %04x, ignoring capabilities\n", rnti);
    return NULL;
  }

  NR_UE_NR_Capability_t *cap = get_nr_cap(clist);
  ASN_STRUCT_FREE(asn_DEF_NR_UE_CapabilityRAT_ContainerList, clist);

  if (cap != NULL) {
    SM_Logs(LOG_INFO, _MAC_, "NR capability extracted for UE with RNTI %04x.", rnti);
  } else {
    SM_Logs(LOG_WARN, _MAC_, "No valid NR capability found for UE with RNTI %04x.", rnti);
  }

  return cap;
}

/* \brief return UE capabilties from HandoverPreparationInformation.
 *
 * The HandoverPreparationInformation contains more, but for the moment, let's
 * keep it simple and only handle that. The function asserts if other IEs are
 * present. */
static NR_UE_NR_Capability_t *get_ue_nr_cap_from_ho_prep_info(uint8_t *buf, uint32_t len)
{
  if (buf == NULL || len == 0){
    SM_Logs(LOG_WARN, _MAC_, "Invalid buffer or length for Handover Preparation Information. Skipping capability processing.");
    return NULL;
  }
  NR_HandoverPreparationInformation_t *hpi = NULL;
  asn_dec_rval_t dec_rval = uper_decode_complete(NULL, &asn_DEF_NR_HandoverPreparationInformation, (void **)&hpi, buf, len);
  if (dec_rval.code != RC_OK) {
    SM_Logs(LOG_WARN, _MAC_, "Failed to decode Handover Preparation Information. Ignoring capabilities.");
    // LOG_W(NR_MAC, "cannot decode HandoverPreparationInformation, ignoring capabilities\n");
    return NULL;
  }
  NR_UE_NR_Capability_t *cap = NULL;
  if (hpi->criticalExtensions.present != NR_HandoverPreparationInformation__criticalExtensions_PR_c1
      || hpi->criticalExtensions.choice.c1 == NULL
      || hpi->criticalExtensions.choice.c1->present
             != NR_HandoverPreparationInformation__criticalExtensions__c1_PR_handoverPreparationInformation
      || hpi->criticalExtensions.choice.c1->choice.handoverPreparationInformation == NULL) {
      SM_Logs(LOG_WARN, _MAC_, "Handover Preparation Information does not contain valid UE Capability RAT list.");
  } else {
    const NR_HandoverPreparationInformation_IEs_t *hpi_ie = hpi->criticalExtensions.choice.c1->choice.handoverPreparationInformation;
    cap = get_nr_cap(&hpi_ie->ue_CapabilityRAT_List);
    
    if (cap != NULL) {
      SM_Logs(LOG_INFO, _MAC_, "Successfully extracted NR UE Capability from Handover Preparation Information.");
    } else {
      SM_Logs(LOG_WARN, _MAC_, "No NR UE Capability could be extracted from the provided Handover Preparation Information.");
    }

  }
  ASN_STRUCT_FREE(asn_DEF_NR_HandoverPreparationInformation, hpi);
  return cap;
}

NR_CellGroupConfig_t *clone_CellGroupConfig(const NR_CellGroupConfig_t *orig)
{
  uint8_t buf[16636];
  asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_CellGroupConfig, NULL, orig, buf, sizeof(buf));
  // AssertFatal(enc_rval.encoded > 0, "could not clone CellGroupConfig: problem while encoding\n");
  SM_Log_Assert(_MAC_,enc_rval.encoded > 0,"Failed to encode CellGroupConfig during cloning. Check the input configuration.");

  NR_CellGroupConfig_t *cloned = NULL;
  asn_dec_rval_t dec_rval = uper_decode(NULL, &asn_DEF_NR_CellGroupConfig, (void **)&cloned, buf, enc_rval.encoded, 0, 0);
  // AssertFatal(dec_rval.code == RC_OK && dec_rval.consumed == enc_rval.encoded,
  //             "could not clone CellGroupConfig: problem while decodung\n");

    SM_Log_Assert(_MAC_,dec_rval.code == RC_OK && dec_rval.consumed == enc_rval.encoded,"Failed to decode CellGroupConfig during cloning. Possible encoding-decoding mismatch.");
  return cloned;
}

static NR_UE_info_t *create_new_UE(gNB_MAC_INST *mac, uint32_t cu_id)
{
  int CC_id = 0;
  const NR_COMMON_channels_t *cc = &mac->common_channels[CC_id];
  rnti_t rnti;
  bool found = nr_mac_get_new_rnti(&mac->UE_info, cc->ra, sizeofArray(cc->ra), &rnti);
  if (!found)
    return NULL;

  NR_UE_info_t* UE = add_new_nr_ue(mac, rnti, NULL);
  if (!UE)
    return NULL;

  f1_ue_data_t new_ue_data = {.secondary_ue = cu_id};
  du_add_f1_ue_data(rnti, &new_ue_data);

  const NR_ServingCellConfigCommon_t *scc = mac->common_channels[CC_id].ServingCellConfigCommon;
  const NR_ServingCellConfig_t *sccd = mac->common_channels[CC_id].pre_ServingCellConfig;
  NR_CellGroupConfig_t *cellGroupConfig = get_initial_cellGroupConfig(UE->uid, scc, sccd, &mac->radio_config);
  cellGroupConfig->spCellConfig->reconfigurationWithSync = get_reconfiguration_with_sync(UE->rnti, UE->uid, scc);
  // note: we don't pass the cellGroupConfig to add_new_nr_ue() because we need
  // the uid to create the CellGroupConfig (which is in the UE context created
  // by add_new_nr_ue(); it's a kind of chicken-and-egg problem), so below we
  // complete the UE context with the information that add_new_nr_ue() would
  // have added
  UE->Msg4_MsgB_ACKed = true;
  UE->CellGroup = cellGroupConfig;

  nr_rlc_activate_srb0(UE->rnti, UE, NULL);
  nr_mac_prepare_ra_ue(mac, rnti, UE->CellGroup);
  /* SRB1 is added to RLC and MAC in the handler later */
  return UE;
}

void ue_context_setup_request(const f1ap_ue_context_setup_t *req)
{
  SM_Logs(LOG_DEBUG, _MAC_, "Processing UE Context Setup Request. Allocating resources for a new or existing UE instance.");
  gNB_MAC_INST *mac = RC.nrmac[0];
  /* response has same type as request... */
  f1ap_ue_context_setup_t resp = {
    .gNB_CU_ue_id = req->gNB_CU_ue_id,
    .gNB_DU_ue_id = req->gNB_DU_ue_id,
  };

  /* gNB-DU UE ID is optional. As of now, the F1AP module fills -1 (on uint, so
   * 0xffffffff), but the DU uses the RNTI, so check if it's a legal RNTI in
   * which case we consider; if not, assume no DU UE ID given */
  bool ue_id_provided = resp.gNB_DU_ue_id > 0 && resp.gNB_DU_ue_id < 0xffff;

  NR_UE_NR_Capability_t *ue_cap = NULL;
  if (req->cu_to_du_rrc_information != NULL) {
    const cu_to_du_rrc_information_t *cu2du = req->cu_to_du_rrc_information;
    // AssertFatal(cu2du->cG_ConfigInfo == NULL, "CG-ConfigInfo not handled\n");
    SM_Log_Assert(_MAC_, cu2du->cG_ConfigInfo == NULL, "CG Configuration is not supported in the current implementation.");
    if (cu2du->handoverPreparationInfo != NULL) {
      ue_cap = get_ue_nr_cap_from_ho_prep_info(cu2du->handoverPreparationInfo, cu2du->handoverPreparationInfo_length);
    } else if (cu2du->uE_CapabilityRAT_ContainerList != NULL) {
      ue_cap = get_ue_nr_cap(req->gNB_DU_ue_id, cu2du->uE_CapabilityRAT_ContainerList, cu2du->uE_CapabilityRAT_ContainerList_length);
    }
    // AssertFatal(cu2du->measConfig == NULL, "MeasConfig not handled\n");
    SM_Log_Assert(_MAC_, cu2du->measConfig == NULL, "Measurement configuration is not supported in the current implementation.");
  }

  NR_SCHED_LOCK(&mac->sched_lock);

  NR_UE_info_t *UE = NULL;
  if (!ue_id_provided) {
    SM_Logs(LOG_DEBUG, _MAC_, "Creating a new UE context as the provided UE ID is invalid or not present.");
    UE = create_new_UE(mac, req->gNB_CU_ue_id);
    resp.gNB_DU_ue_id = UE->rnti;
    resp.crnti = &UE->rnti;
  } else {
    SM_Logs(LOG_DEBUG, _MAC_, "Looking up an existing UE context for the provided identifier.");
    UE = find_nr_UE(&mac->UE_info, req->gNB_DU_ue_id);
  }
  // AssertFatal(UE, "no UE found or could not be created, but UE Context Setup Failed not implemented\n");

    SM_Log_Assert(_MAC_, UE != NULL, "Unable to find or create a UE context. The setup process cannot proceed.");

  NR_CellGroupConfig_t *new_CellGroup = clone_CellGroupConfig(UE->CellGroup);

  if (req->srbs_to_be_setup_length > 0) {
    SM_Logs(LOG_DEBUG, _MAC_, "Starting the setup process for SRBs associated with the requested configuration.");
    resp.srbs_to_be_setup_length = handle_ue_context_srbs_setup(UE,
                                                                req->srbs_to_be_setup_length,
                                                                req->srbs_to_be_setup,
                                                                &resp.srbs_to_be_setup,
                                                                new_CellGroup);
  }

  if (req->drbs_to_be_setup_length > 0) {
    SM_Logs(LOG_DEBUG, _MAC_, "Starting the setup process for DRBs associated with the requested configuration.");
    resp.drbs_to_be_setup_length = handle_ue_context_drbs_setup(UE,
                                                                req->drbs_to_be_setup_length,
                                                                req->drbs_to_be_setup,
                                                                &resp.drbs_to_be_setup,
                                                                new_CellGroup);
  }

  if (req->rrc_container != NULL) {
    SM_Logs(LOG_DEBUG, _MAC_, "Processing the RRC Container for the UE context setup.");
    logical_chan_id_t id = 1;
    nr_rlc_srb_recv_sdu(req->gNB_DU_ue_id, id, req->rrc_container, req->rrc_container_length);
  }

  UE->capability = ue_cap;
  if (ue_cap != NULL) {
    SM_Logs(LOG_DEBUG, _MAC_, "Updating the CellGroup configuration based on the new UE capabilities.");
    // store the new UE capabilities, and update the cellGroupConfig
    NR_ServingCellConfigCommon_t *scc = mac->common_channels[0].ServingCellConfigCommon;
    update_cellGroupConfig(new_CellGroup, UE->uid, UE->capability, &mac->radio_config, scc);
  }

  if (!ue_id_provided) {
    /* new UE: tell the UE to reestablish RLC */
    struct NR_CellGroupConfig__rlc_BearerToAddModList *addmod = new_CellGroup->rlc_BearerToAddModList;
    for (int i = 0; i < addmod->list.count; ++i) {
      asn1cCallocOne(addmod->list.array[i]->reestablishRLC, NR_RLC_BearerConfig__reestablishRLC_true);
    }
  }

  resp.du_to_cu_rrc_information = calloc(1, sizeof(du_to_cu_rrc_information_t));
  SM_Log_Assert(_MAC_, resp.du_to_cu_rrc_information != NULL, "Memory allocation failed for DU-to-CU RRC Information.");
  // AssertFatal(resp.du_to_cu_rrc_information != NULL, "out of memory\n");
  resp.du_to_cu_rrc_information->cellGroupConfig = calloc(1,1024);
  // AssertFatal(resp.du_to_cu_rrc_information->cellGroupConfig != NULL, "out of memory\n");
  SM_Log_Assert(_MAC_, resp.du_to_cu_rrc_information->cellGroupConfig != NULL, "Memory allocation failed for CellGroup configuration.");
  asn_enc_rval_t enc_rval =
      uper_encode_to_buffer(&asn_DEF_NR_CellGroupConfig, NULL, new_CellGroup, resp.du_to_cu_rrc_information->cellGroupConfig, 1024);
  // AssertFatal(enc_rval.encoded > 0, "Could not encode CellGroup, failed element %s\n", enc_rval.failed_type->name);
  SM_Log_Assert(_MAC_, enc_rval.encoded > 0, "Failed to encode the CellGroup configuration. Check the encoding process.");
  resp.du_to_cu_rrc_information->cellGroupConfig_length = (enc_rval.encoded + 7) >> 3;

  nr_mac_prepare_cellgroup_update(mac, UE, new_CellGroup);

  NR_SCHED_UNLOCK(&mac->sched_lock);

  /* some sanity checks, since we use the same type for request and response */
  // DevAssert(resp.cu_to_du_rrc_information == NULL);
  // DevAssert(resp.du_to_cu_rrc_information != NULL);
  // DevAssert(resp.rrc_container == NULL && resp.rrc_container_length == 0);

  SM_Log_Assert(_MAC_, resp.cu_to_du_rrc_information == NULL, "CU-to-DU RRC Information must be NULL in the response.");
  SM_Log_Assert(_MAC_, resp.du_to_cu_rrc_information != NULL, "DU-to-CU RRC Information is missing in the response.");
  SM_Log_Assert(_MAC_, resp.rrc_container == NULL && resp.rrc_container_length == 0, "RRC Container fields must be empty in the response.");

  mac->mac_rrc.ue_context_setup_response(req, &resp);
  SM_Logs(LOG_DEBUG, _MAC_, "UE Context Setup Response successfully sent.");

  /* free the memory we allocated above */
  free(resp.srbs_to_be_setup);
  free(resp.drbs_to_be_setup);
  free(resp.du_to_cu_rrc_information->cellGroupConfig);
  free(resp.du_to_cu_rrc_information);
}

void ue_context_modification_request(const f1ap_ue_context_modif_req_t *req)
{
  SM_Logs(LOG_DEBUG, _MAC_, "Processing UE Context Modification Request to update resources for the specified user.");
  gNB_MAC_INST *mac = RC.nrmac[0];
  f1ap_ue_context_modif_resp_t resp = {
    .gNB_CU_ue_id = req->gNB_CU_ue_id,
    .gNB_DU_ue_id = req->gNB_DU_ue_id,
  };

  NR_UE_NR_Capability_t *ue_cap = NULL;
  if (req->cu_to_du_rrc_information != NULL) {
    // AssertFatal(req->cu_to_du_rrc_information->cG_ConfigInfo == NULL, "CG-ConfigInfo not handled\n");
    SM_Log_Assert(_MAC_, req->cu_to_du_rrc_information->cG_ConfigInfo == NULL, "CG Configuration is not supported.");
    ue_cap = get_ue_nr_cap(req->gNB_DU_ue_id,
                           req->cu_to_du_rrc_information->uE_CapabilityRAT_ContainerList,
                           req->cu_to_du_rrc_information->uE_CapabilityRAT_ContainerList_length);
    // AssertFatal(req->cu_to_du_rrc_information->measConfig == NULL, "MeasConfig not handled\n");
    SM_Log_Assert(_MAC_, req->cu_to_du_rrc_information->measConfig == NULL, "Measurement configuration is not supported.");
  }

  NR_SCHED_LOCK(&mac->sched_lock);
  NR_UE_info_t *UE = find_nr_UE(&RC.nrmac[0]->UE_info, req->gNB_DU_ue_id);
  if (!UE) {
    SM_Logs(LOG_ERROR, _MAC_, "Unable to find the specified user in the existing UE contexts.");
    // LOG_E(NR_MAC, "could not find UE with RNTI %04x\n", req->gNB_DU_ue_id);
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }

  NR_CellGroupConfig_t *new_CellGroup = clone_CellGroupConfig(UE->CellGroup);

  if (req->srbs_to_be_setup_length > 0) {
    SM_Logs(LOG_DEBUG, _MAC_, "Initiating SRB setup as part of the modification request.");
    resp.srbs_to_be_setup_length = handle_ue_context_srbs_setup(UE,
                                                                req->srbs_to_be_setup_length,
                                                                req->srbs_to_be_setup,
                                                                &resp.srbs_to_be_setup,
                                                                new_CellGroup);
  }

  if (req->drbs_to_be_setup_length > 0) {
    SM_Logs(LOG_DEBUG, _MAC_, "Initiating DRB setup as part of the modification request.");
    resp.drbs_to_be_setup_length = handle_ue_context_drbs_setup(UE,
                                                                req->drbs_to_be_setup_length,
                                                                req->drbs_to_be_setup,
                                                                &resp.drbs_to_be_setup,
                                                                new_CellGroup);
  }

  if (req->drbs_to_be_released_length > 0) {
    SM_Logs(LOG_DEBUG, _MAC_, "Initiating DRB release as part of the modification request.");
    resp.drbs_to_be_released_length =
        handle_ue_context_drbs_release(UE, req->drbs_to_be_released_length, req->drbs_to_be_released, new_CellGroup);
  }

  if (req->rrc_container != NULL) {
    SM_Logs(LOG_DEBUG, _MAC_, "Processing RRC Container associated with the modification request.");
    logical_chan_id_t id = 1;
    nr_rlc_srb_recv_sdu(req->gNB_DU_ue_id, id, req->rrc_container, req->rrc_container_length);
  }

  if (req->ReconfigComplOutcome != RRCreconf_info_not_present && req->ReconfigComplOutcome != RRCreconf_success) {
    SM_Logs(LOG_ERROR, _MAC_, "RRC Reconfiguration outcome indicates failure. Rollback mechanisms are not implemented.");
    // LOG_E(NR_MAC,
    //       "RRC reconfiguration outcome unsuccessful, but no rollback mechanism implemented to come back to old configuration\n");
  }

  if (ue_cap != NULL) {
    SM_Logs(LOG_DEBUG, _MAC_, "Updating UE capabilities and reconfiguring CellGroup.");
    // store the new UE capabilities, and update the cellGroupConfig
    ASN_STRUCT_FREE(asn_DEF_NR_UE_NR_Capability, UE->capability);
    UE->capability = ue_cap;
    // LOG_I(NR_MAC, "UE %04x: received capabilities, updating CellGroupConfig\n", UE->rnti);
    NR_ServingCellConfigCommon_t *scc = mac->common_channels[0].ServingCellConfigCommon;
    update_cellGroupConfig(new_CellGroup, UE->uid, UE->capability, &mac->radio_config, scc);
  }

  if (req->srbs_to_be_setup_length > 0 || req->drbs_to_be_setup_length > 0 || req->drbs_to_be_released_length > 0
      || ue_cap != NULL) {
    SM_Logs(LOG_DEBUG, _MAC_, "Preparing DU-to-CU RRC Information for updated CellGroup configuration.");
    resp.du_to_cu_rrc_information = calloc(1, sizeof(du_to_cu_rrc_information_t));
    // AssertFatal(resp.du_to_cu_rrc_information != NULL, "out of memory\n");
    SM_Log_Assert(_MAC_, resp.du_to_cu_rrc_information != NULL, "Memory allocation failed for DU-to-CU RRC Information.");
    resp.du_to_cu_rrc_information->cellGroupConfig = calloc(1, 1024);
    // AssertFatal(resp.du_to_cu_rrc_information->cellGroupConfig != NULL, "out of memory\n");
    SM_Log_Assert(_MAC_, resp.du_to_cu_rrc_information->cellGroupConfig != NULL, "Memory allocation failed for CellGroup configuration.");
    asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_CellGroupConfig,
                                                    NULL,
                                                    new_CellGroup,
                                                    resp.du_to_cu_rrc_information->cellGroupConfig,
                                                    1024);
    // AssertFatal(enc_rval.encoded > 0, "Could not encode CellGroup, failed element %s\n", enc_rval.failed_type->name);
    SM_Log_Assert(_MAC_, enc_rval.encoded > 0, "Failed to encode CellGroup configuration.");
    resp.du_to_cu_rrc_information->cellGroupConfig_length = (enc_rval.encoded + 7) >> 3;

    nr_mac_prepare_cellgroup_update(mac, UE, new_CellGroup);
  } else {
    ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, new_CellGroup); // we actually don't need it
  }

  if (req->transm_action_ind != NULL) {
    // AssertFatal(*req->transm_action_ind == TransmActionInd_STOP, "Transmission Action Indicator restart not handled yet\n");
    nr_transmission_action_indicator_stop(mac, UE);
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);

  /* some sanity checks, since we use the same type for request and response */
  // DevAssert(resp.cu_to_du_rrc_information == NULL);
  // resp.du_to_cu_rrc_information can be either NULL or not
  // DevAssert(resp.rrc_container == NULL && resp.rrc_container_length == 0);
  SM_Log_Assert(_MAC_, resp.cu_to_du_rrc_information == NULL, "CU-to-DU RRC Information must be NULL in the response.");
  mac->mac_rrc.ue_context_modification_response(req, &resp);

  /* free the memory we allocated above */
  free(resp.srbs_to_be_setup);
  free(resp.drbs_to_be_setup);
  if (resp.du_to_cu_rrc_information != NULL) {
    free(resp.du_to_cu_rrc_information->cellGroupConfig);
    free(resp.du_to_cu_rrc_information);
  }
}

void ue_context_modification_confirm(const f1ap_ue_context_modif_confirm_t *confirm)
{
  // LOG_I(MAC, "Received UE Context Modification Confirm for UE %04x\n", confirm->gNB_DU_ue_id);
  SM_Logs(LOG_INFO, _MAC_, "Received confirmation for user context modification. Proceeding with validation.");

  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  /* check first that the scheduler knows such UE */
  NR_UE_info_t *UE = find_nr_UE(&mac->UE_info, confirm->gNB_DU_ue_id);
  if (UE == NULL) {
    // LOG_E(MAC, "ERROR: unknown UE with RNTI %04x, ignoring UE Context Modification Confirm\n", confirm->gNB_DU_ue_id);

    SM_Logs(LOG_ERROR, _MAC_, "User not found for the provided context modification confirmation. Ignoring the confirmation.");
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);

  if (confirm->rrc_container_length > 0) {
    SM_Logs(LOG_DEBUG, _MAC_, "Processing received RRC Container as part of the confirmation.");
    logical_chan_id_t id = 1;
    nr_rlc_srb_recv_sdu(confirm->gNB_DU_ue_id, id, confirm->rrc_container, confirm->rrc_container_length);
  }
  /* nothing else to be done? */
}

void ue_context_modification_refuse(const f1ap_ue_context_modif_refuse_t *refuse)
{
  /* Currently, we only use the UE Context Modification Required procedure to
   * trigger a RRC reconfigurtion after Msg.3 with C-RNTI MAC CE. If the CU
   * refuses, it cannot do this reconfiguration, leaving the UE in an
   * unconfigured state. Therefore, we just free all RA-related info, and
   * request the release of the UE.  */
  // LOG_W(MAC, "Received UE Context Modification Refuse for %04x, requesting release\n", refuse->gNB_DU_ue_id);
    SM_Logs(LOG_WARN, _MAC_, "A modification request for a user was denied. Initiating user release.");

  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  NR_UE_info_t *UE = find_nr_UE(&RC.nrmac[0]->UE_info, refuse->gNB_DU_ue_id);
  if (UE == NULL) {
    // LOG_E(MAC, "ERROR: unknown UE with RNTI %04x, ignoring UE Context Modification Refuse\n", refuse->gNB_DU_ue_id);
    SM_Logs(LOG_ERROR, _MAC_, "Unknown user with provided ID %04x. No action will be taken.", refuse->gNB_DU_ue_id);
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }

  const int CC_id = 0;
  NR_COMMON_channels_t *cc = &mac->common_channels[CC_id];
  for (int i = 0; i < NR_NB_RA_PROC_MAX; i++) {
    NR_RA_t *ra = &cc->ra[i];
    if (ra->rnti == UE->rnti)
      nr_clear_ra_proc(ra);
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);

  f1ap_ue_context_release_req_t request = {
    .gNB_CU_ue_id = refuse->gNB_CU_ue_id,
    .gNB_DU_ue_id = refuse->gNB_DU_ue_id,
    .cause = F1AP_CAUSE_RADIO_NETWORK,
    .cause_value = F1AP_CauseRadioNetwork_procedure_cancelled,
  };
  mac->mac_rrc.ue_context_release_request(&request);
}

void ue_context_release_command(const f1ap_ue_context_release_cmd_t *cmd)
{
  /* mark UE as to be deleted after PUSCH failure */
  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  NR_UE_info_t *UE = find_nr_UE(&mac->UE_info, cmd->gNB_DU_ue_id);
  if (UE == NULL) {
    NR_SCHED_UNLOCK(&mac->sched_lock);
    f1ap_ue_context_release_complete_t complete = {
        .gNB_CU_ue_id = cmd->gNB_CU_ue_id,
        .gNB_DU_ue_id = cmd->gNB_DU_ue_id,
    };
    mac->mac_rrc.ue_context_release_complete(&complete);
    return;
  }

  instance_t f1inst = get_f1_gtp_instance();
  if (f1inst >= 0)
    newGtpuDeleteAllTunnels(f1inst, cmd->gNB_DU_ue_id);

  if (UE->UE_sched_ctrl.ul_failure || cmd->rrc_container_length == 0) {
    /* The UE is already not connected anymore or we have nothing to forward*/
    nr_mac_release_ue(mac, cmd->gNB_DU_ue_id);
    nr_mac_trigger_release_complete(mac, cmd->gNB_DU_ue_id);
  } else {
    /* UE is in sync: forward release message and mark to be deleted
     * after UL failure */
    nr_rlc_srb_recv_sdu(cmd->gNB_DU_ue_id, cmd->srb_id, cmd->rrc_container, cmd->rrc_container_length);
    nr_mac_trigger_release_timer(&UE->UE_sched_ctrl, UE->current_UL_BWP.scs);
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);
}

void dl_rrc_message_transfer(const f1ap_dl_rrc_message_t *dl_rrc)
{
  SM_Logs(LOG_DEBUG, _MAC_, "Handling DL RRC Message Transfer.");
  // LOG_D(NR_MAC,
  //       "DL RRC Message Transfer with %d bytes for RNTI %04x SRB %d\n",
  //       dl_rrc->rrc_container_length,
  //       dl_rrc->gNB_DU_ue_id,
  //       dl_rrc->srb_id);

  gNB_MAC_INST *mac = RC.nrmac[0];
  pthread_mutex_lock(&mac->sched_lock);
  /* check first that the scheduler knows such UE */
  NR_UE_info_t *UE = find_nr_UE(&mac->UE_info, dl_rrc->gNB_DU_ue_id);
  if (UE == NULL) {
    // LOG_E(MAC, "ERROR: unknown UE with RNTI %04x, ignoring DL RRC Message Transfer\n", dl_rrc->gNB_DU_ue_id);
    SM_Logs(LOG_ERROR, _MAC_, "UE with invalid RNTI %04x.", dl_rrc->gNB_DU_ue_id);
    pthread_mutex_unlock(&mac->sched_lock);
    return;
  }
  pthread_mutex_unlock(&mac->sched_lock);

  if (!du_exists_f1_ue_data(dl_rrc->gNB_DU_ue_id)) {
    // LOG_D(NR_MAC, "No CU UE ID stored for UE RNTI %04x, adding CU UE ID %d\n", dl_rrc->gNB_DU_ue_id, dl_rrc->gNB_CU_ue_id);
    SM_Logs(LOG_DEBUG, _MAC_, "No CU-related data exists for RNTI %04x. Adding new CU UE ID: %d.",dl_rrc->gNB_DU_ue_id, dl_rrc->gNB_CU_ue_id);
    f1_ue_data_t new_ue_data = {.secondary_ue = dl_rrc->gNB_CU_ue_id};
    du_add_f1_ue_data(dl_rrc->gNB_DU_ue_id, &new_ue_data);
  }

  if (UE->expect_reconfiguration && dl_rrc->srb_id == 1) {
    /* we expected a reconfiguration, and this is on DCCH. We assume this is
     * the reconfiguration: nr_mac_prepare_cellgroup_update() already stored
     * the CellGroupConfig. Below, we trigger a timer, and the CellGroupConfig
     * will be applied after its expiry in nr_mac_apply_cellgroup().*/
    NR_SCHED_LOCK(&mac->sched_lock);
    int delay = nr_mac_get_reconfig_delay_slots(UE->current_UL_BWP.scs);
    nr_mac_interrupt_ue_transmission(mac, UE, FOLLOW_INSYNC_RECONFIG, delay);
    NR_SCHED_UNLOCK(&mac->sched_lock);
    UE->expect_reconfiguration = false;
    /* Re-establish RLC for all remaining bearers */
    if (UE->reestablish_rlc) {
      SM_Logs(LOG_DEBUG, _MAC_, "Initiating RLC re-establishment for RNTI %04x.", dl_rrc->gNB_DU_ue_id);
      for (int i = 1; i < seq_arr_size(&UE->UE_sched_ctrl.lc_config); ++i) {
        nr_lc_config_t *lc_config = seq_arr_at(&UE->UE_sched_ctrl.lc_config, i);
        nr_rlc_reestablish_entity(dl_rrc->gNB_DU_ue_id, lc_config->lcid);
      }
      UE->reestablish_rlc = false;
    }
  }

  if (dl_rrc->old_gNB_DU_ue_id != NULL) {
    SM_Logs(LOG_INFO, _MAC_, "Old UE context detected for RNTI %04x. Processing context migration.", dl_rrc->gNB_DU_ue_id);
    // AssertFatal(*dl_rrc->old_gNB_DU_ue_id != dl_rrc->gNB_DU_ue_id,
    //             "logic bug: current and old gNB DU UE ID cannot be the same\n");

    SM_Log_Assert(_MAC_,*dl_rrc->old_gNB_DU_ue_id != dl_rrc->gNB_DU_ue_id, "Mismatch in UE identifiers: current and old UE IDs must differ.");
    /* 38.401 says: "Find UE context based on old gNB-DU UE F1AP ID, replace
     * old C-RNTI/PCI with new C-RNTI/PCI". Below, we do the inverse: we keep
     * the new UE context (with new C-RNTI), but set up everything to reuse the
     * old config. */
    NR_UE_info_t *oldUE = find_nr_UE(&mac->UE_info, *dl_rrc->old_gNB_DU_ue_id);
    // AssertFatal(oldUE, "CU claims we should know UE %04x, but we don't\n", *dl_rrc->old_gNB_DU_ue_id);
    SM_Log_Assert(_MAC_,oldUE, "Failed to locate old UE context");

    pthread_mutex_lock(&mac->sched_lock);
    /* 38.331 5.3.7.2 says that the UE releases the spCellConfig, so we drop it
     * from the current configuration. Also, expect the reconfiguration from
     * the CU, so save the old UE's CellGroup for the new UE */
    UE->CellGroup->spCellConfig = NULL;
    NR_UE_sched_ctrl_t *sched_ctrl = &UE->UE_sched_ctrl;
    NR_ServingCellConfigCommon_t *scc = mac->common_channels[0].ServingCellConfigCommon;
    uid_t temp_uid = UE->uid;
    UE->uid = oldUE->uid;
    oldUE->uid = temp_uid;
    configure_UE_BWP(mac, scc, sched_ctrl, NULL, UE, -1, -1);
    for (int i = 1; i < seq_arr_size(&oldUE->UE_sched_ctrl.lc_config); ++i) {
      const nr_lc_config_t *c = seq_arr_at(&oldUE->UE_sched_ctrl.lc_config, i);
      nr_mac_add_lcid(&UE->UE_sched_ctrl, c);
    }

    nr_mac_prepare_cellgroup_update(mac, UE, oldUE->CellGroup);
    oldUE->CellGroup = NULL;
    mac_remove_nr_ue(mac, *dl_rrc->old_gNB_DU_ue_id);
    pthread_mutex_unlock(&mac->sched_lock);
    nr_rlc_remove_ue(dl_rrc->gNB_DU_ue_id);
    nr_rlc_update_id(*dl_rrc->old_gNB_DU_ue_id, dl_rrc->gNB_DU_ue_id);
    /* Set flag to trigger RLC re-establishment
     * for remaining RBs in next RRCReconfiguration */
    UE->reestablish_rlc = true;
    /* 38.331 clause 5.3.7.4: apply the specified configuration defined in 9.2.1 for SRB1 */
    nr_rlc_reconfigure_entity(dl_rrc->gNB_DU_ue_id, 1, NULL);
    instance_t f1inst = get_f1_gtp_instance();
    if (f1inst >= 0) // we actually use F1-U
      gtpv1u_update_ue_id(f1inst, *dl_rrc->old_gNB_DU_ue_id, dl_rrc->gNB_DU_ue_id);
  }

  /* the DU ue id is the RNTI */
  SM_Logs(LOG_INFO, _MAC_, "Delivering SDU for RNTI %04x on SRB ID %d.", dl_rrc->gNB_DU_ue_id, dl_rrc->srb_id);
  nr_rlc_srb_recv_sdu(dl_rrc->gNB_DU_ue_id, dl_rrc->srb_id, dl_rrc->rrc_container, dl_rrc->rrc_container_length);
}
