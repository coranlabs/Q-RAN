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

#include <stdlib.h>

#include "assertions.h"

#include "rrc_gNB_mobility.h"

#include "nr_rrc_proto.h"
#include "rrc_gNB_du.h"
#include "rrc_gNB_radio_bearers.h"
#include "rrc_gNB_UE_context.h"
#include "openair2/F1AP/f1ap_ids.h"
#include "MESSAGES/asn1_msg.h"
#include "nr_pdcp/nr_pdcp_oai_api.h"
#include "openair3/SECU/key_nas_deriver.h"

/* declared in rrc_gNB.c: should be cleaned up through helper function? useful? */
extern mui_t rrc_gNB_mui;

typedef enum { HO_CTX_BOTH, HO_CTX_SOURCE, HO_CTX_TARGET } ho_ctx_type_t;
static nr_handover_context_t *alloc_ho_ctx(ho_ctx_type_t type)
{
  nr_handover_context_t *ho_ctx = calloc(1, sizeof(*ho_ctx));
  // AssertFatal(ho_ctx != NULL, "out of memory\n");
  if (type == HO_CTX_SOURCE || type == HO_CTX_BOTH) {
    ho_ctx->source = calloc(1, sizeof(*ho_ctx->source));
    // AssertFatal(ho_ctx->source != NULL, "out of memory\n");
  }
  if (type == HO_CTX_TARGET || type == HO_CTX_BOTH) {
    ho_ctx->target = calloc(1, sizeof(*ho_ctx->target));
    // AssertFatal(ho_ctx->target != NULL, "out of memory\n");
  }
  return ho_ctx;
}

static void free_ho_ctx(nr_handover_context_t *ho_ctx)
{
  free(ho_ctx->source);
  free(ho_ctx->target);
  free(ho_ctx);
}

/* \brief Initiate a handover of UE to a specific target cell handled by this
 * CU.
 * \param ue a UE context for which the handover should be triggered. The UE
 * context must be non-null (if the HO request comes from "outside" (N2, Xn),
 * the UE contex must be created first.
 * \param target_du the DU towards which to handover. Note: currently, the CU
 * is limited to one cell per DU, so DU and cell are equivalent here.
 * \param ho_ctxt contextual data for the type of handover (F1, N2, Xn) */
static void nr_initiate_handover(const gNB_RRC_INST *rrc,
                                 gNB_RRC_UE_t *ue,
                                 const nr_rrc_du_container_t *source_du,
                                 const nr_rrc_du_container_t *target_du,
                                 uint8_t *ho_prep_buf,
                                 uint32_t ho_prep_len,
                                 ho_req_ack_t ack,
                                 ho_success_t success,
                                 ho_cancel_t cancel)
{
  // DevAssert(rrc != NULL);
  // DevAssert(ue != NULL);
  // DevAssert(target_du != NULL);
  // source_du might be NULL -> inter-CU handover
  DevAssert(ho_prep_buf != NULL && ho_prep_len > 0);

  if (ue->ho_context != NULL) {
    // LOG_E(NR_RRC, "handover for UE %u ongoing, cannot trigger new\n", ue->rrc_ue_id);
    return;
  }
  // if any reconfiguration is ongoing, abort handover request
  for (int i = 0; i < NR_RRC_TRANSACTION_IDENTIFIER_NUMBER; ++i) {
    if (ue->xids[i] != RRC_ACTION_NONE) {
      // LOG_E(NR_RRC, "UE %d: ongoig transaction %d (action %d), cannot trigger handover\n", ue->rrc_ue_id, i, ue->xids[i]);
      return;
    }
  }

  // if we know the source DU (F1 handover), allocate the context for it,
  // otherwise only for the target DU (N2, Xn)
  ho_ctx_type_t ctx_type = source_du != NULL ? HO_CTX_BOTH : HO_CTX_TARGET;
  nr_handover_context_t *ho_ctx = alloc_ho_ctx(ctx_type);
  ho_ctx->target->du = target_du;
  // we will know target->{du_ue_id,new_rnti} once we have UE ctxt setup
  // response
  ho_ctx->target->ho_req_ack = ack;
  ho_ctx->target->ho_success = success;

  const f1_ue_data_t ue_data = cu_get_f1_ue_data(ue->rrc_ue_id);
  if (source_du != NULL) {
    // DevAssert(source_du->assoc_id == ue_data.du_assoc_id);
    // we also have the source DU (F1 handover), store meta info
    ho_ctx->source->ho_cancel = cancel;

    ho_ctx->source->du = source_du;
    ho_ctx->source->du_ue_id = ue_data.secondary_ue;
    ho_ctx->source->old_rnti = ue->rnti;

    int result = asn_copy(&asn_DEF_NR_CellGroupConfig, (void **)&ho_ctx->source->old_cellGroupConfig, ue->masterCellGroup);
    // AssertFatal(result == 0, "error during asn_copy() of CellGroupConfig\n");
  }
  ue->ho_context = ho_ctx;
  // LOG_A(NR_RRC,
  //       "Handover triggered for UE %u/RNTI %04x towards DU %ld/assoc_id %d/PCI %d\n",
  //       ue->rrc_ue_id,
  //       ue->rnti,
  //       target_du->setup_req->gNB_DU_id,
  //       target_du->assoc_id,
  //       target_du->setup_req->cell[0].info.nr_pci);

  cu_to_du_rrc_information_t cu2du = {
      .handoverPreparationInfo = ho_prep_buf,
      .handoverPreparationInfo_length = ho_prep_len,
  };

  f1ap_drb_to_be_setup_t drbs[32] = {0}; // maximum DRB can be 32
  int nb_drb = 0;
  for (int i = 0; i < 32; ++i) { /* for each DRB */
    drb_t *rrc_drb = &ue->established_drbs[i];
    if (rrc_drb->status == DRB_INACTIVE)
      continue;

    f1ap_drb_to_be_setup_t *drb = &drbs[nb_drb];
    nb_drb++;
    drb->drb_id = rrc_drb->drb_id;
    drb->rlc_mode = rrc->configuration.um_on_default_drb ? F1AP_RLC_MODE_UM_BIDIR : F1AP_RLC_MODE_AM;
    memcpy(&drb->up_ul_tnl[0].tl_address, &rrc_drb->cuup_tunnel_config.addr.buffer, sizeof(uint8_t) * 4);
    drb->up_ul_tnl[0].port = rrc->eth_params_s.my_portd;
    drb->up_ul_tnl[0].teid = rrc_drb->cuup_tunnel_config.teid;
    drb->up_ul_tnl_length = 1;

    /* fetch an existing PDU session for this DRB */
    rrc_pdu_session_param_t *pdu = find_pduSession_from_drbId(ue, drb->drb_id);
    // AssertFatal(pdu != NULL, "no PDU session for DRB ID %ld\n", drb->drb_id);
    drb->nssai = pdu->param.nssai;

    // for the moment, we only support one QoS flow. Put a reminder in case
    // this changes
    // AssertFatal(pdu->param.nb_qos == 1, "only 1 Qos flow supported\n");
    drb->drb_info.flows_to_be_setup_length = 1;
    drb->drb_info.flows_mapped_to_drb = calloc(1, sizeof(f1ap_flows_mapped_to_drb_t));
    // AssertFatal(drb->drb_info.flows_mapped_to_drb, "could not allocate memory\n");
    int qfi = rrc_drb->cnAssociation.sdap_config.mappedQoS_FlowsToAdd[0];
    // DevAssert(qfi > 0);
    drb->drb_info.flows_mapped_to_drb[0].qfi = qfi;
    pdusession_level_qos_parameter_t *in_qos_char = get_qos_characteristics(qfi, pdu);
    drb->drb_info.flows_mapped_to_drb[0].qos_params.qos_characteristics = get_qos_char_from_qos_flow_param(in_qos_char);

    /* the DRB QoS parameters: we just reuse the ones from the first flow */
    drb->drb_info.drb_qos = drb->drb_info.flows_mapped_to_drb[0].qos_params;
  }

  f1ap_srb_to_be_setup_t srbs[2] = {{.srb_id = 1, .lcid = 1}, {.srb_id = 2, .lcid = 2}};
  f1ap_served_cell_info_t *cell_info = &target_du->setup_req->cell[0].info;
  RETURN_IF_INVALID_ASSOC_ID(target_du->assoc_id);
  f1ap_ue_context_setup_t ue_context_setup_req = {
      .gNB_CU_ue_id = ue->rrc_ue_id,
      .gNB_DU_ue_id = 0, /* TODO: this should be optional! */
      .plmn.mcc = cell_info->plmn.mcc,
      .plmn.mnc = cell_info->plmn.mnc,
      .plmn.mnc_digit_length = cell_info->plmn.mnc_digit_length,
      .nr_cellid = cell_info->nr_cellid,
      .servCellId = 0, // TODO: correct value?
      .srbs_to_be_setup_length = 2,
      .srbs_to_be_setup = srbs,
      .drbs_to_be_setup_length = nb_drb,
      .drbs_to_be_setup = drbs,
      .cu_to_du_rrc_information = &cu2du,
  };
  rrc->mac_rrc.ue_context_setup_request(target_du->assoc_id, &ue_context_setup_req);
}

/*
void nr_rrc_trigger_n2_ho(gNB_RRC_INST *rrc, int nr_cgi, uint8_t *ho_prep_info, uint32_t *ho_prep_len, void *pdu_session)
{
  // call from outside (via NGAP message), with the request for handover for a
  // (any) UE. This handler should, in order:
  //
  // 1. look up the target DU =du= via get_du_by_cell_id(nr_cgi)
  //    (and reject the request if it can't find the DU)
  // 2. create a context for a new UE =UE=, based on pdu_session information
  // 3. call nr_initiate_handover(rrc, UE, NULL, du, ho_prep_info, ho_prep_len);
  //
  // after success of the DU, send the result via =success_ptr= to the source
  // CU.
}
*/

typedef struct deliver_ue_ctxt_modification_data_t {
  gNB_RRC_INST *rrc;
  f1ap_ue_context_modif_req_t *modification_req;
  sctp_assoc_t assoc_id;
} deliver_ue_ctxt_modification_data_t;
static void rrc_deliver_ue_ctxt_modif_req(void *deliver_pdu_data, ue_id_t ue_id, int srb_id, char *buf, int size, int sdu_id)
{
  DevAssert(deliver_pdu_data != NULL);
  deliver_ue_ctxt_modification_data_t *data = deliver_pdu_data;
  data->modification_req->rrc_container = (uint8_t*)buf;
  data->modification_req->rrc_container_length = size;
  data->rrc->mac_rrc.ue_context_modification_request(data->assoc_id, data->modification_req);
}
static void rrc_gNB_trigger_reconfiguration_for_handover(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue, uint8_t *rrc_reconf, int rrc_reconf_len)
{
  f1_ue_data_t ue_data = cu_get_f1_ue_data(ue->rrc_ue_id);

  TransmActionInd_t transmission_action_indicator = TransmActionInd_STOP;
  RETURN_IF_INVALID_ASSOC_ID(ue_data.du_assoc_id);
  f1ap_ue_context_modif_req_t ue_context_modif_req = {
      .gNB_CU_ue_id = ue->rrc_ue_id,
      .gNB_DU_ue_id = ue_data.secondary_ue,
      .plmn.mcc = rrc->configuration.mcc[0],
      .plmn.mnc = rrc->configuration.mnc[0],
      .plmn.mnc_digit_length = rrc->configuration.mnc_digit_length[0],
      .nr_cellid = rrc->nr_cellid, // TODO target cell ID
      .servCellId = 0, // TODO: correct value?
      .ReconfigComplOutcome = RRCreconf_success,
      .transm_action_ind = &transmission_action_indicator,
  };
  deliver_ue_ctxt_modification_data_t data = {.rrc = rrc,
                                              .modification_req = &ue_context_modif_req,
                                              .assoc_id = ue_data.du_assoc_id};
  int srb_id = 1;
  nr_pdcp_data_req_srb(ue->rrc_ue_id,
                       srb_id,
                       rrc_gNB_mui++,
                       rrc_reconf_len,
                       (unsigned char *const)rrc_reconf,
                       rrc_deliver_ue_ctxt_modif_req,
                       &data);
}

static void nr_rrc_f1_ho_acknowledge(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE, uint8_t *rrc_reconf_buf, uint32_t rrc_reconf_len)
{
  // N2/Xn HO: fill with UE caps, as-context, rrc reconf, send to source CU
  // also, fill the UE->rnti from the new one (in F1 case, happens after
  // confirmation of ue ctxt modif

  // F1 HO: handling of "source CU" information
  DevAssert(UE->ho_context->source != NULL);
  rrc_gNB_trigger_reconfiguration_for_handover(rrc, UE, rrc_reconf_buf, rrc_reconf_len);
  // LOG_A(NR_RRC, "HO acknowledged: Send reconfiguration for UE %u/RNTI %04x...\n", UE->rrc_ue_id, UE->rnti);

  /* Re-establish SRB2 according to clause 5.3.5.6.3 of 3GPP TS 38.331
   * (SRB1 is re-established with RRCReestablishment message)
   */
  int srb_id = 2;
  if (UE->Srb[srb_id].Active) {
    nr_pdcp_entity_security_keys_and_algos_t security_parameters;
    /* Derive the keys from kgnb */
    nr_derive_key(RRC_ENC_ALG, UE->ciphering_algorithm, UE->kgnb, security_parameters.ciphering_key);
    nr_derive_key(RRC_INT_ALG, UE->integrity_algorithm, UE->kgnb, security_parameters.integrity_key);
    security_parameters.integrity_algorithm = UE->integrity_algorithm;
    security_parameters.ciphering_algorithm = UE->ciphering_algorithm;
    nr_pdcp_reestablishment(UE->rrc_ue_id, srb_id, true, &security_parameters);
  }
}

static void nr_rrc_f1_ho_complete(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  // DevAssert(UE->ho_context != NULL);
  nr_ho_source_cu_t *source_ctx = UE->ho_context->source;
  // DevAssert(source_ctx != NULL);
  RETURN_IF_INVALID_ASSOC_ID(source_ctx->du->assoc_id);
  f1ap_ue_context_release_cmd_t cmd = {
      .gNB_CU_ue_id = UE->rrc_ue_id,
      .gNB_DU_ue_id = source_ctx->du_ue_id,
      .cause = F1AP_CAUSE_RADIO_NETWORK, // better
      .cause_value = 5, // 5 = F1AP_CauseRadioNetwork_interaction_with_other_procedure
      .srb_id = DCCH,
  };
  rrc->mac_rrc.ue_context_release_command(source_ctx->du->assoc_id, &cmd);
  // LOG_I(NR_RRC, "UE %d Handover: trigger release on DU assoc_id %d\n", UE->rrc_ue_id, source_ctx->du->assoc_id);
}

static void nr_rrc_cancel_f1_ho(gNB_RRC_INST *rrc, gNB_RRC_UE_t *UE)
{
  // DevAssert(UE->ho_context != NULL);
  nr_ho_target_cu_t *target_ctx = UE->ho_context->target;
  // DevAssert(target_ctx != NULL);
  f1ap_ue_context_release_cmd_t cmd = {
      .gNB_CU_ue_id = UE->rrc_ue_id,
      .gNB_DU_ue_id = target_ctx->du_ue_id,
      .cause = F1AP_CAUSE_RADIO_NETWORK, // better
      .cause_value = 5, // 5 = F1AP_CauseRadioNetwork_interaction_with_other_procedure
      .srb_id = DCCH,
  };
  rrc->mac_rrc.ue_context_release_command(target_ctx->du->assoc_id, &cmd);
}

void nr_rrc_trigger_f1_ho(gNB_RRC_INST *rrc, gNB_RRC_UE_t *ue, nr_rrc_du_container_t *source_du, nr_rrc_du_container_t *target_du)
{
  // DevAssert(rrc != NULL);
  // DevAssert(ue != NULL);

  uint8_t buf[NR_RRC_BUF_SIZE];
  int size = do_NR_HandoverPreparationInformation(ue->ue_cap_buffer.buf, ue->ue_cap_buffer.len, buf, sizeof buf);

  // corresponds to a "handover request", 38.300 Sec 9.3.2.3
  // see also 38.413 Sec 9.3.1.29 for information on source-CU to target-CU
  // information (Source NG-RAN Node to Target NG-RAN Node Transparent Container)
  // here: target Cell is preselected, target CU has access to UE information
  // and therefore also the PDU sessions. Orig RRC reconfiguration should be in
  // handover preparation information
  ho_req_ack_t ack = nr_rrc_f1_ho_acknowledge;
  ho_success_t success = nr_rrc_f1_ho_complete;
  ho_cancel_t cancel = nr_rrc_cancel_f1_ho;
  nr_initiate_handover(rrc, ue, source_du, target_du, buf, size, ack, success, cancel);
}

void nr_rrc_finalize_ho(gNB_RRC_UE_t *ue)
{
  if (ue->ho_context->source)
    ASN_STRUCT_FREE(asn_DEF_NR_CellGroupConfig, ue->ho_context->source->old_cellGroupConfig);
  free_ho_ctx(ue->ho_context);
  ue->ho_context = NULL;
}

void nr_HO_F1_trigger_telnet(gNB_RRC_INST *rrc, uint32_t rrc_ue_id)
{
  rrc_gNB_ue_context_t *ue_context_p = rrc_gNB_get_ue_context(rrc, rrc_ue_id);
  if (ue_context_p == NULL) {
    // LOG_E(NR_RRC, "cannot find UE context for UE ID %d\n", rrc_ue_id);
    SM_Logs(LOG_ERROR,_RRC_,"ue context not found, ueid : %d", rrc_ue_id);
    return;
  }
  
  gNB_RRC_UE_t *ue = &ue_context_p->ue_context;
  nr_rrc_du_container_t *source_du = get_du_for_ue(rrc, ue->rrc_ue_id);
  if (source_du == NULL) {
    f1_ue_data_t ue_data = cu_get_f1_ue_data(rrc_ue_id);
    // LOG_E(NR_RRC, "cannot get source gNB-DU with assoc_id %d for UE %u\n", ue_data.du_assoc_id, ue->rrc_ue_id);
    return;
  }

  nr_rrc_du_container_t *target_du = find_target_du(rrc, source_du->assoc_id);
  if (target_du == NULL) {
    // LOG_E(NR_RRC, "No target gNB-DU found. Handover for UE %u aborted.\n", ue->rrc_ue_id);
    return;
  }

  nr_rrc_trigger_f1_ho(rrc, ue, source_du, target_du);
}
