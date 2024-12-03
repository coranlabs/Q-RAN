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

/* \file        nr_ue_scheduler.c
 * \brief       Routines for UE scheduling
 * \author      Guido Casati
 * \date        Jan 2021
 * \version     0.1
 * \company     Fraunhofer IIS
 * \email       guido.casati@iis.fraunhofer.de
 */

#include <stdio.h>
#include <math.h>
#include <pthread.h>

/* exe */
#include <common/utils/nr/nr_common.h>

/* MAC */
#include "NR_MAC_COMMON/nr_mac.h"
#include "NR_MAC_COMMON/nr_mac_common.h"
#include "NR_MAC_UE/mac_proto.h"
#include "NR_MAC_UE/mac_extern.h"

/* utils */
#include "assertions.h"
#include "oai_asn1.h"
#include "SIMULATION/TOOLS/sim.h" // for taus
#include "utils.h"

#include <executables/softmodem-common.h>

#include "LAYER2/NR_MAC_COMMON/nr_mac_extern.h"
#include "LAYER2/RLC/rlc.h"
#include "RRC/NR_UE/L2_interface_ue.h"

//#define SRS_DEBUG
#define verifyMutex(a)                                                \
  {                                                                   \
    int ret = a;                                                      \
    AssertFatal(ret == 0, "Failure in mutex management ret=%d\n", a); \
  }

static void nr_ue_prach_scheduler(NR_UE_MAC_INST_t *mac, frame_t frameP, sub_frame_t slotP);
static void schedule_ta_command(fapi_nr_dl_config_request_t *dl_config, NR_UE_MAC_INST_t *mac);
static void nr_ue_fill_phr(NR_UE_MAC_INST_t *mac,
                           NR_SINGLE_ENTRY_PHR_MAC_CE *phr,
                           float P_CMAX,
                           float tx_power,
                           frame_t frameP,
                           sub_frame_t subframe);
static uint8_t nr_ue_get_sdu(NR_UE_MAC_INST_t *mac,
                             int cc_id,
                             frame_t frameP,
                             sub_frame_t subframe,
                             uint8_t gNB_index,
                             uint8_t *ulsch_buffer,
                             const uint32_t buflen,
                             int tx_power,
                             int P_MAX,
                             bool *BSRsent);

void clear_ul_config_request(NR_UE_MAC_INST_t *mac, int scs)
{
  int slots = nr_slots_per_frame[scs];
  for (int i = 0; i < slots ; i++) {
    fapi_nr_ul_config_request_t *ul_config = mac->ul_config_request + i;
    verifyMutex(pthread_mutex_lock(&ul_config->mutex_ul_config));
    ul_config->number_pdus = 0;
    verifyMutex(pthread_mutex_unlock(&ul_config->mutex_ul_config));
  }
}

fapi_nr_ul_config_request_pdu_t *lockGet_ul_config(NR_UE_MAC_INST_t *mac, frame_t frame_tx, int slot_tx, uint8_t pdu_type)
{
  NR_TDD_UL_DL_ConfigCommon_t *tdd_config = mac->tdd_UL_DL_ConfigurationCommon;

  // Check if requested on the right slot
  if (!is_nr_UL_slot(tdd_config, slot_tx, mac->frame_type)) {
    LOG_E(NR_MAC, "Lock of UL config for PDU type %d called for slot %d which is not UL\n", pdu_type, slot_tx);
    return NULL;
  }

  AssertFatal(mac->ul_config_request != NULL, "mac->ul_config_request not initialized, logic bug\n");
  fapi_nr_ul_config_request_t *ul_config = mac->ul_config_request + slot_tx;

  verifyMutex(pthread_mutex_lock(&ul_config->mutex_ul_config));
  if (ul_config->number_pdus != 0 && (ul_config->frame != frame_tx || ul_config->slot != slot_tx)) {
    LOG_E(NR_MAC, "Error in ul config consistency, clearing slot %d\n", slot_tx);
    ul_config->number_pdus = 0;
  }
  ul_config->frame = frame_tx;
  ul_config->slot = slot_tx;
  if (ul_config->number_pdus >= FAPI_NR_UL_CONFIG_LIST_NUM) {
    LOG_E(NR_MAC, "Error in ul config for slot %d, no memory\n", slot_tx);
    verifyMutex(pthread_mutex_unlock(&ul_config->mutex_ul_config));
    return NULL;
  }
  fapi_nr_ul_config_request_pdu_t *pdu = ul_config->ul_config_list + ul_config->number_pdus++;
  pdu->pdu_type = pdu_type;
  pdu->lock = &ul_config->mutex_ul_config;
  pdu->privateNBpdus = &ul_config->number_pdus;
  LOG_D(NR_MAC, "Added ul pdu for %d.%d, type %d\n", frame_tx, slot_tx, pdu_type);
  return pdu;
}

static nr_lcordered_info_t *get_lc_info_from_lcid(NR_UE_MAC_INST_t *mac, NR_LogicalChannelIdentity_t lcid)
{
  nr_lcordered_info_t *lc_info = NULL;
  for (int i = 0; i < mac->lc_ordered_list.count; i++) {
    if (mac->lc_ordered_list.array[i]->lcid == lcid) {
      lc_info = mac->lc_ordered_list.array[i];
      break;
    }
  }
  return lc_info;
}

static int lcid_buffer_index(NR_LogicalChannelIdentity_t lcid)
{
  AssertFatal(lcid > 0 && lcid <= NR_MAX_NUM_LCID, "Invalid LCID %ld\n", lcid);
  return lcid - 1;
}

NR_LC_SCHEDULING_INFO *get_scheduling_info_from_lcid(NR_UE_MAC_INST_t *mac, NR_LogicalChannelIdentity_t lcid)
{
  int idx = lcid_buffer_index(lcid);
  return &mac->scheduling_info.lc_sched_info[idx];
}

static void trigger_regular_bsr(NR_UE_MAC_INST_t *mac, NR_LogicalChannelIdentity_t lcid, bool sr_DelayTimerApplied)
{
  // Regular BSR trigger
  mac->scheduling_info.BSR_reporting_active |= NR_BSR_TRIGGER_REGULAR;
  mac->scheduling_info.regularBSR_trigger_lcid = lcid;
  LOG_D(NR_MAC, "Triggering regular BSR for LCID %ld\n", lcid);
  // if the BSR is triggered for a logical channel for which logicalChannelSR-DelayTimerApplied with value true
  // start or restart the logicalChannelSR-DelayTimer
  // else stop the logicalChannelSR-DelayTimer
  if (sr_DelayTimerApplied)
    nr_timer_start(&mac->scheduling_info.sr_DelayTimer);
  else
    nr_timer_stop(&mac->scheduling_info.sr_DelayTimer);
}

void handle_time_alignment_timer_expired(NR_UE_MAC_INST_t *mac)
{
  // flush all HARQ buffers for all Serving Cells
  for (int k = 0; k < NR_MAX_HARQ_PROCESSES; k++) {
    memset(&mac->dl_harq_info[k], 0, sizeof(NR_UE_HARQ_STATUS_t));
    memset(&mac->ul_harq_info[k], 0, sizeof(NR_UL_HARQ_INFO_t));
    mac->dl_harq_info[k].last_ndi = -1; // initialize to invalid value
    mac->ul_harq_info[k].last_ndi = -1; // initialize to invalid value
  }
  // release PUCCH for all Serving Cells;
  // release SRS for all Serving Cells;
  release_PUCCH_SRS(mac);
  // clear any configured downlink assignments and uplink grants;
  int scs = mac->current_UL_BWP ? mac->current_UL_BWP->scs : get_softmodem_params()->numerology;
  if (mac->dl_config_request)
    memset(mac->dl_config_request, 0, sizeof(*mac->dl_config_request));
  if (mac->ul_config_request)
    clear_ul_config_request(mac, scs);
  // clear any PUSCH resources for semi-persistent CSI reporting
  // TODO we don't have semi-persistent CSI reporting
  // maintain N_TA
  // TODO not sure what to do here
}

void update_mac_timers(NR_UE_MAC_INST_t *mac)
{
  if (mac->data_inactivity_timer) {
    bool inactivity_timer_expired = nr_timer_tick(mac->data_inactivity_timer);
    if (inactivity_timer_expired)
      nr_mac_rrc_inactivity_timer_ind(mac->ue_id);
  }
  bool alignment_timer_expired = nr_timer_tick(&mac->time_alignment_timer);
  if (alignment_timer_expired)
    handle_time_alignment_timer_expired(mac);
  nr_timer_tick(&mac->ra.contention_resolution_timer);
  for (int j = 0; j < NR_MAX_SR_ID; j++)
    nr_timer_tick(&mac->scheduling_info.sr_info[j].prohibitTimer);
  nr_timer_tick(&mac->scheduling_info.sr_DelayTimer);
  bool retxBSR_expired = nr_timer_tick(&mac->scheduling_info.retxBSR_Timer);
  if (retxBSR_expired) {
    LOG_D(NR_MAC, "retxBSR timer expired\n");
    for (int i = 0; i < mac->lc_ordered_list.count; i++) {
      nr_lcordered_info_t *lc_info = mac->lc_ordered_list.array[i];
      NR_LogicalChannelIdentity_t lcid = lc_info->lcid;
      // if at least one of the logical channels which belong to an LCG contains UL data
      NR_LC_SCHEDULING_INFO *lc_sched_info = get_scheduling_info_from_lcid(mac, lcid);
      if (lc_sched_info->LCGID != NR_INVALID_LCGID && lc_sched_info->LCID_buffer_remain > 0) {
        LOG_D(NR_MAC, "Triggering regular BSR after retxBSR timer expired\n");
        trigger_regular_bsr(mac, lcid, lc_info->sr_DelayTimerApplied);
        break;
      }
    }
  }
  bool periodicBSR_expired = nr_timer_tick(&mac->scheduling_info.periodicBSR_Timer);
  if (periodicBSR_expired) {
    // periodicBSR-Timer expires, trigger BSR
    mac->scheduling_info.BSR_reporting_active |= NR_BSR_TRIGGER_PERIODIC;
    LOG_D(NR_MAC, "[UE %d] MAC BSR Triggered PeriodicBSR Timer expiry\n", mac->ue_id);
  }
  for (int i = 0; i < NR_MAX_NUM_LCID; i++)
    AssertFatal(!nr_timer_tick(&mac->scheduling_info.lc_sched_info[i].Bj_timer),
                "Bj timer for LCID %d expired! That should never happen\n",
                i);

  nr_phr_info_t *phr_info = &mac->scheduling_info.phr_info;
  if (phr_info->is_configured) {
    bool prohibit_expired = nr_timer_tick(&phr_info->prohibitPHR_Timer);
    if (prohibit_expired) {
      int16_t pathloss = compute_nr_SSB_PL(mac, mac->ssb_measurements.ssb_rsrp_dBm);
      if (abs(pathloss - phr_info->PathlossLastValue) > phr_info->PathlossChange_db) {
        phr_info->phr_reporting |= (1 << phr_cause_prohibit_timer);
      }
    }
    bool periodic_expired = nr_timer_tick(&phr_info->periodicPHR_Timer);
    if (periodic_expired) {
      phr_info->phr_reporting |= (1 << phr_cause_periodic_timer);
    }
  }
}

void remove_ul_config_last_item(fapi_nr_ul_config_request_pdu_t *pdu)
{
  pdu->privateNBpdus--;
}

void release_ul_config(fapi_nr_ul_config_request_pdu_t *configPerSlot, bool clearIt)
{
  if (clearIt)
    *configPerSlot->privateNBpdus = 0;
  verifyMutex(pthread_mutex_unlock(configPerSlot->lock));
}

fapi_nr_ul_config_request_pdu_t *fapiLockIterator(fapi_nr_ul_config_request_t *ul_config, frame_t frame_tx, int slot_tx)
{
  verifyMutex(pthread_mutex_lock(&ul_config->mutex_ul_config));
  if (ul_config->number_pdus >= FAPI_NR_UL_CONFIG_LIST_NUM) {
    LOG_E(NR_MAC, "Error in ul config in slot %d no memory\n", ul_config->slot);
    verifyMutex(pthread_mutex_unlock(&ul_config->mutex_ul_config));
    return NULL;
  }
  if (ul_config->number_pdus != 0 && (ul_config->frame != frame_tx || ul_config->slot != slot_tx)) {
    LOG_E(NR_MAC, "Error in ul config consistency, clearing it slot %d\n", slot_tx);
    ul_config->number_pdus = 0;
    verifyMutex(pthread_mutex_unlock(&ul_config->mutex_ul_config));
    return NULL;
  }
  if (ul_config->number_pdus >= FAPI_NR_UL_CONFIG_LIST_NUM) {
    LOG_E(NR_MAC, "Error in ul config for slot %d, no memory\n", slot_tx);
    verifyMutex(pthread_mutex_unlock(&ul_config->mutex_ul_config));
    return NULL;
  }
  fapi_nr_ul_config_request_pdu_t *pdu = ul_config->ul_config_list + ul_config->number_pdus;
  pdu->pdu_type = FAPI_NR_END;
  pdu->lock = &ul_config->mutex_ul_config;
  pdu->privateNBpdus = &ul_config->number_pdus;
  return ul_config->ul_config_list;
}

fapi_nr_ul_config_request_pdu_t *lockGet_ul_iterator(NR_UE_MAC_INST_t *mac, frame_t frame_tx, int slot_tx)
{
  NR_TDD_UL_DL_ConfigCommon_t *tdd_config = mac->tdd_UL_DL_ConfigurationCommon;
  //Check if requested on the right slot
  if (!is_nr_UL_slot(tdd_config, slot_tx, mac->frame_type)) {
    LOG_E(NR_MAC, "UL lock iterator called for slot %d which is not UL\n", slot_tx);
    return NULL;
  }
  AssertFatal(mac->ul_config_request != NULL, "mac->ul_config_request not initialized, logic bug\n");
  return fapiLockIterator(mac->ul_config_request + slot_tx, frame_tx, slot_tx);
}

/*
 * This function returns the DL config corresponding to a given DL slot
 * from MAC instance .
 */
fapi_nr_dl_config_request_t *get_dl_config_request(NR_UE_MAC_INST_t *mac, int slot)
{
  AssertFatal(mac->dl_config_request != NULL, "mac->dl_config_request not initialized, logic bug\n");
  return &mac->dl_config_request[slot];
}

void ul_layers_config(NR_UE_MAC_INST_t *mac, nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu, dci_pdu_rel15_t *dci, nr_dci_format_t dci_format)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_SRS_Config_t *srs_config = current_UL_BWP->srs_Config;
  NR_PUSCH_Config_t *pusch_Config = current_UL_BWP->pusch_Config;

  long transformPrecoder = pusch_config_pdu->transform_precoding;

  /* PRECOD_NBR_LAYERS */
  // 0 bits if the higher layer parameter txConfig = nonCodeBook

  if (*pusch_Config->txConfig == NR_PUSCH_Config__txConfig_codebook){

    // The UE shall transmit PUSCH using the same antenna port(s) as the SRS port(s) in the SRS resource indicated by the DCI format 0_1
    // 38.214  Section 6.1.1

    uint8_t n_antenna_port = get_pusch_nb_antenna_ports(pusch_Config, srs_config, dci->srs_resource_indicator);

    // 1 antenna port and the higher layer parameter txConfig = codebook 0 bits

    if (n_antenna_port == 4) { // 4 antenna port and the higher layer parameter txConfig = codebook

      // Table 7.3.1.1.2-2: transformPrecoder=disabled and maxRank = 2 or 3 or 4
      if ((transformPrecoder == NR_PUSCH_Config__transformPrecoder_disabled)
          && ((*pusch_Config->maxRank == 2) || (*pusch_Config->maxRank == 3) || (*pusch_Config->maxRank == 4))) {
        if (*pusch_Config->codebookSubset == NR_PUSCH_Config__codebookSubset_fullyAndPartialAndNonCoherent) {
          pusch_config_pdu->nrOfLayers = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][0];
          pusch_config_pdu->Tpmi = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][1];
        }

        if (*pusch_Config->codebookSubset == NR_PUSCH_Config__codebookSubset_partialAndNonCoherent){
          pusch_config_pdu->nrOfLayers = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][2];
          pusch_config_pdu->Tpmi = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][3];
        }

        if (*pusch_Config->codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent){
          pusch_config_pdu->nrOfLayers = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][4];
          pusch_config_pdu->Tpmi = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][5];
        }
      }

      // Table 7.3.1.1.2-3: transformPrecoder= enabled, or transformPrecoder=disabled and maxRank = 1
      if (((transformPrecoder == NR_PUSCH_Config__transformPrecoder_enabled)
           || (transformPrecoder == NR_PUSCH_Config__transformPrecoder_disabled))
          && (*pusch_Config->maxRank == 1)) {
        if (*pusch_Config->codebookSubset == NR_PUSCH_Config__codebookSubset_fullyAndPartialAndNonCoherent) {
          pusch_config_pdu->nrOfLayers = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][6];
          pusch_config_pdu->Tpmi = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][7];
        }

        if (*pusch_Config->codebookSubset == NR_PUSCH_Config__codebookSubset_partialAndNonCoherent){
          pusch_config_pdu->nrOfLayers = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][8];
          pusch_config_pdu->Tpmi = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][9];
        }

        if (*pusch_Config->codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent){
          pusch_config_pdu->nrOfLayers = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][10];
          pusch_config_pdu->Tpmi = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][11];
        }
      }
    }

    if (n_antenna_port == 2) {
      // 2 antenna port and the higher layer parameter txConfig = codebook
      // Table 7.3.1.1.2-4: transformPrecoder=disabled and maxRank = 2
      if ((transformPrecoder == NR_PUSCH_Config__transformPrecoder_disabled) && (*pusch_Config->maxRank == 2)) {
        if (*pusch_Config->codebookSubset == NR_PUSCH_Config__codebookSubset_fullyAndPartialAndNonCoherent) {
          pusch_config_pdu->nrOfLayers = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][12];
          pusch_config_pdu->Tpmi = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][13];
        }

        if (*pusch_Config->codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent){
          pusch_config_pdu->nrOfLayers = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][14];
          pusch_config_pdu->Tpmi = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][15];
        }

      }

      // Table 7.3.1.1.2-5: transformPrecoder= enabled, or transformPrecoder= disabled and maxRank = 1
      if (((transformPrecoder == NR_PUSCH_Config__transformPrecoder_enabled)
           || (transformPrecoder == NR_PUSCH_Config__transformPrecoder_disabled))
          && (*pusch_Config->maxRank == 1)) {
        if (*pusch_Config->codebookSubset == NR_PUSCH_Config__codebookSubset_fullyAndPartialAndNonCoherent) {
          pusch_config_pdu->nrOfLayers = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][16];
          pusch_config_pdu->Tpmi = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][17];
        }

        if (*pusch_Config->codebookSubset == NR_PUSCH_Config__codebookSubset_nonCoherent){
          pusch_config_pdu->nrOfLayers = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][18];
          pusch_config_pdu->Tpmi = table_7_3_1_1_2_2_3_4_5[dci->precoding_information.val][19];
        }
      }
    }
  }
}

// todo: this function shall be reviewed completely because of the many comments left by the author
void ul_ports_config(NR_UE_MAC_INST_t *mac, int *n_front_load_symb, nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu, dci_pdu_rel15_t *dci, nr_dci_format_t dci_format)
{
  uint8_t rank = pusch_config_pdu->nrOfLayers;

  NR_PUSCH_Config_t *pusch_Config = mac->current_UL_BWP->pusch_Config;
  AssertFatal(pusch_Config!=NULL,"pusch_Config shouldn't be null\n");

  long transformPrecoder = pusch_config_pdu->transform_precoding;
  LOG_D(NR_MAC,"transformPrecoder %s\n", transformPrecoder==NR_PUSCH_Config__transformPrecoder_disabled ? "disabled" : "enabled");

  long *max_length = NULL;
  long *dmrs_type = NULL;
  if (pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA) {
    max_length = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA->choice.setup->maxLength;
    dmrs_type = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA->choice.setup->dmrs_Type;
  }
  else {
    max_length = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup->maxLength;
    dmrs_type = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup->dmrs_Type;
  }

  LOG_D(NR_MAC,"MappingType%s max_length %s, dmrs_type %s, antenna_ports %d\n",
        pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA?"A":"B",max_length?"len2":"len1",dmrs_type?"type2":"type1",dci->antenna_ports.val);

  if ((transformPrecoder == NR_PUSCH_Config__transformPrecoder_enabled) &&
      (dmrs_type == NULL) && (max_length == NULL)) { // tables 7.3.1.1.2-6
    pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2;
    pusch_config_pdu->dmrs_ports = 1 << dci->antenna_ports.val;
  }

  if ((transformPrecoder == NR_PUSCH_Config__transformPrecoder_enabled) &&
      (dmrs_type == NULL) && (max_length != NULL)) { // tables 7.3.1.1.2-7

    pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2; //TBC
    pusch_config_pdu->dmrs_ports = 1<<((dci->antenna_ports.val > 3)?(dci->antenna_ports.val-4):(dci->antenna_ports.val));
    *n_front_load_symb = (dci->antenna_ports.val > 3)?2:1;
  }

  if ((transformPrecoder == NR_PUSCH_Config__transformPrecoder_disabled) && (dmrs_type == NULL)
      && (max_length == NULL)) { // tables 7.3.1.1.2-8/9/10/11

    if (rank == 1) {
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = (dci->antenna_ports.val > 1)?2:1;
      pusch_config_pdu->dmrs_ports =1<<((dci->antenna_ports.val > 1)?(dci->antenna_ports.val-2):(dci->antenna_ports.val));
    }

    if (rank == 2){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = (dci->antenna_ports.val > 0)?2:1;
      pusch_config_pdu->dmrs_ports = (dci->antenna_ports.val > 1)?((dci->antenna_ports.val> 2)?0x5:0xc):0x3;
    }

    if (rank == 3){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2;
      pusch_config_pdu->dmrs_ports = 0x7;  // ports 0-2
    }

    if (rank == 4){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2;
      pusch_config_pdu->dmrs_ports = 0xf;  // ports 0-3
    }
  }

  if ((transformPrecoder == NR_PUSCH_Config__transformPrecoder_disabled) && (dmrs_type == NULL)
      && (max_length != NULL)) { // tables 7.3.1.1.2-12/13/14/15

    if (rank == 1){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = (dci->antenna_ports.val > 1)?2:1; //TBC
      pusch_config_pdu->dmrs_ports = 1<<((dci->antenna_ports.val > 1)?(dci->antenna_ports.val > 5 ?(dci->antenna_ports.val-6):(dci->antenna_ports.val-2)):dci->antenna_ports.val);
      *n_front_load_symb = (dci->antenna_ports.val > 6)?2:1;
    }

    if (rank == 2){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = (dci->antenna_ports.val > 0)?2:1; //TBC
      pusch_config_pdu->dmrs_ports = 0; //FIXME
      //pusch_config_pdu->dmrs_ports[0] = table_7_3_1_1_2_13[dci->antenna_ports.val][1];
      //pusch_config_pdu->dmrs_ports[1] = table_7_3_1_1_2_13[dci->antenna_ports.val][2];
      //n_front_load_symb = (dci->antenna_ports.val > 3)?2:1; // FIXME
    }

    if (rank == 3){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2; //TBC
      pusch_config_pdu->dmrs_ports = 0; //FIXME
      //pusch_config_pdu->dmrs_ports[0] = table_7_3_1_1_2_14[dci->antenna_ports.val][1];
      //pusch_config_pdu->dmrs_ports[1] = table_7_3_1_1_2_14[dci->antenna_ports.val][2];
      //pusch_config_pdu->dmrs_ports[2] = table_7_3_1_1_2_14[dci->antenna_ports.val][3];
      //n_front_load_symb = (dci->antenna_ports.val > 1)?2:1; //FIXME
    }

    if (rank == 4){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2; //TBC
      pusch_config_pdu->dmrs_ports = 0; //FIXME
      //pusch_config_pdu->dmrs_ports[0] = table_7_3_1_1_2_15[dci->antenna_ports.val][1];
      //pusch_config_pdu->dmrs_ports[1] = table_7_3_1_1_2_15[dci->antenna_ports.val][2];
      //pusch_config_pdu->dmrs_ports[2] = table_7_3_1_1_2_15[dci->antenna_ports.val][3];
      //pusch_config_pdu->dmrs_ports[3] = table_7_3_1_1_2_15[dci->antenna_ports.val][4];
      //n_front_load_symb = (dci->antenna_ports.val > 1)?2:1; //FIXME
    }
  }

  if ((transformPrecoder == NR_PUSCH_Config__transformPrecoder_disabled) && (dmrs_type != NULL)
      && (max_length == NULL)) { // tables 7.3.1.1.2-16/17/18/19

    if (rank == 1){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = (dci->antenna_ports.val > 1)?((dci->antenna_ports.val > 5)?3:2):1; //TBC
      pusch_config_pdu->dmrs_ports = (dci->antenna_ports.val > 1)?(dci->antenna_ports.val > 5 ?(dci->antenna_ports.val-6):(dci->antenna_ports.val-2)):dci->antenna_ports.val; //TBC
    }

    if (rank == 2){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = (dci->antenna_ports.val > 0)?((dci->antenna_ports.val > 2)?3:2):1; //TBC
      pusch_config_pdu->dmrs_ports = 0; //FIXME
      //pusch_config_pdu->dmrs_ports[0] = table_7_3_1_1_2_17[dci->antenna_ports.val][1];
      //pusch_config_pdu->dmrs_ports[1] = table_7_3_1_1_2_17[dci->antenna_ports.val][2];
    }

    if (rank == 3){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = (dci->antenna_ports.val > 0)?3:2; //TBC
      pusch_config_pdu->dmrs_ports = 0; //FIXME
      //pusch_config_pdu->dmrs_ports[0] = table_7_3_1_1_2_18[dci->antenna_ports.val][1];
      //pusch_config_pdu->dmrs_ports[1] = table_7_3_1_1_2_18[dci->antenna_ports.val][2];
      //pusch_config_pdu->dmrs_ports[2] = table_7_3_1_1_2_18[dci->antenna_ports.val][3];
    }

    if (rank == 4){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = dci->antenna_ports.val + 2; //TBC
      pusch_config_pdu->dmrs_ports = 0; //FIXME
      //pusch_config_pdu->dmrs_ports[0] = 0;
      //pusch_config_pdu->dmrs_ports[1] = 1;
      //pusch_config_pdu->dmrs_ports[2] = 2;
      //pusch_config_pdu->dmrs_ports[3] = 3;
    }
  }

  if ((transformPrecoder == NR_PUSCH_Config__transformPrecoder_disabled) && (dmrs_type != NULL)
      && (max_length != NULL)) { // tables 7.3.1.1.2-20/21/22/23

    if (rank == 1){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = table_7_3_1_1_2_20[dci->antenna_ports.val][0]; //TBC
      pusch_config_pdu->dmrs_ports = table_7_3_1_1_2_20[dci->antenna_ports.val][1]; //TBC
      //n_front_load_symb = table_7_3_1_1_2_20[dci->antenna_ports.val][2]; //FIXME
    }

    if (rank == 2){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = table_7_3_1_1_2_21[dci->antenna_ports.val][0]; //TBC
      pusch_config_pdu->dmrs_ports = 0; //FIXME
      //pusch_config_pdu->dmrs_ports[0] = table_7_3_1_1_2_21[dci->antenna_ports.val][1];
      //pusch_config_pdu->dmrs_ports[1] = table_7_3_1_1_2_21[dci->antenna_ports.val][2];
      //n_front_load_symb = table_7_3_1_1_2_21[dci->antenna_ports.val][3]; //FIXME
    }

    if (rank == 3){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = table_7_3_1_1_2_22[dci->antenna_ports.val][0]; //TBC
      pusch_config_pdu->dmrs_ports = 0; //FIXME
      //pusch_config_pdu->dmrs_ports[0] = table_7_3_1_1_2_22[dci->antenna_ports.val][1];
      //pusch_config_pdu->dmrs_ports[1] = table_7_3_1_1_2_22[dci->antenna_ports.val][2];
      //pusch_config_pdu->dmrs_ports[2] = table_7_3_1_1_2_22[dci->antenna_ports.val][3];
      //n_front_load_symb = table_7_3_1_1_2_22[dci->antenna_ports.val][4]; //FIXME
    }

    if (rank == 4){
      pusch_config_pdu->num_dmrs_cdm_grps_no_data = table_7_3_1_1_2_23[dci->antenna_ports.val][0]; //TBC
      pusch_config_pdu->dmrs_ports = 0; //FIXME
      //pusch_config_pdu->dmrs_ports[0] = table_7_3_1_1_2_23[dci->antenna_ports.val][1];
      //pusch_config_pdu->dmrs_ports[1] = table_7_3_1_1_2_23[dci->antenna_ports.val][2];
      //pusch_config_pdu->dmrs_ports[2] = table_7_3_1_1_2_23[dci->antenna_ports.val][3];
      //pusch_config_pdu->dmrs_ports[3] = table_7_3_1_1_2_23[dci->antenna_ports.val][4];
      //n_front_load_symb = table_7_3_1_1_2_23[dci->antenna_ports.val][5]; //FIXME
    }
  }
  LOG_D(NR_MAC,"num_dmrs_cdm_grps_no_data %d, dmrs_ports %d\n",pusch_config_pdu->num_dmrs_cdm_grps_no_data,pusch_config_pdu->dmrs_ports);
}

// Configuration of Msg3 PDU according to clauses:
// - 8.3 of 3GPP TS 38.213 version 16.3.0 Release 16
// - 6.1.2.2 of TS 38.214
// - 6.1.3 of TS 38.214
// - 6.2.2 of TS 38.214
// - 6.1.4.2 of TS 38.214
// - 6.4.1.1.1 of TS 38.211
// - 6.3.1.7 of 38.211
int nr_config_pusch_pdu(NR_UE_MAC_INST_t *mac,
                        NR_tda_info_t *tda_info,
                        nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu,
                        dci_pdu_rel15_t *dci,
                        csi_payload_t *csi_report,
                        RAR_grant_t *rar_grant,
                        rnti_t rnti,
                        int ss_type,
                        const nr_dci_format_t dci_format)
{
  uint16_t l_prime_mask = 0;
  int N_PRB_oh  = 0;

  int rnti_type = get_rnti_type(mac, rnti);
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_UE_ServingCell_Info_t *sc_info = &mac->sc_info;
  int scs = current_UL_BWP->scs;

  // Common configuration
  pusch_config_pdu->dmrs_config_type = pusch_dmrs_type1;
  pusch_config_pdu->pdu_bit_map = PUSCH_PDU_BITMAP_PUSCH_DATA;
  pusch_config_pdu->nrOfLayers = 1;
  pusch_config_pdu->Tpmi = 0;
  pusch_config_pdu->rnti = rnti;

  pusch_dmrs_AdditionalPosition_t add_pos = pusch_dmrs_pos2;
  int dmrslength = 1;
  NR_PUSCH_Config_t *pusch_Config = current_UL_BWP->pusch_Config;

  AssertFatal(!((mac->ra.ra_state < nrRA_SUCCEEDED && mac->ra.ra_type == RA_2_STEP) && rar_grant), "logic error: Is not possible to have both msgA_pusch_resource and rar_grant\n");

  if (mac->ra.ra_state < nrRA_SUCCEEDED && mac->ra.ra_type == RA_2_STEP) {
    NR_MsgA_PUSCH_Resource_r16_t *msgA_PUSCH_Resource =
        current_UL_BWP->msgA_ConfigCommon_r16->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16;
    int S = 0;
    int L = 0;
    SLIV2SL(*msgA_PUSCH_Resource->startSymbolAndLengthMsgA_PO_r16, &S, &L);
    tda_info->k2 = msgA_PUSCH_Resource->msgA_PUSCH_TimeDomainOffset_r16;
    tda_info->startSymbolIndex = S;
    tda_info->nrOfSymbols = L;
    l_prime_mask = get_l_prime(tda_info->nrOfSymbols,
                               tda_info->mapping_type,
                               add_pos,
                               dmrslength,
                               tda_info->startSymbolIndex,
                               mac->dmrs_TypeA_Position);
    LOG_I(NR_MAC,
          "MSGA PUSCH start_sym:%d NR Symb:%d mappingtype:%d, DMRS_MASK:%x\n",
          pusch_config_pdu->start_symbol_index,
          pusch_config_pdu->nr_of_symbols,
          tda_info->mapping_type,
          l_prime_mask);

    LOG_D(NR_MAC,
          "sc_info->initial_ul_BWPStart = %d  sc_info->initial_ul_BWPSize = %d\n",
          sc_info->initial_ul_BWPStart,
          sc_info->initial_ul_BWPSize);
    pusch_config_pdu->handle = 0;
    pusch_config_pdu->rb_size = msgA_PUSCH_Resource->nrofPRBs_PerMsgA_PO_r16;
    pusch_config_pdu->mcs_table = 0;
    pusch_config_pdu->frequency_hopping = msgA_PUSCH_Resource->msgA_IntraSlotFrequencyHopping_r16 ? *msgA_PUSCH_Resource->msgA_IntraSlotFrequencyHopping_r16 : 0;
    pusch_config_pdu->dmrs_ports = 1; // is in SIB1 nrofDMRS_Sequences_r16?
    pusch_config_pdu->pusch_data.new_data_indicator = 1; // new data
    pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2;
    pusch_config_pdu->ul_dmrs_symb_pos = get_l_prime(3, 0, pusch_dmrs_pos2, pusch_len1, 10, mac->dmrs_TypeA_Position);
    pusch_config_pdu->transform_precoding =
           *mac->current_UL_BWP->msgA_ConfigCommon_r16->msgA_PUSCH_Config_r16->msgA_TransformPrecoder_r16;
    pusch_config_pdu->rb_bitmap[0] = 0;
    pusch_config_pdu->rb_start = msgA_PUSCH_Resource->frequencyStartMsgA_PUSCH_r16; // rb_start depends on the RO
    pusch_config_pdu->bwp_size = sc_info->initial_ul_BWPSize;
    pusch_config_pdu->bwp_start = sc_info->initial_ul_BWPStart;
    pusch_config_pdu->subcarrier_spacing = scs;
    pusch_config_pdu->cyclic_prefix = 0;
    pusch_config_pdu->uplink_frequency_shift_7p5khz = 0;
    pusch_config_pdu->vrb_to_prb_mapping = 0;
    pusch_config_pdu->dmrs_config_type = 0;
    pusch_config_pdu->data_scrambling_id = mac->physCellId;
    pusch_config_pdu->ul_dmrs_scrambling_id = mac->physCellId;
    pusch_config_pdu->scid = 0;
    pusch_config_pdu->resource_alloc = 1;
    pusch_config_pdu->tx_direct_current_location = 0;
    pusch_config_pdu->mcs_index = msgA_PUSCH_Resource->msgA_MCS_r16;
    pusch_config_pdu->qam_mod_order = nr_get_Qm_dl(pusch_config_pdu->mcs_index, 0);
    pusch_config_pdu->start_symbol_index = S;
    pusch_config_pdu->nr_of_symbols = L;
    pusch_config_pdu->pusch_data.rv_index = 0; // 8.3 in 38.213
    pusch_config_pdu->pusch_data.harq_process_id = 0;
    pusch_config_pdu->pusch_data.num_cb = 0;
    pusch_config_pdu->tbslbrm = 0;
    pusch_config_pdu->target_code_rate = nr_get_code_rate_ul(pusch_config_pdu->mcs_index, 0);

  } else if (rar_grant) {
    // Note: for Msg3 or MsgA PUSCH transmission the N_PRB_oh is always set to 0
    int ibwp_start = sc_info->initial_ul_BWPStart;
    int ibwp_size = sc_info->initial_ul_BWPSize;
    int abwp_start = current_UL_BWP->BWPStart;
    int abwp_size = current_UL_BWP->BWPSize;
    int scs = current_UL_BWP->scs;

    // BWP start selection according to 8.3 of TS 38.213
    if ((ibwp_start < abwp_start) || (ibwp_size > abwp_size)) {
      pusch_config_pdu->bwp_start = abwp_start;
      pusch_config_pdu->bwp_size = abwp_size;
    } else {
      pusch_config_pdu->bwp_start = ibwp_start;
      pusch_config_pdu->bwp_size = ibwp_size;
    }

    //// Resource assignment from RAR
    // Frequency domain allocation according to 8.3 of TS 38.213
    int mask;
    if (ibwp_size < 180)
      mask = (1 << ((int) ceil(log2((ibwp_size*(ibwp_size+1))>>1)))) - 1;
    else
      mask = (1 << (28 - (int)(ceil(log2((ibwp_size*(ibwp_size+1))>>1))))) - 1;

    dci_field_t f_alloc;
    f_alloc.val = rar_grant->Msg3_f_alloc & mask;
    if (nr_ue_process_dci_freq_dom_resource_assignment(pusch_config_pdu,
                                                       NULL,
                                                       NULL,
                                                       ibwp_size,
                                                       0,
                                                       0,
                                                       f_alloc) < 0) {
      LOG_E(NR_MAC, "can't nr_ue_process_dci_freq_dom_resource_assignment()\n");
      return -1;
    }

    // virtual resource block to physical resource mapping for Msg3 PUSCH (6.3.1.7 in 38.211)
    //pusch_config_pdu->rb_start += ibwp_start - abwp_start;

    // Time domain allocation
    pusch_config_pdu->start_symbol_index = tda_info->startSymbolIndex;
    pusch_config_pdu->nr_of_symbols = tda_info->nrOfSymbols;

    l_prime_mask = get_l_prime(tda_info->nrOfSymbols,
                               tda_info->mapping_type,
                               add_pos,
                               dmrslength,
                               tda_info->startSymbolIndex,
                               mac->dmrs_TypeA_Position);
    LOG_D(NR_MAC, "MSG3 start_sym:%d NR Symb:%d mappingtype:%d, DMRS_MASK:%x\n", pusch_config_pdu->start_symbol_index, pusch_config_pdu->nr_of_symbols, tda_info->mapping_type, l_prime_mask);

#ifdef DEBUG_MSG3
    LOG_D(NR_MAC, "In %s BWP assignment (BWP (start %d, size %d) \n", __FUNCTION__, pusch_config_pdu->bwp_start, pusch_config_pdu->bwp_size);
#endif

    // MCS
    pusch_config_pdu->mcs_index = rar_grant->mcs;
    // Frequency hopping
    pusch_config_pdu->frequency_hopping = rar_grant->freq_hopping;

    // DM-RS configuration according to 6.2.2 UE DM-RS transmission procedure in 38.214
    pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2;
    pusch_config_pdu->dmrs_ports = 1;

    // DMRS sequence initialization [TS 38.211, sec 6.4.1.1.1].
    // Should match what is sent in DCI 0_1, otherwise set to 0.
    pusch_config_pdu->scid = 0;

    // Transform precoding according to 6.1.3 UE procedure for applying transform precoding on PUSCH in 38.214
    pusch_config_pdu->transform_precoding = get_transformPrecoding(current_UL_BWP, NR_UL_DCI_FORMAT_0_0, 0); // as if it was DCI 0_0

    // Resource allocation in frequency domain according to 6.1.2.2 in TS 38.214
    pusch_config_pdu->resource_alloc = 1;

    //// Completing PUSCH PDU
    pusch_config_pdu->mcs_table = 0;
    pusch_config_pdu->cyclic_prefix = 0;
    pusch_config_pdu->data_scrambling_id = mac->physCellId;
    pusch_config_pdu->ul_dmrs_scrambling_id = mac->physCellId;
    pusch_config_pdu->subcarrier_spacing = scs;
    pusch_config_pdu->vrb_to_prb_mapping = 0;
    pusch_config_pdu->uplink_frequency_shift_7p5khz = 0;
    //Optional Data only included if indicated in pduBitmap
    pusch_config_pdu->pusch_data.rv_index = 0;  // 8.3 in 38.213
    pusch_config_pdu->pusch_data.harq_process_id = 0;
    pusch_config_pdu->pusch_data.new_data_indicator = true; // new data
    pusch_config_pdu->pusch_data.num_cb = 0;
    pusch_config_pdu->tbslbrm = 0;

  } else if (dci) {
    pusch_config_pdu->ulsch_indicator = dci->ulsch_indicator;
    if (dci->csi_request.nbits > 0 && dci->csi_request.val > 0) {
      AssertFatal(csi_report, "CSI report needs to be present in case of CSI request\n");
      pusch_config_pdu->pusch_uci.csi_part1_bit_length = csi_report->p1_bits;
      pusch_config_pdu->pusch_uci.csi_part1_payload = csi_report->part1_payload;
      pusch_config_pdu->pusch_uci.csi_part2_bit_length = csi_report->p2_bits;
      pusch_config_pdu->pusch_uci.csi_part2_payload = csi_report->part2_payload;
      AssertFatal(pusch_Config && pusch_Config->uci_OnPUSCH, "UCI on PUSCH need to be configured\n");
      NR_UCI_OnPUSCH_t *onPusch = pusch_Config->uci_OnPUSCH->choice.setup;
      AssertFatal(onPusch &&
                  onPusch->betaOffsets &&
                  onPusch->betaOffsets->present == NR_UCI_OnPUSCH__betaOffsets_PR_semiStatic,
                  "Only semistatic beta offset is supported\n");
      NR_BetaOffsets_t *beta_offsets = onPusch->betaOffsets->choice.semiStatic;

      pusch_config_pdu->pusch_uci.beta_offset_csi1 = pusch_config_pdu->pusch_uci.csi_part1_bit_length < 12 ?
                                                     *beta_offsets->betaOffsetCSI_Part1_Index1 :
                                                     *beta_offsets->betaOffsetCSI_Part1_Index2;
      pusch_config_pdu->pusch_uci.beta_offset_csi2 = pusch_config_pdu->pusch_uci.csi_part2_bit_length < 12 ?
                                                     *beta_offsets->betaOffsetCSI_Part2_Index1 :
                                                     *beta_offsets->betaOffsetCSI_Part2_Index2;
      pusch_config_pdu->pusch_uci.alpha_scaling = onPusch->scaling;
    }
    else {
      pusch_config_pdu->pusch_uci.csi_part1_bit_length = 0;
      pusch_config_pdu->pusch_uci.csi_part2_bit_length = 0;
    }

    pusch_config_pdu->pusch_uci.harq_ack_bit_length = 0;

    if (dci_format == NR_UL_DCI_FORMAT_0_0 && ss_type == NR_SearchSpace__searchSpaceType_PR_common) {
      pusch_config_pdu->bwp_start = sc_info->initial_ul_BWPStart;
      pusch_config_pdu->bwp_size = sc_info->initial_ul_BWPSize;
    } else {
      pusch_config_pdu->bwp_start = current_UL_BWP->BWPStart;
      pusch_config_pdu->bwp_size = current_UL_BWP->BWPSize;
    }

    pusch_config_pdu->start_symbol_index = tda_info->startSymbolIndex;
    pusch_config_pdu->nr_of_symbols = tda_info->nrOfSymbols;

    /* Transform precoding */
    pusch_config_pdu->transform_precoding = get_transformPrecoding(current_UL_BWP, dci_format, 0);
    bool tp_enabled = pusch_config_pdu->transform_precoding == NR_PUSCH_Config__transformPrecoder_enabled;

    /*DCI format-related configuration*/
    if (dci_format == NR_UL_DCI_FORMAT_0_0) {
      if (!tp_enabled && pusch_config_pdu->nr_of_symbols < 3)
        pusch_config_pdu->num_dmrs_cdm_grps_no_data = 1;
      else
        pusch_config_pdu->num_dmrs_cdm_grps_no_data = 2;
    } else if (dci_format == NR_UL_DCI_FORMAT_0_1) {
      ul_layers_config(mac, pusch_config_pdu, dci, dci_format);
      ul_ports_config(mac, &dmrslength, pusch_config_pdu, dci, dci_format);
    } else {
      LOG_E(NR_MAC, "UL grant from DCI format %d is not handled...\n", dci_format);
      return -1;
    }

    if (pusch_config_pdu->nrOfLayers < 1) {
      LOG_E(NR_MAC, "Invalid UL number of layers %d from DCI\n", pusch_config_pdu->nrOfLayers);
      return -1;
    }

    int mappingtype = tda_info->mapping_type;

    NR_DMRS_UplinkConfig_t *NR_DMRS_ulconfig = NULL;
    if(pusch_Config) {
      NR_DMRS_ulconfig = (mappingtype == NR_PUSCH_TimeDomainResourceAllocation__mappingType_typeA)
                             ? pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA->choice.setup
                             : pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup;
    }

    pusch_config_pdu->scid = 0;
    pusch_config_pdu->data_scrambling_id = mac->physCellId;
    if (pusch_Config
        && pusch_Config->dataScramblingIdentityPUSCH
        && rnti_type == TYPE_C_RNTI_
        && !(dci_format == NR_UL_DCI_FORMAT_0_0 && ss_type == NR_SearchSpace__searchSpaceType_PR_common))
      pusch_config_pdu->data_scrambling_id = *pusch_Config->dataScramblingIdentityPUSCH;

    pusch_config_pdu->ul_dmrs_scrambling_id = mac->physCellId;
    if (dci_format == NR_UL_DCI_FORMAT_0_1)
      pusch_config_pdu->scid = dci->dmrs_sequence_initialization.val;

    /* TRANSFORM PRECODING ------------------------------------------------------------------------------------------*/
    if (tp_enabled) {

      uint32_t n_RS_Id = 0;
      if (NR_DMRS_ulconfig->transformPrecodingEnabled &&
          NR_DMRS_ulconfig->transformPrecodingEnabled->nPUSCH_Identity)
        n_RS_Id = *NR_DMRS_ulconfig->transformPrecodingEnabled->nPUSCH_Identity;
      else
        n_RS_Id = mac->physCellId;

      // U as specified in section 6.4.1.1.1.2 in 38.211, if sequence hopping and group hopping are disabled
      pusch_config_pdu->dfts_ofdm.low_papr_group_number = n_RS_Id % 30;

      // V as specified in section 6.4.1.1.1.2 in 38.211 V = 0 if sequence hopping and group hopping are disabled
      if (!NR_DMRS_ulconfig || !NR_DMRS_ulconfig->transformPrecodingEnabled ||
          (!NR_DMRS_ulconfig->transformPrecodingEnabled->sequenceGroupHopping && !NR_DMRS_ulconfig->transformPrecodingEnabled->sequenceHopping))
        pusch_config_pdu->dfts_ofdm.low_papr_sequence_number = 0;
      else
        AssertFatal(1==0,"SequenceGroupHopping or sequenceHopping are NOT Supported\n");

      LOG_D(NR_MAC,
            "TRANSFORM PRECODING IS ENABLED. CDM groups: %d, U: %d \n",
            pusch_config_pdu->num_dmrs_cdm_grps_no_data,
            pusch_config_pdu->dfts_ofdm.low_papr_group_number);
    }
    else {
      if (pusch_config_pdu->scid == 0 && NR_DMRS_ulconfig && NR_DMRS_ulconfig->transformPrecodingDisabled &&
          NR_DMRS_ulconfig->transformPrecodingDisabled->scramblingID0)
        pusch_config_pdu->ul_dmrs_scrambling_id = *NR_DMRS_ulconfig->transformPrecodingDisabled->scramblingID0;
      if (pusch_config_pdu->scid == 1 && NR_DMRS_ulconfig && NR_DMRS_ulconfig->transformPrecodingDisabled
          && NR_DMRS_ulconfig->transformPrecodingDisabled->scramblingID1)
        pusch_config_pdu->ul_dmrs_scrambling_id = *NR_DMRS_ulconfig->transformPrecodingDisabled->scramblingID1;
    }

    /* TRANSFORM PRECODING --------------------------------------------------------------------------------------------------------*/

    /* IDENTIFIER_DCI_FORMATS */
    /* FREQ_DOM_RESOURCE_ASSIGNMENT_UL */
    if (nr_ue_process_dci_freq_dom_resource_assignment(pusch_config_pdu,
                                                       NULL,
                                                       NULL,
                                                       pusch_config_pdu->bwp_size,
                                                       0,
                                                       0,
                                                       dci->frequency_domain_assignment) < 0) {
      LOG_E(NR_MAC, "can't nr_ue_process_dci_freq_dom_resource_assignment()\n");
      return -1;
    }

    /* FREQ_HOPPING_FLAG */
    if (pusch_Config
        && pusch_Config->frequencyHopping
        && (pusch_Config->resourceAllocation != NR_PUSCH_Config__resourceAllocation_resourceAllocationType0)) {
      pusch_config_pdu->frequency_hopping = dci->frequency_hopping_flag.val;
    }

    /* MCS */
    pusch_config_pdu->mcs_index = dci->mcs;

    /* MCS TABLE */
    long *mcs_table_config = pusch_Config ? (tp_enabled ? pusch_Config->mcs_TableTransformPrecoder : pusch_Config->mcs_Table) : NULL;
    pusch_config_pdu->mcs_table = get_pusch_mcs_table(mcs_table_config, tp_enabled, dci_format, rnti_type, ss_type, false);

    /* NDI */
    NR_UL_HARQ_INFO_t *harq = &mac->ul_harq_info[dci->harq_pid.val];
    pusch_config_pdu->pusch_data.new_data_indicator = false;
    if (dci->ndi != harq->last_ndi) {
      pusch_config_pdu->pusch_data.new_data_indicator = true;
      // if new data reset harq structure
      memset(harq, 0, sizeof(*harq));
    }
    harq->last_ndi = dci->ndi;
    /* RV */
    pusch_config_pdu->pusch_data.rv_index = dci->rv;
    /* HARQ_PROCESS_NUMBER */
    pusch_config_pdu->pusch_data.harq_process_id = dci->harq_pid.val;

    if (NR_DMRS_ulconfig != NULL)
      add_pos = (NR_DMRS_ulconfig->dmrs_AdditionalPosition == NULL) ? 2 : *NR_DMRS_ulconfig->dmrs_AdditionalPosition;

    /* DMRS */
    l_prime_mask = get_l_prime(pusch_config_pdu->nr_of_symbols,
                               mappingtype, add_pos, dmrslength,
                               pusch_config_pdu->start_symbol_index,
                               mac->dmrs_TypeA_Position);

    if (sc_info->xOverhead_PUSCH)
      N_PRB_oh = 6 * (1 + *sc_info->xOverhead_PUSCH);
    else
      N_PRB_oh = 0;

    if (sc_info->rateMatching_PUSCH) {
      long maxMIMO_Layers = 0;
      if (sc_info->maxMIMO_Layers_PUSCH)
        maxMIMO_Layers = *sc_info->maxMIMO_Layers_PUSCH;
      else if (pusch_Config && pusch_Config->maxRank)
        maxMIMO_Layers = *pusch_Config->maxRank;
      else {
        if (pusch_Config && pusch_Config->txConfig) {
          if (*pusch_Config->txConfig == NR_PUSCH_Config__txConfig_codebook)
            maxMIMO_Layers = mac->uecap_maxMIMO_PUSCH_layers_cb;
          else
            maxMIMO_Layers = mac->uecap_maxMIMO_PUSCH_layers_nocb;
        } else
          maxMIMO_Layers = 1; // single antenna port
      }
      AssertFatal (maxMIMO_Layers > 0, "Invalid number of max MIMO layers for PUSCH\n");
      pusch_config_pdu->tbslbrm = nr_compute_tbslbrm(pusch_config_pdu->mcs_table, sc_info->ul_bw_tbslbrm, maxMIMO_Layers);
    } else
      pusch_config_pdu->tbslbrm = 0;

    /* PTRS */
    if (pusch_Config && pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB &&
        pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup->phaseTrackingRS) {
      if (!tp_enabled) {
        nfapi_nr_ue_ptrs_ports_t ptrs_ports_list;
        pusch_config_pdu->pusch_ptrs.ptrs_ports_list = &ptrs_ports_list;
        bool valid_ptrs_setup = set_ul_ptrs_values(pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup->phaseTrackingRS->choice.setup,
                                                   pusch_config_pdu->rb_size,
                                                   pusch_config_pdu->mcs_index,
                                                   pusch_config_pdu->mcs_table,
                                                   &pusch_config_pdu->pusch_ptrs.ptrs_freq_density,
                                                   &pusch_config_pdu->pusch_ptrs.ptrs_time_density,
                                                   &pusch_config_pdu->pusch_ptrs.ptrs_ports_list->ptrs_re_offset,
                                                   &pusch_config_pdu->pusch_ptrs.num_ptrs_ports,
                                                   &pusch_config_pdu->pusch_ptrs.ul_ptrs_power,
                                                   pusch_config_pdu->nr_of_symbols);
        if(valid_ptrs_setup == true) {
          pusch_config_pdu->pdu_bit_map |= PUSCH_PDU_BITMAP_PUSCH_PTRS;
        }
        LOG_D(NR_MAC, "UL PTRS values: PTRS time den: %d, PTRS freq den: %d\n",
              pusch_config_pdu->pusch_ptrs.ptrs_time_density, pusch_config_pdu->pusch_ptrs.ptrs_freq_density);
      }
    }
  }

  LOG_D(NR_MAC,
        "Received UL grant (rb_start %d, rb_size %d, start_symbol_index %d, nr_of_symbols %d) for RNTI type %s \n",
        pusch_config_pdu->rb_start,
        pusch_config_pdu->rb_size,
        pusch_config_pdu->start_symbol_index,
        pusch_config_pdu->nr_of_symbols,
        rnti_types(rnti_type));

  pusch_config_pdu->ul_dmrs_symb_pos = l_prime_mask;
  pusch_config_pdu->qam_mod_order = nr_get_Qm_ul(pusch_config_pdu->mcs_index, pusch_config_pdu->mcs_table);
  if (pusch_config_pdu->qam_mod_order == 0) {
    LOG_W(NR_MAC, "Invalid Mod order, likely due to unexpected UL DCI. Ignoring DCI! \n");
    return -1;
  }

  int start_symbol = pusch_config_pdu->start_symbol_index;
  int number_of_symbols = pusch_config_pdu->nr_of_symbols;
  int number_dmrs_symbols = 0;
  for (int i = start_symbol; i < start_symbol + number_of_symbols; i++) {
    if((pusch_config_pdu->ul_dmrs_symb_pos >> i) & 0x01)
      number_dmrs_symbols += 1;
  }

  int nb_dmrs_re_per_rb = ((pusch_config_pdu->dmrs_config_type == pusch_dmrs_type1) ? 6 : 4) * pusch_config_pdu->num_dmrs_cdm_grps_no_data;

  // Compute TBS
  uint16_t R = nr_get_code_rate_ul(pusch_config_pdu->mcs_index, pusch_config_pdu->mcs_table);
  int pid = pusch_config_pdu->pusch_data.harq_process_id;
  if (R > 0) {
    pusch_config_pdu->target_code_rate = R;
    pusch_config_pdu->pusch_data.tb_size = nr_compute_tbs(pusch_config_pdu->qam_mod_order,
                                                          R,
                                                          pusch_config_pdu->rb_size,
                                                          pusch_config_pdu->nr_of_symbols,
                                                          nb_dmrs_re_per_rb * number_dmrs_symbols,
                                                          N_PRB_oh,
                                                          0, // TBR to verify tb scaling
                                                          pusch_config_pdu->nrOfLayers) >> 3;
    mac->ul_harq_info[pid].TBS = pusch_config_pdu->pusch_data.tb_size;
    mac->ul_harq_info[pid].R = R;
  }
  else {
    pusch_config_pdu->target_code_rate = mac->ul_harq_info[pid].R;
    pusch_config_pdu->pusch_data.tb_size = mac->ul_harq_info[pid].TBS;
  }

  /* TPC_PUSCH */
  int delta_pusch = 0;
  if (dci) {
    bool stateless_pusch_power_control = mac->current_UL_BWP->pusch_Config != NULL
                                        && mac->current_UL_BWP->pusch_Config->pusch_PowerControl != NULL
                                        && mac->current_UL_BWP->pusch_Config->pusch_PowerControl->tpc_Accumulation != NULL;
    int table_38_213_7_1_1_1[2][4] = {{-1, 0, 1, 3}, {-4, -1, 1, 4}};
    if (stateless_pusch_power_control) {
      delta_pusch = table_38_213_7_1_1_1[1][dci->tpc];
    } else {
      // TODO: This is not entirely correct. In case there is a prevously scheduled PUSCH for a future slot
      // we should apply its TPC now.
      delta_pusch = table_38_213_7_1_1_1[0][dci->tpc];
    }
  }
  delta_pusch = 0; // set to 0 as a workaround for PHY not applying PUSCH tx power

  bool is_rar_tx_retx = rnti_type == TYPE_TC_RNTI_;

  pusch_config_pdu->tx_power = get_pusch_tx_power_ue(mac,
                                                     pusch_config_pdu->rb_size,
                                                     pusch_config_pdu->rb_start,
                                                     pusch_config_pdu->nr_of_symbols,
                                                     nb_dmrs_re_per_rb * number_dmrs_symbols,
                                                     0, //TODO: count PTRS per RB
                                                     pusch_config_pdu->qam_mod_order,
                                                     pusch_config_pdu->target_code_rate,
                                                     pusch_config_pdu->pusch_uci.beta_offset_csi1,
                                                     pusch_config_pdu->pusch_data.tb_size << 3,
                                                     delta_pusch,
                                                     is_rar_tx_retx,
                                                     pusch_config_pdu->transform_precoding);

  pusch_config_pdu->ldpcBaseGraph = get_BG(pusch_config_pdu->pusch_data.tb_size << 3, pusch_config_pdu->target_code_rate);

  //The MAC entity shall restart retxBSR-Timer upon reception of a grant for transmission of new data on any UL-SCH
  if (pusch_config_pdu->pusch_data.new_data_indicator && dci && dci->ulsch_indicator)
    nr_timer_start(&mac->scheduling_info.retxBSR_Timer);

  if (pusch_config_pdu->pusch_data.tb_size == 0) {
    LOG_E(MAC, "Invalid TBS = 0. Probably caused by missed detection of DCI\n");
    return -1;
  }

  // 38.321 5.4.6
  //  if it is the first UL resource allocated for a new transmission since the last MAC reset:
  //  2> start phr-PeriodicTimer;
  if (mac->scheduling_info.phr_info.is_configured) {
    if (mac->scheduling_info.phr_info.was_mac_reset && pusch_config_pdu->pusch_data.new_data_indicator) {
      mac->scheduling_info.phr_info.was_mac_reset = false;
      nr_timer_start(&mac->scheduling_info.phr_info.periodicPHR_Timer);
    }
  }

  return 0;
}

csi_payload_t nr_ue_aperiodic_csi_reporting(NR_UE_MAC_INST_t *mac, dci_field_t csi_request, int tda, long *k2)
{
  NR_CSI_AperiodicTriggerStateList_t *aperiodicTriggerStateList = mac->sc_info.aperiodicTriggerStateList;
  AssertFatal(aperiodicTriggerStateList, "Received CSI request via DCI but aperiodicTriggerStateList is not present\n");
  int n_states = aperiodicTriggerStateList->list.count;
  int n_ts = csi_request.nbits;
  csi_payload_t csi = {0};
  AssertFatal(n_states <= ((1 << n_ts) - 1), "Case of subselection indication of trigger states not supported yet\n");
  int num_trig = 0;
  for (int i = 0; i < n_ts; i++) {
    // A non-zero codepoint of the CSI request field in the DCI is mapped to a CSI triggering state
    // according to the order of the associated positions of the up to (2^n_ts -1) trigger states
    // in CSI-AperiodicTriggerStateList with codepoint 1 mapped to the triggering state in the first position
    if (csi_request.val & (1 << i)) {
      AssertFatal(num_trig == 0, "Multiplexing more than 1 CSI report is not supported\n");
      NR_CSI_AperiodicTriggerState_t *trigger_state = aperiodicTriggerStateList->list.array[i];
      AssertFatal(trigger_state->associatedReportConfigInfoList.list.count == 1,
                  "Cannot handle more than 1 report configuration per state\n");
      NR_CSI_AssociatedReportConfigInfo_t *reportconfig = trigger_state->associatedReportConfigInfoList.list.array[0];
      NR_CSI_ReportConfigId_t id = reportconfig->reportConfigId;
      NR_CSI_MeasConfig_t *csi_measconfig = mac->sc_info.csi_MeasConfig;
      int found = -1;
      for (int c = 0; c < csi_measconfig->csi_ReportConfigToAddModList->list.count; c++) {
        NR_CSI_ReportConfig_t *report_config = csi_measconfig->csi_ReportConfigToAddModList->list.array[c];
        if (report_config->reportConfigId == id) {
          struct NR_CSI_ReportConfig__reportConfigType__aperiodic__reportSlotOffsetList *offset_list = &report_config->reportConfigType.choice.aperiodic->reportSlotOffsetList;
          AssertFatal(tda < offset_list->list.count, "TDA index from DCI %d exceeds slot offset list %d\n", tda, offset_list->list.count);
          if (k2 == NULL || *k2 < *offset_list->list.array[tda])
            k2 = offset_list->list.array[tda];
          found = c;
          break;
        }
      }
      AssertFatal(found >= 0, "Couldn't find csi-ReportConfig with ID %ld\n", id);
      num_trig++;
      csi = nr_get_csi_payload(mac, found, ON_PUSCH, csi_measconfig);
    }
  }
  return csi;
}

int configure_srs_pdu(NR_UE_MAC_INST_t *mac,
                      NR_SRS_Resource_t *srs_resource,
                      fapi_nr_ul_config_srs_pdu *srs_config_pdu,
                      int period,
                      int offset,
                      NR_SRS_ResourceSet_t *srs_resource_set)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;

  srs_config_pdu->rnti = mac->crnti;
  srs_config_pdu->handle = 0;
  srs_config_pdu->bwp_size = current_UL_BWP->BWPSize;
  srs_config_pdu->bwp_start = current_UL_BWP->BWPStart;
  srs_config_pdu->subcarrier_spacing = current_UL_BWP->scs;
  srs_config_pdu->cyclic_prefix = 0;
  srs_config_pdu->num_ant_ports = srs_resource->nrofSRS_Ports;
  srs_config_pdu->num_symbols = srs_resource->resourceMapping.nrofSymbols;
  srs_config_pdu->num_repetitions = srs_resource->resourceMapping.repetitionFactor;
  srs_config_pdu->time_start_position = srs_resource->resourceMapping.startPosition;
  srs_config_pdu->config_index = srs_resource->freqHopping.c_SRS;
  srs_config_pdu->sequence_id = srs_resource->sequenceId;
  srs_config_pdu->bandwidth_index = srs_resource->freqHopping.b_SRS;
  srs_config_pdu->comb_size = srs_resource->transmissionComb.present - 1;

  switch(srs_resource->transmissionComb.present) {
    case NR_SRS_Resource__transmissionComb_PR_n2:
      srs_config_pdu->comb_offset = srs_resource->transmissionComb.choice.n2->combOffset_n2;
      srs_config_pdu->cyclic_shift = srs_resource->transmissionComb.choice.n2->cyclicShift_n2;
      break;
    case NR_SRS_Resource__transmissionComb_PR_n4:
      srs_config_pdu->comb_offset = srs_resource->transmissionComb.choice.n4->combOffset_n4;
      srs_config_pdu->cyclic_shift = srs_resource->transmissionComb.choice.n4->cyclicShift_n4;
      break;
    default:
      LOG_W(NR_MAC, "Invalid or not implemented comb_size!\n");
  }

  srs_config_pdu->frequency_position = srs_resource->freqDomainPosition;
  srs_config_pdu->frequency_shift = srs_resource->freqDomainShift;
  srs_config_pdu->frequency_hopping = srs_resource->freqHopping.b_hop;
  srs_config_pdu->group_or_sequence_hopping = srs_resource->groupOrSequenceHopping;
  srs_config_pdu->resource_type = srs_resource->resourceType.present - 1;
  if(srs_config_pdu->resource_type > 0) { // not aperiodic
    srs_config_pdu->t_srs = period;
    srs_config_pdu->t_offset = offset;
  }

  bool is_configured_for_pusch_on_current_bwp = true;
  int delta_srs = 0; // DCI format 2_3 not implemented
  srs_config_pdu->tx_power =
      get_srs_tx_power_ue(mac, srs_resource, srs_resource_set, delta_srs, is_configured_for_pusch_on_current_bwp);

#ifdef SRS_DEBUG
  LOG_I(NR_MAC,"Frame = %i, slot = %i\n", frame, slot);
  LOG_I(NR_MAC,"srs_config_pdu->rnti = 0x%04x\n", srs_config_pdu->rnti);
  LOG_I(NR_MAC,"srs_config_pdu->handle = %u\n", srs_config_pdu->handle);
  LOG_I(NR_MAC,"srs_config_pdu->bwp_size = %u\n", srs_config_pdu->bwp_size);
  LOG_I(NR_MAC,"srs_config_pdu->bwp_start = %u\n", srs_config_pdu->bwp_start);
  LOG_I(NR_MAC,"srs_config_pdu->subcarrier_spacing = %u\n", srs_config_pdu->subcarrier_spacing);
  LOG_I(NR_MAC,"srs_config_pdu->cyclic_prefix = %u (0: Normal; 1: Extended)\n", srs_config_pdu->cyclic_prefix);
  LOG_I(NR_MAC,"srs_config_pdu->num_ant_ports = %u (0 = 1 port, 1 = 2 ports, 2 = 4 ports)\n", srs_config_pdu->num_ant_ports);
  LOG_I(NR_MAC,"srs_config_pdu->num_symbols = %u (0 = 1 symbol, 1 = 2 symbols, 2 = 4 symbols)\n", srs_config_pdu->num_symbols);
  LOG_I(NR_MAC,"srs_config_pdu->num_repetitions = %u (0 = 1, 1 = 2, 2 = 4)\n", srs_config_pdu->num_repetitions);
  LOG_I(NR_MAC,"srs_config_pdu->time_start_position = %u\n", srs_config_pdu->time_start_position);
  LOG_I(NR_MAC,"srs_config_pdu->config_index = %u\n", srs_config_pdu->config_index);
  LOG_I(NR_MAC,"srs_config_pdu->sequence_id = %u\n", srs_config_pdu->sequence_id);
  LOG_I(NR_MAC,"srs_config_pdu->bandwidth_index = %u\n", srs_config_pdu->bandwidth_index);
  LOG_I(NR_MAC,"srs_config_pdu->comb_size = %u (0 = comb size 2, 1 = comb size 4, 2 = comb size 8)\n", srs_config_pdu->comb_size);
  LOG_I(NR_MAC,"srs_config_pdu->comb_offset = %u\n", srs_config_pdu->comb_offset);
  LOG_I(NR_MAC,"srs_config_pdu->cyclic_shift = %u\n", srs_config_pdu->cyclic_shift);
  LOG_I(NR_MAC,"srs_config_pdu->frequency_position = %u\n", srs_config_pdu->frequency_position);
  LOG_I(NR_MAC,"srs_config_pdu->frequency_shift = %u\n", srs_config_pdu->frequency_shift);
  LOG_I(NR_MAC,"srs_config_pdu->frequency_hopping = %u\n", srs_config_pdu->frequency_hopping);
  LOG_I(NR_MAC,"srs_config_pdu->group_or_sequence_hopping = %u (0 = No hopping, 1 = Group hopping groupOrSequenceHopping, 2 = Sequence hopping)\n", srs_config_pdu->group_or_sequence_hopping);
  LOG_I(NR_MAC,"srs_config_pdu->resource_type = %u (0: aperiodic, 1: semi-persistent, 2: periodic)\n", srs_config_pdu->resource_type);
  LOG_I(NR_MAC,"srs_config_pdu->t_srs = %u\n", srs_config_pdu->t_srs);
  LOG_I(NR_MAC,"srs_config_pdu->t_offset = %u\n", srs_config_pdu->t_offset);
#endif
  return 0;
}

// Aperiodic SRS scheduling
void nr_ue_aperiodic_srs_scheduling(NR_UE_MAC_INST_t *mac, long resource_trigger, int frame, int slot)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_SRS_Config_t *srs_config = current_UL_BWP->srs_Config;

  if (!srs_config || !srs_config->srs_ResourceSetToAddModList) {
    LOG_E(NR_MAC, "DCI is triggering aperiodic SRS but there is no SRS configuration\n");
    return;
  }

  int slot_offset = 0;
  NR_SRS_Resource_t *srs_resource = NULL;
  NR_SRS_ResourceSet_t *srs_resource_set = NULL;
  for(int rs = 0; rs < srs_config->srs_ResourceSetToAddModList->list.count; rs++) {

    // Find aperiodic resource set
    srs_resource_set = srs_config->srs_ResourceSetToAddModList->list.array[rs];
    if(srs_resource_set->resourceType.present != NR_SRS_ResourceSet__resourceType_PR_aperiodic)
      continue;
    // the resource trigger need to match the DCI one
    if(srs_resource_set->resourceType.choice.aperiodic->aperiodicSRS_ResourceTrigger != resource_trigger)
      continue;
    // if slotOffset is null -> offset = 0
    if(srs_resource_set->resourceType.choice.aperiodic->slotOffset)
      slot_offset = *srs_resource_set->resourceType.choice.aperiodic->slotOffset;

    // Find the corresponding srs resource
    for(int r1 = 0; r1 < srs_resource_set->srs_ResourceIdList->list.count; r1++) {
      for (int r2 = 0; r2 < srs_config->srs_ResourceToAddModList->list.count; r2++) {
        if ((*srs_resource_set->srs_ResourceIdList->list.array[r1] == srs_config->srs_ResourceToAddModList->list.array[r2]->srs_ResourceId) &&
            (srs_config->srs_ResourceToAddModList->list.array[r2]->resourceType.present == NR_SRS_Resource__resourceType_PR_aperiodic)) {
          srs_resource = srs_config->srs_ResourceToAddModList->list.array[r2];
          break;
        }
      }
    }
  }

  if(srs_resource == NULL) {
    LOG_E(NR_MAC, "Couldn't find SRS aperiodic resource with trigger %ld\n", resource_trigger);
    return;
  }

  AssertFatal(slot_offset > DURATION_RX_TO_TX,
              "Slot offset between DCI and aperiodic SRS (%d) needs to be higher than DURATION_RX_TO_TX (%d)\n",
              slot_offset, DURATION_RX_TO_TX);
  int n_slots_frame = nr_slots_per_frame[current_UL_BWP->scs];
  int sched_slot = (slot + slot_offset) % n_slots_frame;
  NR_TDD_UL_DL_ConfigCommon_t *tdd_config = mac->tdd_UL_DL_ConfigurationCommon;
  if (!is_nr_UL_slot(tdd_config, sched_slot, mac->frame_type)) {
    LOG_E(NR_MAC, "Slot for scheduling aperiodic SRS %d is not an UL slot\n", sched_slot);
    return;
  }
  int sched_frame = frame + (slot + slot_offset / n_slots_frame) % MAX_FRAME_NUMBER;
  fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, sched_frame, sched_slot, FAPI_NR_UL_CONFIG_TYPE_SRS);
  if (!pdu)
    return;
  int ret = configure_srs_pdu(mac, srs_resource, &pdu->srs_config_pdu, 0, 0, srs_resource_set);
  if (ret != 0)
    remove_ul_config_last_item(pdu);
  release_ul_config(pdu, false);
}


// Periodic SRS scheduling
static bool nr_ue_periodic_srs_scheduling(NR_UE_MAC_INST_t *mac, frame_t frame, slot_t slot)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_SRS_Config_t *srs_config = current_UL_BWP ? current_UL_BWP->srs_Config : NULL;

  if (!srs_config || !srs_config->srs_ResourceSetToAddModList) {
    return false;
  }

  bool srs_scheduled = false;
  NR_SRS_ResourceSet_t *srs_resource_set = NULL;
  for(int rs = 0; rs < srs_config->srs_ResourceSetToAddModList->list.count; rs++) {
    // Find periodic resource set
    srs_resource_set = srs_config->srs_ResourceSetToAddModList->list.array[rs];
    if(srs_resource_set->resourceType.present != NR_SRS_ResourceSet__resourceType_PR_periodic) {
      continue;
    }

    // Find the corresponding srs resource
    NR_SRS_Resource_t *srs_resource = NULL;
    for(int r1 = 0; r1 < srs_resource_set->srs_ResourceIdList->list.count; r1++) {
      for (int r2 = 0; r2 < srs_config->srs_ResourceToAddModList->list.count; r2++) {
        if ((*srs_resource_set->srs_ResourceIdList->list.array[r1] == srs_config->srs_ResourceToAddModList->list.array[r2]->srs_ResourceId) &&
            (srs_config->srs_ResourceToAddModList->list.array[r2]->resourceType.present == NR_SRS_Resource__resourceType_PR_periodic)) {
          srs_resource = srs_config->srs_ResourceToAddModList->list.array[r2];
          break;
        }
      }
    }

    if(srs_resource == NULL) {
      continue;
    }

    uint16_t period = srs_period[srs_resource->resourceType.choice.periodic->periodicityAndOffset_p.present];
    uint16_t offset = get_nr_srs_offset(srs_resource->resourceType.choice.periodic->periodicityAndOffset_p);

    int n_slots_frame = nr_slots_per_frame[current_UL_BWP->scs];

    // Check if UE should transmit the SRS
    if((frame*n_slots_frame+slot-offset)%period == 0) {
      fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frame, slot, FAPI_NR_UL_CONFIG_TYPE_SRS);
      if (!pdu)
        return false;
      int ret = configure_srs_pdu(mac, srs_resource, &pdu->srs_config_pdu, period, offset, srs_resource_set);
      if (ret != 0)
        remove_ul_config_last_item(pdu);
      else
        srs_scheduled = true;
      release_ul_config(pdu, false);
    }
  }
  return srs_scheduled;
}

// Performs :
// 1. TODO: Call RRC for link status return to PHY
// 2. TODO: Perform SR/BSR procedures for scheduling feedback
void nr_ue_dl_scheduler(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info)
{
  frame_t rx_frame = dl_info->frame;
  slot_t rx_slot = dl_info->slot;

  fapi_nr_dl_config_request_t *dl_config = get_dl_config_request(mac, rx_slot);
  dl_config->sfn  = rx_frame;
  dl_config->slot = rx_slot;
  dl_config->number_pdus = 0;

  if (mac->state == UE_NOT_SYNC || mac->state == UE_DETACHING)
    return;

  if (mac->state == UE_CONNECTED) {
    nr_schedule_csirs_reception(mac, rx_frame, rx_slot);
    nr_schedule_csi_for_im(mac, rx_frame, rx_slot);
  }

  ue_dci_configuration(mac, dl_config, rx_frame, rx_slot);

  if (mac->ul_time_alignment.ta_apply != no_ta)
    schedule_ta_command(dl_config, mac);

  nr_scheduled_response_t scheduled_response = {.dl_config = dl_config,
                                                .module_id = mac->ue_id,
                                                .CC_id = dl_info->cc_id,
                                                .phy_data = dl_info->phy_data,
                                                .mac = mac};
  if (mac->if_module != NULL && mac->if_module->scheduled_response != NULL)
    mac->if_module->scheduled_response(&scheduled_response);
  else
    LOG_E(NR_MAC, "Internal error, no scheduled_response function\n");
}

static bool check_pucchres_for_pending_SR(NR_PUCCH_Config_t *pucch_Config, int target_sr_id)
{
  for (int id = 0; id < pucch_Config->schedulingRequestResourceToAddModList->list.count; id++) {
    NR_SchedulingRequestResourceConfig_t *sr_Config = pucch_Config->schedulingRequestResourceToAddModList->list.array[id];
    if (sr_Config->schedulingRequestID == target_sr_id)  {
      if (sr_Config->resource) {
        return true;
      }
    }
  }
  return false;
}

void schedule_RA_after_SR_failure(NR_UE_MAC_INST_t *mac)
{
  if (get_softmodem_params()->phy_test)
    return; // cannot trigger RA in phytest mode
  trigger_MAC_UE_RA(mac);
  // release PUCCH for all Serving Cells;
  // release SRS for all Serving Cells;
  release_PUCCH_SRS(mac);
  // clear any configured downlink assignments and uplink grants;
  int scs = mac->current_UL_BWP->scs;
  if (mac->dl_config_request)
    memset(mac->dl_config_request, 0, sizeof(*mac->dl_config_request));
  if (mac->ul_config_request)
    clear_ul_config_request(mac, scs);
  // clear any PUSCH resources for semi-persistent CSI reporting
  // TODO we don't have semi-persistent CSI reporting
}

static void nr_update_sr(NR_UE_MAC_INST_t *mac, bool BSRsent)
{
  NR_UE_SCHEDULING_INFO *sched_info = &mac->scheduling_info;

  // if no pending data available for transmission
  // All pending SR(s) shall be cancelled and each respective sr-ProhibitTimer shall be stopped
  // in TS 38.321:
  // The SR configuration of the logical channel that triggered the BSR
  // (subclause 5.4.5) (if such a configuration exists) is considered as corresponding SR configuration for the triggered SR
  // in the hereafter code, we compute actual state at the time we compute the SR
  // So, it might be more accurate than the logical channel that triggered the BSR some slots ago if new data came in the UE for
  // ULSCH

  nr_lcordered_info_t *lc_info = NULL;
  for (int idx = 0; idx < mac->lc_ordered_list.count && lc_info == NULL; idx++) {
    nr_lcordered_info_t *lc_info_loop = mac->lc_ordered_list.array[idx];
    NR_LC_SCHEDULING_INFO *lc_sched_info = get_scheduling_info_from_lcid(mac, lc_info_loop->lcid);
    if (lc_sched_info->LCID_buffer_remain)
      lc_info = lc_info_loop;
  }

  if (lc_info == NULL) { // all queues are empty
    for (int i = 0; i < NR_MAX_SR_ID; i++) {
      nr_sr_info_t *sr = &sched_info->sr_info[i];
      if (sr->active_SR_ID) {
        LOG_D(NR_MAC, "No pending data available -> Canceling pending SRs\n");
        sr->pending = false;
        sr->counter = 0;
        nr_timer_stop(&sr->prohibitTimer);
      }
    }
    return;
  }

  // if a Regular BSR has been triggered and logicalChannelSR-DelayTimer is not running
  if (BSRsent || nr_timer_is_active(&sched_info->sr_DelayTimer))
    return;

  // if there is no UL-SCH resource available for a new transmission (ie we are at this point)
  // if the MAC entity is configured with configured uplink grant(s) and the Regular BSR was triggered for a
  // logical channel for which logicalChannelSR-Mask is set to false or
  if (mac->current_UL_BWP->configuredGrantConfig && lc_info->lc_SRMask)
    return;

  // if the UL-SCH resources available for a new transmission do not meet the LCP mapping restrictions
  // TODO not implemented

  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_PUCCH_Config_t *pucch_Config = current_UL_BWP ? current_UL_BWP->pucch_Config : NULL;
  if (!pucch_Config || !pucch_Config->schedulingRequestResourceToAddModList)
    return; // cannot schedule SR if there is no schedulingRequestResource configured

  if (lc_info->sr_id < 0 || lc_info->sr_id >= NR_MAX_SR_ID)
    LOG_E(NR_MAC, "No SR corresponding to this LCID\n"); // TODO not sure what to do here
  else {
    nr_sr_info_t *sr = &sched_info->sr_info[lc_info->sr_id];
    if (!sr->pending) {
      if (check_pucchres_for_pending_SR(pucch_Config, lc_info->sr_id)) {
        // trigger SR
        LOG_D(NR_MAC, "Triggering SR for ID %d\n", lc_info->sr_id);
        sr->pending = true;
        sr->counter = 0;
      } else {
        // initiate a Random Access procedure on the SpCell and cancel the pending SR
        // if the MAC entity has no valid PUCCH resource configured for the pending SR
        sr->pending = false;
        sr->counter = 0;
        nr_timer_stop(&sr->prohibitTimer);
        schedule_RA_after_SR_failure(mac);
      }
    }
  }
}

static void nr_update_rlc_buffers_status(NR_UE_MAC_INST_t *mac, frame_t frameP, slot_t slotP, uint8_t gNB_index)
{
  for (int i = 0; i < mac->lc_ordered_list.count; i++) {
    nr_lcordered_info_t *lc_info = mac->lc_ordered_list.array[i];
    int lcid = lc_info->lcid;
    NR_LC_SCHEDULING_INFO *lc_sched_info = get_scheduling_info_from_lcid(mac, lcid);
    mac_rlc_status_resp_t rlc_status =
        mac_rlc_status_ind(mac->ue_id, mac->ue_id, gNB_index, frameP, slotP, ENB_FLAG_NO, MBMS_FLAG_NO, lcid, 0, 0);

    if (rlc_status.bytes_in_buffer > 0) {
      LOG_D(NR_MAC,
            "[UE %d] LCID %d has %d bytes to transmit at sfn %d.%d\n",
            mac->ue_id,
            lcid,
            rlc_status.bytes_in_buffer,
            frameP,
            slotP);
      lc_sched_info->LCID_buffer_remain = rlc_status.bytes_in_buffer;
    }
  }
}

/*TS 38.321
A BSR shall be triggered if any of the following events occur:
- UL data, for a logical channel which belongs to an LCG, becomes available to the MAC entity; and either
   => here we don't implement exactly the same, there is no direct relation with new data came in the UE since last BSR

- this UL data belongs to a logical channel with higher priority than the priority of any logical channel
  containing available UL data which belong to any LCG; or
  => same, we don't know the last BSR content

- none of the logical channels which belong to an LCG contains any available UL data.
  in which case the BSR is referred below to as 'Regular BSR';

- UL resources are allocated and number of padding bits is equal to or larger than the size of the Buffer Status
Report MAC CE plus its subheader, in which case the BSR is referred below to as 'Padding BSR';

- retxBSR-Timer expires, and at least one of the logical channels which belong to an LCG contains UL data, in
which case the BSR is referred below to as 'Regular BSR';

- periodicBSR-Timer expires, in which case the BSR is referred below to as 'Periodic BSR'.

*/

static void nr_update_bsr(NR_UE_MAC_INST_t *mac, uint32_t *LCG_bytes)
{
  bool bsr_regular_triggered = mac->scheduling_info.BSR_reporting_active & NR_BSR_TRIGGER_REGULAR;
  for (int i = 0; i < mac->lc_ordered_list.count; i++) {
    nr_lcordered_info_t *lc_info = mac->lc_ordered_list.array[i];
    int lcid = lc_info->lcid;
    NR_LC_SCHEDULING_INFO *lc_sched_info = get_scheduling_info_from_lcid(mac, lcid);
    int lcgid = lc_sched_info->LCGID;
    // check if UL data for a logical channel which belongs to a LCG becomes available for transmission
    if (lcgid != NR_INVALID_LCGID) {
      // Update waiting bytes for this LCG
      LCG_bytes[lcgid] += lc_sched_info->LCID_buffer_remain;
      if (!bsr_regular_triggered) {
        bsr_regular_triggered = true;
        trigger_regular_bsr(mac, lcid, lc_info->sr_DelayTimerApplied);
        LOG_D(NR_MAC, "[UE %d] MAC BSR Triggered\n", mac->ue_id);
      }
    }
  }
}

void nr_ue_ul_scheduler(NR_UE_MAC_INST_t *mac, nr_uplink_indication_t *ul_info)
{
  int cc_id = ul_info->cc_id;
  frame_t frame_tx = ul_info->frame;
  slot_t slot_tx = ul_info->slot;
  uint32_t gNB_index = ul_info->gNB_index;

  RA_config_t *ra = &mac->ra;
  if(mac->state > UE_NOT_SYNC && mac->state < UE_CONNECTED) {
    nr_ue_get_rach(mac, cc_id, frame_tx, gNB_index, slot_tx);
    nr_ue_prach_scheduler(mac, frame_tx, slot_tx);
  }

  bool BSRsent = false;
  if (mac->state == UE_CONNECTED) {
    nr_ue_periodic_srs_scheduling(mac, frame_tx, slot_tx);
    nr_update_rlc_buffers_status(mac, frame_tx, slot_tx, gNB_index);
  }

  // Schedule ULSCH only if the current frame and slot match those in ul_config_req
  // AND if a UL grant (UL DCI or Msg3) has been received (as indicated by num_pdus)
  uint8_t ulsch_input_buffer_array[NFAPI_MAX_NUM_UL_PDU][MAX_ULSCH_PAYLOAD_BYTES];
  int number_of_pdus = 0;

  fapi_nr_ul_config_request_pdu_t *ulcfg_pdu = lockGet_ul_iterator(mac, frame_tx, slot_tx);
  if (!ulcfg_pdu)
    return;
  LOG_D(NR_MAC, "number of UL PDUs: %d with UL transmission in sfn [%d.%d]\n", *ulcfg_pdu->privateNBpdus, frame_tx, slot_tx);

  while (ulcfg_pdu->pdu_type != FAPI_NR_END) {
    uint8_t *ulsch_input_buffer = ulsch_input_buffer_array[number_of_pdus];
    if (ulcfg_pdu->pdu_type == FAPI_NR_UL_CONFIG_TYPE_PUSCH) {
      nfapi_nr_ue_pusch_pdu_t *pdu = &ulcfg_pdu->pusch_config_pdu;
      uint32_t TBS_bytes = pdu->pusch_data.tb_size;
      LOG_D(NR_MAC,
            "harq_id %d, new_data_indicator %d, TBS_bytes %d (ra_state %d)\n",
            pdu->pusch_data.harq_process_id,
            pdu->pusch_data.new_data_indicator,
            TBS_bytes,
            ra->ra_state);
      pdu->tx_request_body.fapiTxPdu = NULL;
      if ((ra->ra_state == nrRA_WAIT_RAR || ra->ra_state == nrRA_WAIT_MSGB) && !ra->cfra) {
        nr_get_Msg3_MsgA_PUSCH_payload(mac, ulsch_input_buffer, TBS_bytes);
        for (int k = 0; k < TBS_bytes; k++) {
          LOG_D(NR_MAC, "(%i): 0x%x\n", k, ulsch_input_buffer[k]);
        }
        pdu->tx_request_body.fapiTxPdu = ulsch_input_buffer;
        pdu->tx_request_body.pdu_length = TBS_bytes;
        number_of_pdus++;
      } else {
        if (ulcfg_pdu->pusch_config_pdu.pusch_data.new_data_indicator
            && (mac->state == UE_CONNECTED || (ra->ra_state == nrRA_WAIT_RAR && ra->cfra))) {
          if (!nr_timer_is_active(&mac->time_alignment_timer) && mac->state == UE_CONNECTED && !get_softmodem_params()->phy_test) {
            // UL data arrival during RRC_CONNECTED when UL synchronisation status is "non-synchronised"
            trigger_MAC_UE_RA(mac);
            return;
          }
          // Getting IP traffic to be transmitted
          int tx_power = pdu->tx_power;
          int P_CMAX = nr_get_Pcmax(mac->p_Max,
                                    mac->nr_band,
                                    mac->frame_type,
                                    mac->frequency_range,
                                    mac->current_UL_BWP->channel_bandwidth,
                                    pdu->qam_mod_order,
                                    false,
                                    mac->current_UL_BWP->scs,
                                    mac->current_UL_BWP->BWPSize,
                                    pdu->transform_precoding,
                                    pdu->rb_size,
                                    pdu->rb_start);

          nr_ue_get_sdu(mac, cc_id, frame_tx, slot_tx, gNB_index, ulsch_input_buffer, TBS_bytes, tx_power, P_CMAX, &BSRsent);
          pdu->tx_request_body.fapiTxPdu = ulsch_input_buffer;
          pdu->tx_request_body.pdu_length = TBS_bytes;
          number_of_pdus++;
          // start or restart dataInactivityTimer  if any MAC entity transmits a MAC SDU for DTCH logical channel,
          // or DCCH logical channel
          if (mac->data_inactivity_timer)
            nr_timer_start(mac->data_inactivity_timer);
        }
      }

      if (ra->ra_state == nrRA_WAIT_CONTENTION_RESOLUTION && !ra->cfra) {
        LOG_I(NR_MAC, "[RAPROC][%d.%d] RA-Msg3 retransmitted\n", frame_tx, slot_tx);
        // 38.321 restart the ra-ContentionResolutionTimer at each HARQ retransmission in the first symbol after the end of the Msg3
        // transmission
        nr_Msg3_transmitted(mac, cc_id, frame_tx, slot_tx, gNB_index);
      }
      if (ra->ra_state == nrRA_WAIT_RAR && !ra->cfra) {
        LOG_A(NR_MAC, "[RAPROC][%d.%d] RA-Msg3 transmitted\n", frame_tx, slot_tx);
        nr_Msg3_transmitted(mac, cc_id, frame_tx, slot_tx, gNB_index);
      }
      if (ra->ra_state == nrRA_WAIT_MSGB && !ra->cfra) {
        LOG_A(NR_MAC, "[RAPROC][%d.%d] RA-MsgA-PUSCH transmitted\n", frame_tx, slot_tx);
      }
    }
    ulcfg_pdu++;
  }
  release_ul_config(ulcfg_pdu, false);

  if(mac->state >= UE_PERFORMING_RA && mac->state < UE_DETACHING)
    nr_ue_pucch_scheduler(mac, frame_tx, slot_tx);

  if (mac->if_module != NULL && mac->if_module->scheduled_response != NULL) {
    LOG_D(NR_MAC, "3# scheduled_response transmitted,%d, %d\n", frame_tx, slot_tx);
    nr_scheduled_response_t scheduled_response = {.ul_config = mac->ul_config_request + slot_tx,
                                                  .mac = mac,
                                                  .module_id = mac->ue_id,
                                                  .CC_id = cc_id,
                                                  .phy_data = ul_info->phy_data};
    mac->if_module->scheduled_response(&scheduled_response);
  }

  if(mac->state == UE_CONNECTED)
    nr_update_sr(mac, BSRsent);
  // Global variables implicit logic
  // far away, BSR_reporting_active is set
  mac->scheduling_info.BSR_reporting_active = NR_BSR_TRIGGER_NONE;

  // update Bj for all active lcids before LCP procedure
  if (mac->current_UL_BWP) {
    LOG_D(NR_MAC, "%4d.%2d Logical Channel Prioritization\n", frame_tx, slot_tx);
    for (int i = 0; i < mac->lc_ordered_list.count; i++) {
      nr_lcordered_info_t *lc_info = mac->lc_ordered_list.array[i];
      int lcid = lc_info->lcid;
      // max amount of data that can be buffered/accumulated in a logical channel buffer
      uint32_t bucketSize_max = lc_info->bucket_size;
      //  measure Bj increment the value of Bj by product PBR  * T
      NR_LC_SCHEDULING_INFO *sched_info = get_scheduling_info_from_lcid(mac, lcid);
      int32_t bj = sched_info->Bj;
      if (lc_info->pbr < UINT_MAX) {
        uint32_t slots_elapsed = nr_timer_elapsed_time(&sched_info->Bj_timer); // slots elapsed since Bj was last incremented
        // it is safe to divide by 1k since pbr in lc_info is computed multiplying by 1000 the RRC value to convert kB/s to B/s
        uint32_t pbr_ms = lc_info->pbr / 1000;
        bj += ((pbr_ms * slots_elapsed) >> mac->current_UL_BWP->scs); // each slot length is 1/scs ms
      }
      else
        bj = INT_MAX;
      // bj > max bucket size, set bj to max bucket size, as in ts38.321 5.4.3.1 Logical Channel Prioritization
      sched_info->Bj = min(bj, bucketSize_max);
      // reset bj timer
      nr_timer_start(&sched_info->Bj_timer);
    }
  }
}

static uint8_t nr_locate_BsrIndexByBufferSize(const uint32_t *table, int size, int value)
{
  if (value == 0) {
    return 0;   //elseif (value > 150000) return 63;
  }

  int jl = 0; // lower bound
  int ju = size - 1; // upper bound
  bool ascend = table[ju] >= table[jl] ? 1 : 0; // determine the order of the the table:  1 if ascending order of table, 0 otherwise
  while (ju - jl > 1) { //If we are not yet done,
    int jm = (ju + jl) / 2; // compute a midpoint,
    if ((value >= table[jm]) == ascend) {
      jl = jm;    // replace the lower limit
    } else {
      ju = jm;    //replace the upper limit
    }

    LOG_T(NR_MAC, "[UE] searching BSR index %d for (BSR TABLE %d < value %d)\n",
          jm, table[jm], value);
  }

  if (value == table[jl]) {
    return jl;
  } else {
    return jl + 1;    //equally  ju
  }
}

// PUSCH scheduler:
// - Calculate the slot in which ULSCH should be scheduled. This is current slot + K2,
// - where K2 is the offset between the slot in which UL DCI is received and the slot
// - in which ULSCH should be scheduled. K2 is configured in RRC configuration.  
// PUSCH Msg3 scheduler:
// - scheduled by RAR UL grant according to 8.3 of TS 38.213
// Note: Msg3 tx in the uplink symbols of mixed slot
int nr_ue_pusch_scheduler(const NR_UE_MAC_INST_t *mac,
                          const uint8_t is_Msg3,
                          const frame_t current_frame,
                          const int current_slot,
                          frame_t *frame_tx,
                          int *slot_tx,
                          const long k2)
{
  int delta = 0;
  const NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;

  // Get the numerology to calculate the Tx frame and slot
  int mu = current_UL_BWP->scs;

  // k2 as per 3GPP TS 38.214 version 15.9.0 Release 15 ch 6.1.2.1.1
  // PUSCH time domain resource allocation is higher layer configured from uschTimeDomainAllocationList in either pusch-ConfigCommon

  if (is_Msg3) {

    switch (mu) {
      case 0:
        delta = 2;
        break;
      case 1:
        delta = 3;
        break;
      case 2:
        delta = 4;
        break;
      case 3:
        delta = 6;
        break;
      default:
        AssertFatal(1 == 0, "Invalid numerology %i\n", mu);
    }

    AssertFatal((k2 + delta) > DURATION_RX_TO_TX,
                "Slot offset (%ld) for Msg3 needs to be higher than DURATION_RX_TO_TX (%d). Please set min_rxtxtime at least to %d in gNB config file or gNBs.[0].min_rxtxtime=%d via command line.\n",
                k2,
                DURATION_RX_TO_TX,
                DURATION_RX_TO_TX,
                DURATION_RX_TO_TX);

    *slot_tx = (current_slot + k2 + delta) % nr_slots_per_frame[mu];
    *frame_tx = (current_frame + (current_slot + k2 + delta) / nr_slots_per_frame[mu]) % MAX_FRAME_NUMBER;

  } else {

    AssertFatal(k2 > DURATION_RX_TO_TX,
                "Slot offset K2 (%ld) needs to be higher than DURATION_RX_TO_TX (%d). Please set min_rxtxtime at least to %d in gNB config file or gNBs.[0].min_rxtxtime=%d via command line.\n",
                k2,
                DURATION_RX_TO_TX,
                DURATION_RX_TO_TX,
                DURATION_RX_TO_TX);

    if (k2 < 0) { // This can happen when a false DCI is received
      LOG_W(PHY, "%d.%d. Received k2 %ld\n", current_frame, current_slot, k2);
      return -1;
    }

    // Calculate TX slot and frame
    *slot_tx = (current_slot + k2) % nr_slots_per_frame[mu];
    *frame_tx = (current_frame + (current_slot + k2) / nr_slots_per_frame[mu]) % MAX_FRAME_NUMBER;

  }

  LOG_D(NR_MAC, "[%04d.%02d] UL transmission in [%04d.%02d] (k2 %ld delta %d)\n", current_frame, current_slot, *frame_tx, *slot_tx, k2, delta);

  return 0;
}

// Build the list of all the valid RACH occasions in the maximum association pattern period according to the PRACH config
static void build_ro_list(NR_UE_MAC_INST_t *mac)
{
  int x,y; // PRACH Configuration Index table variables used to compute the valid frame numbers
  int y2;  // PRACH Configuration Index table additional variable used to compute the valid frame numbers
  uint8_t slot_shift_for_map;
  uint8_t map_shift;
  bool even_slot_invalid;
  int64_t s_map;
  uint8_t prach_conf_start_symbol; // Starting symbol of the PRACH occasions in the PRACH slot
  uint8_t N_t_slot; // Number of PRACH occasions in a 14-symbols PRACH slot
  uint8_t N_dur; // Duration of a PRACH occasion (nb of symbols)
  uint16_t format = 0xffff;
  uint8_t format2 = 0xff;
  int nb_fdm;

  uint8_t config_index;
  int msg1_FDM;

  uint8_t nb_of_frames_per_prach_conf_period;

  NR_RACH_ConfigCommon_t *setup = mac->current_UL_BWP->rach_ConfigCommon;
  NR_RACH_ConfigGeneric_t *rach_ConfigGeneric = &setup->rach_ConfigGeneric;

  config_index = rach_ConfigGeneric->prach_ConfigurationIndex;
  msg1_FDM = rach_ConfigGeneric->msg1_FDM;

  switch (msg1_FDM){
    case 0:
    case 1:
    case 2:
    case 3:
      nb_fdm = 1 << msg1_FDM;
      break;
    default:
      AssertFatal(1 == 0, "Unknown msg1_FDM from rach_ConfigGeneric %d\n", msg1_FDM);
  }

  // Create the PRACH occasions map
  // WIP: For now assume no rejected PRACH occasions because of conflict with SSB or TDD_UL_DL_ConfigurationCommon schedule

  int unpaired = mac->phy_config.config_req.cell_config.frame_duplex_type;

  const int64_t *prach_config_info_p = get_prach_config_info(mac->frequency_range, config_index, unpaired);
  int mu = nr_get_prach_mu(mac->current_UL_BWP->msgA_ConfigCommon_r16, setup);

  // Identify the proper PRACH Configuration Index table according to the operating frequency
  LOG_D(NR_MAC,"mu = %u, PRACH config index  = %u, unpaired = %u\n", mu, config_index, unpaired);

  if (mac->frequency_range == FR2) { //FR2

    x = prach_config_info_p[2];
    y = prach_config_info_p[3];
    y2 = prach_config_info_p[4];

    s_map = prach_config_info_p[5];

    prach_conf_start_symbol = prach_config_info_p[6];
    N_t_slot = prach_config_info_p[8];
    N_dur = prach_config_info_p[9];
    if (prach_config_info_p[1] != -1)
      format2 = (uint8_t) prach_config_info_p[1];
    format = ((uint8_t) prach_config_info_p[0]) | (format2<<8);

    slot_shift_for_map = mu-2;
    if ( (mu == 3) && (prach_config_info_p[7] == 1) )
      even_slot_invalid = true;
    else
      even_slot_invalid = false;
  }
  else { // FR1
    x = prach_config_info_p[2];
    y = prach_config_info_p[3];
    y2 = y;

    s_map = prach_config_info_p[4];

    prach_conf_start_symbol = prach_config_info_p[5];
    N_t_slot = prach_config_info_p[7];
    N_dur = prach_config_info_p[8];
    LOG_D(NR_MAC,"N_t_slot %d, N_dur %d\n",N_t_slot,N_dur);
    if (prach_config_info_p[1] != -1)
      format2 = (uint8_t) prach_config_info_p[1];
    format = ((uint8_t) prach_config_info_p[0]) | (format2<<8);

    slot_shift_for_map = mu;
    if ( (mu == 1) && (prach_config_info_p[6] <= 1) )
      // no prach in even slots @ 30kHz for 1 prach per subframe
      even_slot_invalid = true;
    else
      even_slot_invalid = false;
  } // FR2 / FR1

  const int bwp_id = mac->current_UL_BWP->bwp_id;
  prach_association_pattern_t *prach_assoc_pattern = &mac->prach_assoc_pattern[bwp_id];
  prach_assoc_pattern->nb_of_prach_conf_period_in_max_period = MAX_NB_PRACH_CONF_PERIOD_IN_ASSOCIATION_PATTERN_PERIOD / x;
  nb_of_frames_per_prach_conf_period = x;

  LOG_D(NR_MAC,"nb_of_prach_conf_period_in_max_period %d\n", prach_assoc_pattern->nb_of_prach_conf_period_in_max_period);

  // Fill in the PRACH occasions table for every slot in every frame in every PRACH configuration periods in the maximum association pattern period
  // ----------------------------------------------------------------------------------------------------------------------------------------------
  // ----------------------------------------------------------------------------------------------------------------------------------------------
  // For every PRACH configuration periods
  // -------------------------------------
  for (int period_idx = 0; period_idx < prach_assoc_pattern->nb_of_prach_conf_period_in_max_period; period_idx++) {
    prach_conf_period_t *prach_conf_period_list = &prach_assoc_pattern->prach_conf_period_list[period_idx];
    prach_conf_period_list->nb_of_prach_occasion = 0;
    prach_conf_period_list->nb_of_frame = nb_of_frames_per_prach_conf_period;
    prach_conf_period_list->nb_of_slot = nr_slots_per_frame[mu];

    LOG_D(NR_MAC,"PRACH Conf Period Idx %d\n", period_idx);

    // For every frames in a PRACH configuration period
    // ------------------------------------------------
    for (int frame_idx = 0; frame_idx < nb_of_frames_per_prach_conf_period; frame_idx++) {
      int frame_rach = (period_idx * nb_of_frames_per_prach_conf_period) + frame_idx;

      LOG_D(NR_MAC,"PRACH Conf Period Frame Idx %d - Frame %d\n", frame_idx, frame_rach);
      // Is it a valid frame for this PRACH configuration index? (n_sfn mod x = y)
      if ((frame_rach % x) == y || (frame_rach % x) == y2) {

        // For every slot in a frame
        // -------------------------
        for (int slot = 0; slot < nr_slots_per_frame[mu]; slot++) {
          // Is it a valid slot?
          map_shift = slot >> slot_shift_for_map; // in PRACH configuration index table slots are numbered wrt 60kHz
          if ((s_map>>map_shift) & 0x01) {
            // Valid slot

            // Additionally, for 30kHz/120kHz, we must check for the n_RA_Slot param also
            if (even_slot_invalid && (slot%2 == 0))
              continue; // no prach in even slots @ 30kHz/120kHz for 1 prach per 60khz slot/subframe

            // We're good: valid frame and valid slot
            // Compute all the PRACH occasions in the slot

            prach_occasion_slot_t *slot_map = &prach_conf_period_list->prach_occasion_slot_map[frame_idx][slot];
            slot_map->nb_of_prach_occasion_in_time = N_t_slot;
            slot_map->nb_of_prach_occasion_in_freq = nb_fdm;
            slot_map->prach_occasion = malloc(N_t_slot * nb_fdm * sizeof(*slot_map->prach_occasion));
            AssertFatal(slot_map->prach_occasion, "no memory available\n");
            for (int n_prach_occ_in_time = 0; n_prach_occ_in_time < N_t_slot; n_prach_occ_in_time++) {
              uint8_t start_symbol = prach_conf_start_symbol + n_prach_occ_in_time * N_dur;
              LOG_D(NR_MAC,"PRACH Occ in time %d\n", n_prach_occ_in_time);

              for (int n_prach_occ_in_freq = 0; n_prach_occ_in_freq < nb_fdm; n_prach_occ_in_freq++) {
                slot_map->prach_occasion[n_prach_occ_in_time * nb_fdm + n_prach_occ_in_freq] =
                    (prach_occasion_info_t){.start_symbol = start_symbol,
                                            .fdm = n_prach_occ_in_freq,
                                            .frame = frame_idx,
                                            .slot = slot,
                                            .format = format};
                prach_assoc_pattern->prach_conf_period_list[period_idx].nb_of_prach_occasion++;

                LOG_D(NR_MAC,
                      "Adding a PRACH occasion: frame %u, slot-symbol %d-%d, occ_in_time-occ_in-freq %d-%d, nb ROs in conf period %d, for this slot: RO# in time %d, RO# in freq %d\n",
                      frame_rach,
                      slot,
                      start_symbol,
                      n_prach_occ_in_time,
                      n_prach_occ_in_freq,
                      prach_conf_period_list->nb_of_prach_occasion,
                      slot_map->nb_of_prach_occasion_in_time,
                      slot_map->nb_of_prach_occasion_in_freq);
              } // For every freq in the slot
            } // For every time occasions in the slot
          } // Valid slot?
        } // For every slots in a frame
      } // Valid frame?
    } // For every frames in a prach configuration period
  } // For every prach configuration periods in the maximum association pattern period (160ms)
}

// Build the list of all the valid/transmitted SSBs according to the config
static void build_ssb_list(NR_UE_MAC_INST_t *mac)
{
  // Create the list of transmitted SSBs
  const int bwp_id = mac->current_UL_BWP->bwp_id;
  ssb_list_info_t *ssb_list = &mac->ssb_list[bwp_id];
  fapi_nr_config_request_t *cfg = &mac->phy_config.config_req;
  ssb_list->nb_tx_ssb = 0;

  for (int ssb_index = 0; ssb_index < MAX_NB_SSB; ssb_index++) {
    uint32_t curr_mask = cfg->ssb_table.ssb_mask_list[ssb_index / 32].ssb_mask;
    // check if if current SSB is transmitted
    if ((curr_mask >> (31 - (ssb_index % 32))) & 0x01) {
      ssb_list->nb_ssb_per_index[ssb_index] = ssb_list->nb_tx_ssb;
      ssb_list->nb_tx_ssb++;
    }
    else
      ssb_list->nb_ssb_per_index[ssb_index] = -1;
  }
  ssb_list->tx_ssb = calloc(ssb_list->nb_tx_ssb, sizeof(*ssb_list->tx_ssb));
}

static int get_ssb_idx_from_list(ssb_list_info_t *ssb_list, int idx)
{
  for (int ssb_index = 0; ssb_index < MAX_NB_SSB; ssb_index++) {
    if (ssb_list->nb_ssb_per_index[ssb_index] == idx)
      return ssb_index;
  }
  AssertFatal(false, "Couldn't find SSB index in SSB list\n");
  return 0;
}

// Map the transmitted SSBs to the ROs and create the association pattern according to the config
static void map_ssb_to_ro(NR_UE_MAC_INST_t *mac)
{
  // Map SSBs to PRACH occasions
  // WIP: Assumption: No PRACH occasion is rejected because of a conflict with SSBs or TDD_UL_DL_ConfigurationCommon schedule
  NR_RACH_ConfigCommon_t *setup = mac->current_UL_BWP->rach_ConfigCommon;
  NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR ssb_perRACH_config = setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->present;

  const struct {
    // true if more than one or exactly one SSB per RACH occasion, false if more than one RO per SSB
    bool multiple_ssb_per_ro;
    // Nb of SSBs per RACH or RACHs per SSB
    int ssb_rach_ratio;
  } config[] = {{false, 0}, {false, 8}, {false, 4}, {false, 2}, {true, 1}, {true, 2}, {true, 4}, {true, 8}, {true, 16}};
  AssertFatal(ssb_perRACH_config <= NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_sixteen,
              "Unsupported ssb_perRACH_config %d\n",
              ssb_perRACH_config);
  const bool multiple_ssb_per_ro = config[ssb_perRACH_config].multiple_ssb_per_ro;
  const int ssb_rach_ratio = config[ssb_perRACH_config].ssb_rach_ratio;

  LOG_D(NR_MAC,"SSB rach ratio %d, Multiple SSB per RO %d\n", ssb_rach_ratio, multiple_ssb_per_ro);

  const int bwp_id = mac->current_UL_BWP->bwp_id;
  ssb_list_info_t *ssb_list = &mac->ssb_list[bwp_id];

  // Evaluate the number of PRACH configuration periods required to map all the SSBs and set the association period
  // WIP: Assumption for now is that all the PRACH configuration periods within a maximum association pattern period have the same
  // number of PRACH occasions
  //      (No PRACH occasions are conflicting with SSBs nor TDD_UL_DL_ConfigurationCommon schedule)
  //      There is only one possible association period which can contain up to 16 PRACH configuration periods
  LOG_D(NR_MAC,"Evaluate the number of PRACH configuration periods required to map all the SSBs and set the association period\n");
  const int required_nb_of_prach_occasion =
      multiple_ssb_per_ro ? ((ssb_list->nb_tx_ssb - 1) + ssb_rach_ratio) / ssb_rach_ratio : ssb_list->nb_tx_ssb * ssb_rach_ratio;

  prach_association_pattern_t *prach_assoc_pattern = &mac->prach_assoc_pattern[bwp_id];
  const prach_conf_period_t *prach_conf_period = &prach_assoc_pattern->prach_conf_period_list[0];
  AssertFatal(prach_conf_period->nb_of_prach_occasion > 0,
              "prach_conf_period->nb_of_prach_occasion shouldn't be 0 (nb_tx_ssb %d, ssb_rach_ratio %d)\n",
              ssb_list->nb_tx_ssb,
              ssb_rach_ratio);
  prach_association_period_t *prach_association_period = &prach_assoc_pattern->prach_association_period_list[0];
  const int required_nb_of_prach_conf_period =
      ((required_nb_of_prach_occasion - 1) + prach_conf_period->nb_of_prach_occasion) / prach_conf_period->nb_of_prach_occasion;

  if (required_nb_of_prach_conf_period == 1) {
    prach_association_period->nb_of_prach_conf_period = 1;
  }
  else if (required_nb_of_prach_conf_period == 2) {
    prach_association_period->nb_of_prach_conf_period = 2;
  }
  else if (required_nb_of_prach_conf_period <= 4) {
    prach_association_period->nb_of_prach_conf_period = 4;
  }
  else if (required_nb_of_prach_conf_period <= 8) {
    prach_association_period->nb_of_prach_conf_period = 8;
  }
  else if (required_nb_of_prach_conf_period <= 16) {
    prach_association_period->nb_of_prach_conf_period = 16;
  }
  else {
    AssertFatal(1 == 0, "Invalid number of PRACH config periods within an association period %d\n", required_nb_of_prach_conf_period);
  }

  prach_assoc_pattern->nb_of_assoc_period = 1; // WIP: only one possible association period
  prach_association_period->nb_of_frame = prach_association_period->nb_of_prach_conf_period * prach_conf_period->nb_of_frame;
  prach_assoc_pattern->nb_of_frame = prach_association_period->nb_of_frame;

  LOG_D(NR_MAC,
        "Assoc period %d, Nb of frames in assoc period %d\n",
        prach_association_period->nb_of_prach_conf_period,
        prach_association_period->nb_of_frame);

  // Set the starting PRACH Configuration period index in the association_pattern map for this particular association period
  int prach_configuration_period_idx =
      0; // WIP: only one possible association period so the starting PRACH configuration period is automatically 0

  // Map all the association periods within the association pattern period
  LOG_D(NR_MAC,"Proceed to the SSB to RO mapping\n");
  // Check if we need to map multiple SSBs per RO or multiple ROs per SSB

  if (multiple_ssb_per_ro) {
    const prach_association_period_t *end =
        prach_assoc_pattern->prach_association_period_list + prach_assoc_pattern->nb_of_assoc_period;
    for (prach_association_period_t *prach_period = prach_assoc_pattern->prach_association_period_list; prach_period < end;
         prach_period++) {
      // Set the starting PRACH Configuration period index in the association_pattern map for this particular association period
      // WIP: only one possible association period so the starting PRACH configuration period is automatically 0
      // WIP: For the moment, only map each SSB idx once per association period if configuration is multiple SSBs per RO
      //      this is true if no PRACH occasions are conflicting with SSBs nor TDD_UL_DL_ConfigurationCommon schedule
      int idx = 0;
      bool done = false;
      for (int i = 0; i < prach_period->nb_of_prach_conf_period && !done; i++, prach_configuration_period_idx++) {
        prach_period->prach_conf_period_list[i] = &prach_assoc_pattern->prach_conf_period_list[prach_configuration_period_idx];
        prach_conf_period_t *prach_conf = prach_period->prach_conf_period_list[i];
        // Build the association period with its association PRACH Configuration indexes
        // Go through all the ROs within the PRACH config period
        for (int frame = 0; frame < prach_conf->nb_of_frame && !done; frame++) {
          for (int slot = 0; slot < prach_conf->nb_of_slot && !done; slot++) {
            prach_occasion_slot_t *slot_map = &prach_conf->prach_occasion_slot_map[frame][slot];
            for (int ro_in_time = 0; ro_in_time < slot_map->nb_of_prach_occasion_in_time && !done; ro_in_time++) {
              for (int ro_in_freq = 0; ro_in_freq < slot_map->nb_of_prach_occasion_in_freq && !done; ro_in_freq++) {
                prach_occasion_info_t *ro_p =
                    slot_map->prach_occasion + ro_in_time * slot_map->nb_of_prach_occasion_in_freq + ro_in_freq;
                // Go through the list of transmitted SSBs and map the required amount of SSBs to this RO
                // WIP: For the moment, only map each SSB idx once per association period if configuration is multiple SSBs per RO
                //      this is true if no PRACH occasions are conflicting with SSBs nor TDD_UL_DL_ConfigurationCommon schedule
                for (; idx < ssb_list->nb_tx_ssb; idx++) {
                  ssb_info_t *tx_ssb = ssb_list->tx_ssb + idx;
                  // Map only the transmitted ssb_idx
                  int ssb_idx = get_ssb_idx_from_list(ssb_list, idx);
                  ro_p->mapped_ssb_idx[ro_p->nb_mapped_ssb] = ssb_idx;
                  ro_p->nb_mapped_ssb++;
                  AssertFatal(MAX_NB_RO_PER_SSB_IN_ASSOCIATION_PATTERN > tx_ssb->nb_mapped_ro + 1,
                              "Too many mapped ROs (%d) to a single SSB\n",
                              tx_ssb->nb_mapped_ro);
                  tx_ssb->mapped_ro[tx_ssb->nb_mapped_ro] = ro_p;
                  tx_ssb->nb_mapped_ro++;
                  LOG_D(NR_MAC,
                        "Mapped ssb_idx %u to RO slot-symbol %u-%u, %u-%u-%u/%u\n"
                        "Nb mapped ROs for this ssb idx: in the association period only %u\n",
                        ssb_idx,
                        ro_p->slot,
                        ro_p->start_symbol,
                        slot,
                        ro_in_time,
                        ro_in_freq,
                        slot_map->nb_of_prach_occasion_in_freq,
                        tx_ssb->nb_mapped_ro);
                  // If all the required SSBs are mapped to this RO, exit the loop of SSBs
                  if (ro_p->nb_mapped_ssb == ssb_rach_ratio) {
                      idx++;
                      break;
                    }
                }
                done = MAX_NB_SSB == idx;
              }
            }
          }
        }
      }
    }
  } else {
    int frame = 0;
    int slot = 0;
    int ro_in_time = 0;
    int ro_in_freq = 0;
    prach_association_period_t *end = prach_assoc_pattern->prach_association_period_list + prach_assoc_pattern->nb_of_assoc_period;
    for (prach_association_period_t *prach_period = prach_assoc_pattern->prach_association_period_list; prach_period < end;
         prach_period++) {
      // Go through the list of transmitted SSBs
      for (int idx = 0; idx < ssb_list->nb_tx_ssb; idx++) {
        ssb_info_t *tx_ssb = ssb_list->tx_ssb + idx;
        uint8_t nb_mapped_ro_in_association_period = 0; // Reset the nb of mapped ROs for the new SSB index
        bool done = false;
        // Map all the required ROs to this SSB
        // Go through the list of PRACH config periods within this association period
        for (int i = 0; i < prach_period->nb_of_prach_conf_period && !done; i++, prach_configuration_period_idx++) {
          // Build the association period with its association PRACH Configuration indexes
          prach_period->prach_conf_period_list[i] = &prach_assoc_pattern->prach_conf_period_list[prach_configuration_period_idx];
	  prach_conf_period_t *prach_conf = prach_period->prach_conf_period_list[i];
          for (; frame < prach_conf->nb_of_frame; frame++) {
            for (; slot < prach_conf->nb_of_slot; slot++) {
              prach_occasion_slot_t *slot_map = &prach_conf->prach_occasion_slot_map[frame][slot];
              for (; ro_in_time < slot_map->nb_of_prach_occasion_in_time; ro_in_time++) {
                for (; ro_in_freq < slot_map->nb_of_prach_occasion_in_freq; ro_in_freq++) {
                  prach_occasion_info_t *ro_p =
                      slot_map->prach_occasion + ro_in_time * slot_map->nb_of_prach_occasion_in_freq + ro_in_freq;
                  int ssb_idx = get_ssb_idx_from_list(ssb_list, idx);
                  ro_p->mapped_ssb_idx[0] = ssb_idx;
                  ro_p->nb_mapped_ssb = 1;
                  AssertFatal(MAX_NB_RO_PER_SSB_IN_ASSOCIATION_PATTERN > tx_ssb->nb_mapped_ro + 1,
                              "Too many mapped ROs (%d) to a single SSB\n",
                              tx_ssb->nb_mapped_ro);
                  tx_ssb->mapped_ro[tx_ssb->nb_mapped_ro] = ro_p;
                  tx_ssb->nb_mapped_ro++;
                  nb_mapped_ro_in_association_period++;

                  LOG_D(NR_MAC,
                        "Mapped ssb_idx %u to RO slot-symbol %u-%u-%u, %u-%u-%u-%u/%u\n"
                        "Nb mapped ROs for this ssb idx: in the association period only %u / total %u\n",
                        ssb_idx,
                        ro_p->frame,
                        ro_p->slot,
                        ro_p->start_symbol,
                        frame,
                        slot,
                        ro_in_time,
                        ro_in_freq,
                        slot_map->nb_of_prach_occasion_in_freq,
                        tx_ssb->nb_mapped_ro,
                        nb_mapped_ro_in_association_period);

                  // Exit the loop if this SSB has been mapped to all the required ROs
                  // WIP: Assuming that ssb_rach_ratio equals the maximum nb of times a given ssb_idx is mapped within an
                  // association period:
                  //      this is true if no PRACH occasions are conflicting with SSBs nor TDD_UL_DL_ConfigurationCommon schedule
                  if (nb_mapped_ro_in_association_period == ssb_rach_ratio) {
                    ro_in_freq++;
                    break;
                  }
                }
                if (nb_mapped_ro_in_association_period == ssb_rach_ratio)
                  break;
                else
                  ro_in_freq = 0;
              }
              if (nb_mapped_ro_in_association_period == ssb_rach_ratio)
                break;
              else
                ro_in_time = 0;
            }
            if (nb_mapped_ro_in_association_period == ssb_rach_ratio)
              break;
            else
              slot = 0;
          }
          if (nb_mapped_ro_in_association_period == ssb_rach_ratio)
            break;
          else
            frame = 0;
        }
      }
    }
  }
}

// Returns a RACH occasion if any matches the SSB idx, the frame and the slot
static int get_nr_prach_info_from_ssb_index(prach_association_pattern_t *prach_assoc_pattern,
                                            int ssb_idx,
                                            int frame,
                                            int slot,
                                            ssb_list_info_t *ssb_list,
                                            prach_occasion_info_t **prach_occasion_info_pp)
{
  prach_occasion_slot_t *prach_occasion_slot_p = NULL;

  *prach_occasion_info_pp = NULL;

  // Search for a matching RO slot in the SSB_to_RO map
  // A valid RO slot will match:
  //      - ssb_idx mapped to one of the ROs in that RO slot
  //      - exact slot number
  //      - frame offset
  int idx_list = ssb_list->nb_ssb_per_index[ssb_idx];
  ssb_info_t *ssb_info_p = &ssb_list->tx_ssb[idx_list];
  LOG_D(NR_MAC, "checking for prach : ssb_info_p->nb_mapped_ro %d\n", ssb_info_p->nb_mapped_ro);
  for (int n_mapped_ro = 0; n_mapped_ro < ssb_info_p->nb_mapped_ro; n_mapped_ro++) {
    LOG_D(NR_MAC,
          "%d.%d: mapped_ro[%d]->frame.slot %d.%d, prach_assoc_pattern->nb_of_frame %d\n",
          frame,
          slot,
          n_mapped_ro,
          ssb_info_p->mapped_ro[n_mapped_ro]->frame,
          ssb_info_p->mapped_ro[n_mapped_ro]->slot,
          prach_assoc_pattern->nb_of_frame);
    if ((slot == ssb_info_p->mapped_ro[n_mapped_ro]->slot) && prach_assoc_pattern->prach_conf_period_list[0].nb_of_frame != 0 &&
        (ssb_info_p->mapped_ro[n_mapped_ro]->frame == (frame % prach_assoc_pattern->nb_of_frame))) {
      uint8_t prach_config_period_nb = ssb_info_p->mapped_ro[n_mapped_ro]->frame / prach_assoc_pattern->prach_conf_period_list[0].nb_of_frame;
      uint8_t frame_nb_in_prach_config_period = ssb_info_p->mapped_ro[n_mapped_ro]->frame % prach_assoc_pattern->prach_conf_period_list[0].nb_of_frame;
      prach_occasion_slot_p = &prach_assoc_pattern->prach_conf_period_list[prach_config_period_nb].prach_occasion_slot_map[frame_nb_in_prach_config_period][slot];
    }
  }

  // If there is a matching RO slot in the SSB_to_RO map
  if (NULL != prach_occasion_slot_p) {
    // A random RO mapped to the SSB index should be selected in the slot

    // First count the number of times the SSB index is found in that RO
    uint8_t nb_mapped_ssb = 0;

    for (int ro_in_time = 0; ro_in_time < prach_occasion_slot_p->nb_of_prach_occasion_in_time; ro_in_time++) {
      for (int ro_in_freq = 0; ro_in_freq < prach_occasion_slot_p->nb_of_prach_occasion_in_freq; ro_in_freq++) {
        prach_occasion_info_t *ro_p =
            prach_occasion_slot_p->prach_occasion + ro_in_time * prach_occasion_slot_p->nb_of_prach_occasion_in_freq + ro_in_freq;
        for (uint8_t ssb_nb = 0; ssb_nb < ro_p->nb_mapped_ssb; ssb_nb++) {
          if (ro_p->mapped_ssb_idx[ssb_nb] == ssb_idx) {
            nb_mapped_ssb++;
          }
        }
      }
    }

    // Choose a random SSB nb
    uint8_t random_ssb_nb = 0;

    random_ssb_nb = ((taus()) % nb_mapped_ssb);

    // Select the RO according to the chosen random SSB nb
    nb_mapped_ssb=0;
    for (int ro_in_time=0; ro_in_time < prach_occasion_slot_p->nb_of_prach_occasion_in_time; ro_in_time++) {
      for (int ro_in_freq=0; ro_in_freq < prach_occasion_slot_p->nb_of_prach_occasion_in_freq; ro_in_freq++) {
        prach_occasion_info_t *ro_p =
            prach_occasion_slot_p->prach_occasion + ro_in_time * prach_occasion_slot_p->nb_of_prach_occasion_in_freq + ro_in_freq;
        for (uint8_t ssb_nb = 0; ssb_nb < ro_p->nb_mapped_ssb; ssb_nb++) {
          if (ro_p->mapped_ssb_idx[ssb_nb] == ssb_idx) {
            if (nb_mapped_ssb == random_ssb_nb) {
              *prach_occasion_info_pp = ro_p;
              return 1;
            }
            else {
              nb_mapped_ssb++;
            }
          }
        }
      }
    }
  }

  return 0;
}

// Build the SSB to RO mapping upon RRC configuration update
void build_ssb_to_ro_map(NR_UE_MAC_INST_t *mac)
{
  // Clear all the lists and maps
  const int bwp_id = mac->current_UL_BWP->bwp_id;
  free_rach_structures(mac, bwp_id);
  memset(&mac->ssb_list[bwp_id], 0, sizeof(ssb_list_info_t));
  memset(&mac->prach_assoc_pattern[bwp_id], 0, sizeof(prach_association_pattern_t));

  // Build the list of all the valid RACH occasions in the maximum association pattern period according to the PRACH config
  LOG_D(NR_MAC,"Build RO list\n");
  build_ro_list(mac);

  // Build the list of all the valid/transmitted SSBs according to the config
  LOG_D(NR_MAC,"Build SSB list\n");
  build_ssb_list(mac);

  // Map the transmitted SSBs to the ROs and create the association pattern according to the config
  LOG_D(NR_MAC,"Map SSB to RO\n");
  map_ssb_to_ro(mac);
  LOG_D(NR_MAC,"Map SSB to RO done\n");
}

static bool schedule_uci_on_pusch(NR_UE_MAC_INST_t *mac,
                                  frame_t frame_tx,
                                  int slot_tx,
                                  const PUCCH_sched_t *pucch,
                                  NR_UE_UL_BWP_t *current_UL_BWP)
{
  fapi_nr_ul_config_request_pdu_t *ulcfg_pdu = lockGet_ul_iterator(mac, frame_tx, slot_tx);
  if (!ulcfg_pdu)
    return NULL;

  nfapi_nr_ue_pusch_pdu_t *pusch_pdu = NULL;
  while (ulcfg_pdu->pdu_type != FAPI_NR_END) {
    if (ulcfg_pdu->pdu_type == FAPI_NR_UL_CONFIG_TYPE_PUSCH) {
      int start_pusch = ulcfg_pdu->pusch_config_pdu.start_symbol_index;
      int nsymb_pusch = ulcfg_pdu->pusch_config_pdu.nr_of_symbols;
      int end_pusch = start_pusch + nsymb_pusch;
      NR_PUCCH_Resource_t *pucchres = pucch->pucch_resource;
      int nr_of_symbols = 0;
      int start_symbol_index = 0;
      if (pucch->initial_pucch_id > -1 && pucch->pucch_resource == NULL) {
        const int idx = *current_UL_BWP->pucch_ConfigCommon->pucch_ResourceCommon;
        const initial_pucch_resource_t pucch_resourcecommon = get_initial_pucch_resource(idx);
        start_symbol_index = pucch_resourcecommon.startingSymbolIndex;
        nr_of_symbols = pucch_resourcecommon.nrofSymbols;
      }
      else {
        switch(pucchres->format.present) {
          case NR_PUCCH_Resource__format_PR_format0 :
            nr_of_symbols = pucchres->format.choice.format0->nrofSymbols;
            start_symbol_index = pucchres->format.choice.format0->startingSymbolIndex;
            break;
          case NR_PUCCH_Resource__format_PR_format1 :
            nr_of_symbols = pucchres->format.choice.format1->nrofSymbols;
            start_symbol_index = pucchres->format.choice.format1->startingSymbolIndex;
            break;
          case NR_PUCCH_Resource__format_PR_format2 :
            nr_of_symbols = pucchres->format.choice.format2->nrofSymbols;
            start_symbol_index = pucchres->format.choice.format2->startingSymbolIndex;
            break;
          case NR_PUCCH_Resource__format_PR_format3 :
            nr_of_symbols = pucchres->format.choice.format3->nrofSymbols;
            start_symbol_index = pucchres->format.choice.format3->startingSymbolIndex;
            break;
          case NR_PUCCH_Resource__format_PR_format4 :
            nr_of_symbols = pucchres->format.choice.format4->nrofSymbols;
            start_symbol_index = pucchres->format.choice.format4->startingSymbolIndex;
            break;
          default :
            AssertFatal(false, "Undefined PUCCH format \n");
        }
      }
      int final_symbol = nr_of_symbols + start_symbol_index;
      // PUCCH overlapping in time with PUSCH
      if (start_symbol_index < end_pusch && final_symbol > start_pusch) {
        pusch_pdu = &ulcfg_pdu->pusch_config_pdu;
        break;
      }
    }
    ulcfg_pdu++;
  }

  if (!pusch_pdu) {
    release_ul_config(ulcfg_pdu, false);
    return false;
  }

  // If a UE would transmit on a serving cell a PUSCH without UL-SCH that overlaps with a PUCCH transmission
  // on a serving cell that includes positive SR information, the UE does not transmit the PUSCH.
  if (pusch_pdu && pusch_pdu->ulsch_indicator == 0 && pucch->sr_payload == 1) {
    release_ul_config(ulcfg_pdu, false);
    return false;
  }

  // - UE multiplexes only HARQ-ACK information, if any, from the UCI in the PUSCH transmission
  // and does not transmit the PUCCH if the UE multiplexes aperiodic or semi-persistent CSI reports in the PUSCH

  // - UE multiplexes only HARQ-ACK information and CSI reports, if any, from the UCI in the PUSCH transmission
  // and does not transmit the PUCCH if the UE does not multiplex aperiodic or semi-persistent CSI reports in the PUSCH
  bool mux_done = false;
  if (pucch->n_harq > 0) {
    NR_PUSCH_Config_t *pusch_Config = mac->current_UL_BWP->pusch_Config;
    if (pusch_Config && pusch_Config->uci_OnPUSCH) {
      NR_UCI_OnPUSCH_t *onPusch = pusch_Config->uci_OnPUSCH->choice.setup;
      AssertFatal(onPusch &&
                  onPusch->betaOffsets &&
                  onPusch->betaOffsets->present == NR_UCI_OnPUSCH__betaOffsets_PR_semiStatic,
                  "Only semistatic beta offset is supported\n");
      NR_BetaOffsets_t *beta_offsets = onPusch->betaOffsets->choice.semiStatic;
      pusch_pdu->pusch_uci.harq_ack_bit_length = pucch->n_harq;
      pusch_pdu->pusch_uci.harq_payload = pucch->ack_payload;
      pusch_pdu->pusch_uci.beta_offset_harq_ack = pucch->n_harq > 2 ?
                                                  (pucch->n_harq < 12 ? *beta_offsets->betaOffsetACK_Index2 :
                                                  *beta_offsets->betaOffsetACK_Index3) :
                                                  *beta_offsets->betaOffsetACK_Index1;
      pusch_pdu->pusch_uci.alpha_scaling = onPusch->scaling;
      mux_done = true;
    } else {
      LOG_E(NR_MAC, "UCI on PUSCH need to be configured to schedule UCI on PUSCH\n");
    }
  }
  if (pusch_pdu->pusch_uci.csi_part1_bit_length == 0 && pusch_pdu->pusch_uci.csi_part2_bit_length == 0) {
    // To support this we would need to shift some bits into CSI part2 -> need to change the logic
    AssertFatal(pucch->n_csi == 0, "Multiplexing periodic CSI on PUSCH not supported\n");
  }

  release_ul_config(ulcfg_pdu, false);
  // only use PUSCH if any mux is done otherwise send PUCCH
  return mux_done;
}

void nr_ue_pucch_scheduler(NR_UE_MAC_INST_t *mac, frame_t frameP, int slotP)
{
  PUCCH_sched_t pucch[3] = {0}; // TODO the size might change in the future in case of multiple SR or multiple CSI in a slot

  mac->nr_ue_emul_l1.num_srs = 0;
  mac->nr_ue_emul_l1.num_harqs = 0;
  mac->nr_ue_emul_l1.num_csi_reports = 0;
  int num_res = 0;

  // SR
  if (mac->state == UE_CONNECTED && trigger_periodic_scheduling_request(mac, &pucch[0], frameP, slotP)) {
    num_res++;
    // TODO check if the PUCCH resource for the SR transmission occasion overlap with a UL-SCH resource
  }

  // CSI
  int csi_res = 0;
  if (mac->state == UE_CONNECTED)
    csi_res = nr_get_csi_measurements(mac, frameP, slotP, &pucch[num_res]);
  if (csi_res > 0) {
    num_res += csi_res;
  }

  // ACKNACK
  bool any_harq = get_downlink_ack(mac, frameP, slotP, &pucch[num_res]);
  if (any_harq)
    num_res++;

  if (num_res == 0)
    return;
  // do no transmit pucch if only SR scheduled and it is negative
  if (num_res == 1 && pucch[0].n_sr > 0 && pucch[0].sr_payload == 0)
    return;

  if (num_res > 1)
    multiplex_pucch_resource(mac, pucch, num_res);

  for (int j = 0; j < num_res; j++) {
    if (pucch[j].n_harq + pucch[j].n_sr + pucch[j].n_csi != 0) {
      LOG_D(NR_MAC,
            "%d.%d configure pucch, O_ACK %d, O_SR %d, O_CSI %d\n",
            frameP,
            slotP,
            pucch[j].n_harq,
            pucch[j].n_sr,
            pucch[j].n_csi);
      mac->nr_ue_emul_l1.num_srs = pucch[j].n_sr;
      mac->nr_ue_emul_l1.num_harqs = pucch[j].n_harq;
      mac->nr_ue_emul_l1.num_csi_reports = pucch[j].n_csi;

      // checking if we need to schedule pucch[j] on PUSCH
      if (schedule_uci_on_pusch(mac, frameP, slotP, &pucch[j], mac->current_UL_BWP))
        continue;

      fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frameP, slotP, FAPI_NR_UL_CONFIG_TYPE_PUCCH);
      if (!pdu) {
        LOG_E(NR_MAC, "Error in pucch allocation\n");
        return;
      }
      mac->nr_ue_emul_l1.active_uci_sfn_slot = NFAPI_SFNSLOT2HEX(frameP, slotP);
      int ret = nr_ue_configure_pucch(mac,
                                      slotP,
                                      frameP,
                                      mac->crnti, // FIXME not sure this is valid for all pucch instances
                                      &pucch[j],
                                      &pdu->pucch_config_pdu);
      if (ret != 0)
        remove_ul_config_last_item(pdu);
      release_ul_config(pdu, false);
    }
  }
}

void nr_schedule_csi_for_im(NR_UE_MAC_INST_t *mac, int frame, int slot)
{
  if (!mac->sc_info.csi_MeasConfig)
    return;

  NR_CSI_MeasConfig_t *csi_measconfig = mac->sc_info.csi_MeasConfig;

  if (csi_measconfig->csi_IM_ResourceToAddModList == NULL)
    return;

  fapi_nr_dl_config_request_t *dl_config = get_dl_config_request(mac, slot);
  NR_CSI_IM_Resource_t *imcsi;
  int period, offset;

  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  int mu = current_DL_BWP->scs;
  uint16_t bwp_size = current_DL_BWP->BWPSize;
  uint16_t bwp_start = current_DL_BWP->BWPStart;

  for (int id = 0; id < csi_measconfig->csi_IM_ResourceToAddModList->list.count; id++){
    imcsi = csi_measconfig->csi_IM_ResourceToAddModList->list.array[id];
    csi_period_offset(NULL,imcsi->periodicityAndOffset,&period,&offset);
    if((frame*nr_slots_per_frame[mu]+slot-offset)%period != 0)
      continue;
    fapi_nr_dl_config_csiim_pdu_rel15_t *csiim_config_pdu = &dl_config->dl_config_list[dl_config->number_pdus].csiim_config_pdu.csiim_config_rel15;
    csiim_config_pdu->bwp_size = bwp_size;
    csiim_config_pdu->bwp_start = bwp_start;
    csiim_config_pdu->subcarrier_spacing = mu;
    csiim_config_pdu->start_rb = imcsi->freqBand->startingRB;
    csiim_config_pdu->nr_of_rbs = imcsi->freqBand->nrofRBs;
    // As specified in 5.2.2.4 of 38.214
    switch (imcsi->csi_IM_ResourceElementPattern->present) {
      case NR_CSI_IM_Resource__csi_IM_ResourceElementPattern_PR_pattern0:
        for (int i = 0; i < 4; i++) {
          csiim_config_pdu->k_csiim[i] =
              (imcsi->csi_IM_ResourceElementPattern->choice.pattern0->subcarrierLocation_p0 << 1) + (i >> 1);
          csiim_config_pdu->l_csiim[i] = imcsi->csi_IM_ResourceElementPattern->choice.pattern0->symbolLocation_p0 + (i % 2);
        }
        break;
      case NR_CSI_IM_Resource__csi_IM_ResourceElementPattern_PR_pattern1:
        for (int i = 0; i < 4; i++) {
          csiim_config_pdu->k_csiim[i] = (imcsi->csi_IM_ResourceElementPattern->choice.pattern1->subcarrierLocation_p1 << 2) + i;
          csiim_config_pdu->l_csiim[i] = imcsi->csi_IM_ResourceElementPattern->choice.pattern1->symbolLocation_p1;
        }
        break;
      default:
        AssertFatal(1 == 0, "Invalid CSI-IM pattern\n");
    }
    dl_config->dl_config_list[dl_config->number_pdus].pdu_type = FAPI_NR_DL_CONFIG_TYPE_CSI_IM;
    dl_config->number_pdus += 1;
  }
}

NR_CSI_ResourceConfigId_t find_CSI_resourceconfig(NR_CSI_MeasConfig_t *csi_measconfig,
                                                  NR_BWP_Id_t dl_bwp_id,
                                                  NR_NZP_CSI_RS_ResourceId_t csi_id)
{
  bool found = false;
  for (int csi_list = 0; csi_list < csi_measconfig->csi_ResourceConfigToAddModList->list.count; csi_list++) {
    NR_CSI_ResourceConfig_t *csires = csi_measconfig->csi_ResourceConfigToAddModList->list.array[csi_list];
    if(csires->bwp_Id != dl_bwp_id)
      continue;
    struct NR_CSI_ResourceConfig__csi_RS_ResourceSetList *resset = &csires->csi_RS_ResourceSetList;
    if(resset->present != NR_CSI_ResourceConfig__csi_RS_ResourceSetList_PR_nzp_CSI_RS_SSB)
      continue;
    if(!resset->choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList)
      continue;
    for(int i = 0; i < resset->choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList->list.count; i++) {
      NR_NZP_CSI_RS_ResourceSetId_t *res_id = resset->choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList->list.array[i];
      AssertFatal(res_id, "NR_NZP_CSI_RS_ResourceSetId shouldn't be NULL\n");
      struct NR_CSI_MeasConfig__nzp_CSI_RS_ResourceSetToAddModList *res_list = csi_measconfig->nzp_CSI_RS_ResourceSetToAddModList;
      AssertFatal(res_list, "nzp_CSI_RS_ResourceSetToAddModList shouldn't be NULL\n");
      for (int j = 0; j < res_list->list.count; j++) {
        NR_NZP_CSI_RS_ResourceSet_t *csi_res = res_list->list.array[j];
        if (*res_id != csi_res->nzp_CSI_ResourceSetId)
          continue;
        for (int k = 0; k < csi_res->nzp_CSI_RS_Resources.list.count; k++) {
          AssertFatal(csi_res->nzp_CSI_RS_Resources.list.array[k], "NZP_CSI_RS_ResourceId shoulan't be NULL\n");
          if (csi_id == *csi_res->nzp_CSI_RS_Resources.list.array[k]) {
            found = true;
            break;
          }
        }
        if (found && csi_res->trs_Info)
          // CRI-RS for Tracking (not implemented yet)
          // in this case we there is no associated CSI report
          // therefore to signal this we return a value higher than
          // maxNrofCSI-ResourceConfigurations
          return NR_maxNrofCSI_ResourceConfigurations + 1;
        else if (found)
          return csires->csi_ResourceConfigId;
      }
    }
  }
  return -1; // not found any CSI-resource in current DL BWP associated with this CSI-RS ID
}

uint8_t set_csirs_measurement_bitmap(NR_CSI_MeasConfig_t *csi_measconfig, NR_CSI_ResourceConfigId_t csi_res_id)
{
  uint8_t meas_bitmap = 0;
  if (csi_res_id > NR_maxNrofCSI_ResourceConfigurations)
    return meas_bitmap; // CSI-RS for tracking
  for(int i = 0; i < csi_measconfig->csi_ReportConfigToAddModList->list.count; i++) {
    NR_CSI_ReportConfig_t *report_config = csi_measconfig->csi_ReportConfigToAddModList->list.array[i];
    if(report_config->resourcesForChannelMeasurement != csi_res_id)
      continue;
    // bit 0 RSRP bit 1 RI bit 2 LI bit 3 PMI bit 4 CQI bit 5 i1
    switch (report_config->reportQuantity.present) {
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_PMI_CQI:
        meas_bitmap += (1 << 1) + (1 << 3) + (1 << 4);
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_i1:
        meas_bitmap += (1 << 1) + (1 << 5);
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_i1_CQI:
        meas_bitmap += (1 << 1) + (1 << 4) + (1 << 5);
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_CQI:
        meas_bitmap += (1 << 1) + (1 << 4);
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RSRP:
        meas_bitmap += 1;
        break;
      case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_LI_PMI_CQI:
        meas_bitmap += (1 << 1) + (1 << 2) + (1 << 3) + (1 << 4);
        break;
      default:
        AssertFatal(false, "Unexpected measurement report type %d\n", report_config->reportQuantity.present);
    }
  }
  AssertFatal(meas_bitmap > 0, "Expected to have at least 1 measurement configured for CSI-RS\n");
  return meas_bitmap;
}

void configure_csi_resource_mapping(fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu,
                                    NR_CSI_RS_ResourceMapping_t  *resourceMapping,
                                    uint32_t bwp_size,
                                    uint32_t bwp_start)
{
  // According to last paragraph of TS 38.214 5.2.2.3.1
  if (resourceMapping->freqBand.startingRB < bwp_start)
    csirs_config_pdu->start_rb = bwp_start;
  else
    csirs_config_pdu->start_rb = resourceMapping->freqBand.startingRB;
  if (resourceMapping->freqBand.nrofRBs > (bwp_start + bwp_size - csirs_config_pdu->start_rb))
    csirs_config_pdu->nr_of_rbs = bwp_start + bwp_size - csirs_config_pdu->start_rb;
  else
    csirs_config_pdu->nr_of_rbs = resourceMapping->freqBand.nrofRBs;
  AssertFatal(csirs_config_pdu->nr_of_rbs >= 24, "CSI-RS has %d RBs, but the minimum is 24\n", csirs_config_pdu->nr_of_rbs);

  csirs_config_pdu->symb_l0 = resourceMapping->firstOFDMSymbolInTimeDomain;
  if (resourceMapping->firstOFDMSymbolInTimeDomain2)
    csirs_config_pdu->symb_l1 = *resourceMapping->firstOFDMSymbolInTimeDomain2;
  csirs_config_pdu->cdm_type = resourceMapping->cdm_Type;

  csirs_config_pdu->freq_density = resourceMapping->density.present;
  if ((resourceMapping->density.present == NR_CSI_RS_ResourceMapping__density_PR_dot5)
      && (resourceMapping->density.choice.dot5 == NR_CSI_RS_ResourceMapping__density__dot5_evenPRBs))
    csirs_config_pdu->freq_density--;

  switch(resourceMapping->frequencyDomainAllocation.present){
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row1:
      csirs_config_pdu->row = 1;
      csirs_config_pdu->freq_domain = ((resourceMapping->frequencyDomainAllocation.choice.row1.buf[0]) >> 4) & 0x0f;
      break;
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row2:
      csirs_config_pdu->row = 2;
      csirs_config_pdu->freq_domain = (((resourceMapping->frequencyDomainAllocation.choice.row2.buf[1] >> 4) & 0x0f)
                                       | ((resourceMapping->frequencyDomainAllocation.choice.row2.buf[0] << 4) & 0xff0));
      break;
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_row4:
      csirs_config_pdu->row = 4;
      csirs_config_pdu->freq_domain = ((resourceMapping->frequencyDomainAllocation.choice.row4.buf[0]) >> 5) & 0x07;
      break;
    case NR_CSI_RS_ResourceMapping__frequencyDomainAllocation_PR_other:
      csirs_config_pdu->freq_domain = ((resourceMapping->frequencyDomainAllocation.choice.other.buf[0]) >> 2) & 0x3f;
      // determining the row of table 7.4.1.5.3-1 in 38.211
      switch (resourceMapping->nrofPorts) {
        case NR_CSI_RS_ResourceMapping__nrofPorts_p1:
          AssertFatal(1 == 0, "Resource with 1 CSI port shouldn't be within other rows\n");
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p2:
          csirs_config_pdu->row = 3;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p4:
          csirs_config_pdu->row = 5;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p8:
          if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            csirs_config_pdu->row = 8;
          else {
            int num_k = 0;
            for (int k = 0; k < 6; k++)
              num_k += (((csirs_config_pdu->freq_domain) >> k) & 0x01);
            if (num_k == 4)
              csirs_config_pdu->row = 6;
            else
              csirs_config_pdu->row = 7;
          }
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p12:
          if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            csirs_config_pdu->row = 10;
          else
            csirs_config_pdu->row = 9;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p16:
          if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            csirs_config_pdu->row = 12;
          else
            csirs_config_pdu->row = 11;
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p24:
          if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            csirs_config_pdu->row = 14;
          else {
            if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm8_FD2_TD4)
              csirs_config_pdu->row = 15;
            else
              csirs_config_pdu->row = 13;
          }
          break;
        case NR_CSI_RS_ResourceMapping__nrofPorts_p32:
          if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm4_FD2_TD2)
            csirs_config_pdu->row = 17;
          else {
            if (resourceMapping->cdm_Type == NR_CSI_RS_ResourceMapping__cdm_Type_cdm8_FD2_TD4)
              csirs_config_pdu->row = 18;
            else
              csirs_config_pdu->row = 16;
          }
          break;
        default:
          AssertFatal(false, "Invalid number of ports in CSI-RS resource\n");
      }
      break;
    default:
      AssertFatal(false, "Invalid freqency domain allocation in CSI-RS resource\n");
  }
}

void nr_schedule_csirs_reception(NR_UE_MAC_INST_t *mac, int frame, int slot)
{
  if (!mac->sc_info.csi_MeasConfig)
    return;

  NR_CSI_MeasConfig_t *csi_measconfig = mac->sc_info.csi_MeasConfig;

  if (csi_measconfig->nzp_CSI_RS_ResourceToAddModList == NULL)
    return;

  fapi_nr_dl_config_request_t *dl_config = get_dl_config_request(mac, slot);
  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  NR_BWP_Id_t dl_bwp_id = current_DL_BWP->bwp_id;

  int mu = current_DL_BWP->scs;
  uint16_t bwp_size = current_DL_BWP->BWPSize;
  uint16_t bwp_start = current_DL_BWP->BWPStart;

  for (int id = 0; id < csi_measconfig->nzp_CSI_RS_ResourceToAddModList->list.count; id++) {
    NR_NZP_CSI_RS_Resource_t *nzpcsi = csi_measconfig->nzp_CSI_RS_ResourceToAddModList->list.array[id];
    int period, offset;
    csi_period_offset(NULL, nzpcsi->periodicityAndOffset, &period, &offset);
    if((frame * nr_slots_per_frame[mu] + slot-offset) % period != 0)
      continue;
    NR_CSI_ResourceConfigId_t csi_res_id = find_CSI_resourceconfig(csi_measconfig, dl_bwp_id, nzpcsi->nzp_CSI_RS_ResourceId);
    // do not schedule reseption of this CSI-RS if not associated with current BWP
    if(csi_res_id < 0)
      continue;
    LOG_D(MAC,"Scheduling reception of CSI-RS in frame %d slot %d\n", frame, slot);
    fapi_nr_dl_config_csirs_pdu_rel15_t *csirs_config_pdu = &dl_config->dl_config_list[dl_config->number_pdus].csirs_config_pdu.csirs_config_rel15;
    csirs_config_pdu->measurement_bitmap = set_csirs_measurement_bitmap(csi_measconfig, csi_res_id);
    csirs_config_pdu->subcarrier_spacing = mu;
    csirs_config_pdu->cyclic_prefix = current_DL_BWP->cyclicprefix ? *current_DL_BWP->cyclicprefix : 0;

    if (csi_res_id > NR_maxNrofCSI_ResourceConfigurations)
      csirs_config_pdu->csi_type = 0; // TRS
    else
      csirs_config_pdu->csi_type = 1; // NZP-CSI-RS

    csirs_config_pdu->scramb_id = nzpcsi->scramblingID;
    csirs_config_pdu->power_control_offset = nzpcsi->powerControlOffset + 8;
    if (nzpcsi->powerControlOffsetSS)
      csirs_config_pdu->power_control_offset_ss = *nzpcsi->powerControlOffsetSS;
    else
      csirs_config_pdu->power_control_offset_ss = 1; // 0 dB

    configure_csi_resource_mapping(csirs_config_pdu, &nzpcsi->resourceMapping, bwp_size, bwp_start);

    dl_config->dl_config_list[dl_config->number_pdus].pdu_type = FAPI_NR_DL_CONFIG_TYPE_CSI_RS;
    dl_config->number_pdus += 1;
  }
}

// This function schedules the PRACH according to prach_ConfigurationIndex and TS 38.211, tables 6.3.3.2.x
// PRACH formats 9, 10, 11 are corresponding to dual PRACH format configurations A1/B1, A2/B2, A3/B3.
// - todo:
// - Partial configuration is actually already stored in (fapi_nr_prach_config_t) &mac->phy_config.config_req->prach_config
static void nr_ue_prach_scheduler(NR_UE_MAC_INST_t *mac, frame_t frameP, sub_frame_t slotP)
{
  RA_config_t *ra = &mac->ra;
  ra->RA_offset = 2; // to compensate the rx frame offset at the gNB
  if (ra->ra_state != nrRA_GENERATE_PREAMBLE)
    return;

  fapi_nr_config_request_t *cfg = &mac->phy_config.config_req;
  fapi_nr_prach_config_t *prach_config = &cfg->prach_config;

  NR_RACH_ConfigCommon_t *setup = mac->current_UL_BWP->rach_ConfigCommon;
  NR_RACH_ConfigGeneric_t *rach_ConfigGeneric = &setup->rach_ConfigGeneric;
  const int bwp_id = mac->current_UL_BWP->bwp_id;
  NR_TDD_UL_DL_ConfigCommon_t *tdd_config = mac->tdd_UL_DL_ConfigurationCommon;

  if (is_nr_UL_slot(tdd_config, slotP, mac->frame_type)) {

    // WIP Need to get the proper selected ssb_idx
    //     Initial beam selection functionality is not available yet
    uint8_t selected_gnb_ssb_idx = mac->mib_ssb;

    // Get any valid PRACH occasion in the current slot for the selected SSB index
    prach_occasion_info_t *prach_occasion_info_p;
    int is_nr_prach_slot = get_nr_prach_info_from_ssb_index(&mac->prach_assoc_pattern[bwp_id],
                                                            selected_gnb_ssb_idx,
                                                            (int)frameP,
                                                            (int)slotP,
                                                            &mac->ssb_list[bwp_id],
                                                            &prach_occasion_info_p);

    if (is_nr_prach_slot) {
      AssertFatal(NULL != prach_occasion_info_p,"PRACH Occasion Info not returned in a valid NR Prach Slot\n");

      nr_get_RA_window(mac);

      uint16_t format = prach_occasion_info_p->format;
      uint16_t format0 = format & 0xff;        // single PRACH format
      uint16_t format1 = (format >> 8) & 0xff; // dual PRACH format

      fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frameP, slotP, FAPI_NR_UL_CONFIG_TYPE_PRACH);
      if (!pdu) {
        LOG_E(NR_MAC, "Error in PRACH allocation\n");
        return;
      }
      uint16_t ncs = get_NCS(rach_ConfigGeneric->zeroCorrelationZoneConfig, format0, setup->restrictedSetConfig);
      pdu->prach_config_pdu = (fapi_nr_ul_config_prach_pdu){
          .phys_cell_id = mac->physCellId,
          .num_prach_ocas = 1,
          .prach_slot = prach_occasion_info_p->slot,
          .prach_start_symbol = prach_occasion_info_p->start_symbol,
          .num_ra = prach_occasion_info_p->fdm,
          .num_cs = ncs,
          .root_seq_id = prach_config->num_prach_fd_occasions_list[prach_occasion_info_p->fdm].prach_root_sequence_index,
          .restricted_set = prach_config->restricted_set_config,
          .freq_msg1 = prach_config->num_prach_fd_occasions_list[prach_occasion_info_p->fdm].k1};

      LOG_I(NR_MAC,
            "PRACH scheduler: Selected RO Frame %u, Slot %u, Symbol %u, Fdm %u\n",
            frameP,
            pdu->prach_config_pdu.prach_slot,
            pdu->prach_config_pdu.prach_start_symbol,
            pdu->prach_config_pdu.num_ra);

      // Search which SSB is mapped in the RO (among all the SSBs mapped to this RO)
      for (int ssb_nb_in_ro=0; ssb_nb_in_ro<prach_occasion_info_p->nb_mapped_ssb; ssb_nb_in_ro++) {
        if (prach_occasion_info_p->mapped_ssb_idx[ssb_nb_in_ro] == selected_gnb_ssb_idx) {
          ra->ssb_nb_in_ro = ssb_nb_in_ro;
          break;
        }
      }
      AssertFatal(ra->ssb_nb_in_ro<prach_occasion_info_p->nb_mapped_ssb, "%u not found in the mapped SSBs to the PRACH occasion", selected_gnb_ssb_idx);

      if (format1 != 0xff) {
        switch (format0) { // dual PRACH format
          case 0xa1:
            pdu->prach_config_pdu.prach_format = 11;
            break;
          case 0xa2:
            pdu->prach_config_pdu.prach_format = 12;
            break;
          case 0xa3:
            pdu->prach_config_pdu.prach_format = 13;
            break;
          default:
            AssertFatal(1 == 0, "Only formats A1/B1 A2/B2 A3/B3 are valid for dual format");
        }
      } else {
        switch (format0) { // single PRACH format
          case 0:
            pdu->prach_config_pdu.prach_format = 0;
            break;
          case 1:
            pdu->prach_config_pdu.prach_format = 1;
            break;
          case 2:
            pdu->prach_config_pdu.prach_format = 2;
            break;
          case 3:
            pdu->prach_config_pdu.prach_format = 3;
            break;
          case 0xa1:
            pdu->prach_config_pdu.prach_format = 4;
            break;
          case 0xa2:
            pdu->prach_config_pdu.prach_format = 5;
            break;
          case 0xa3:
            pdu->prach_config_pdu.prach_format = 6;
            break;
          case 0xb1:
            pdu->prach_config_pdu.prach_format = 7;
            break;
          case 0xb4:
            pdu->prach_config_pdu.prach_format = 8;
            break;
          case 0xc0:
            pdu->prach_config_pdu.prach_format = 9;
            break;
          case 0xc2:
            pdu->prach_config_pdu.prach_format = 10;
            break;
          default:
            AssertFatal(1 == 0, "Invalid PRACH format");
        }
      } // if format1

      nr_get_prach_resources(mac, 0, 0, &ra->prach_resources, ra->rach_ConfigDedicated);
      pdu->prach_config_pdu.ra_PreambleIndex = ra->ra_PreambleIndex;
      pdu->prach_config_pdu.prach_tx_power = get_prach_tx_power(mac);
      mac->ra.ra_rnti = nr_get_ra_rnti(pdu->prach_config_pdu.prach_start_symbol,
                                       pdu->prach_config_pdu.prach_slot,
                                       pdu->prach_config_pdu.num_ra,
                                       0);
      release_ul_config(pdu, false);
      nr_scheduled_response_t scheduled_response = {.ul_config = mac->ul_config_request + slotP,
                                                    .mac = mac,
                                                    .module_id = mac->ue_id,
                                                    .CC_id = 0 /*TBR fix*/};
      if(mac->if_module != NULL && mac->if_module->scheduled_response != NULL)
        mac->if_module->scheduled_response(&scheduled_response);

      if (ra->ra_type == RA_4_STEP) {
        nr_Msg1_transmitted(mac);
      } else if (ra->ra_type == RA_2_STEP) {
        NR_MsgA_PUSCH_Resource_r16_t *msgA_PUSCH_Resource =
            mac->current_UL_BWP->msgA_ConfigCommon_r16->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16;
        int mu = nr_get_prach_mu(mac->current_UL_BWP->msgA_ConfigCommon_r16, setup);
        const int n_slots_frame = nr_slots_per_frame[mu];
        slot_t msgA_pusch_slot = (slotP + msgA_PUSCH_Resource->msgA_PUSCH_TimeDomainOffset_r16) % n_slots_frame;
        frame_t msgA_pusch_frame =
            (frameP + ((slotP + msgA_PUSCH_Resource->msgA_PUSCH_TimeDomainOffset_r16) / n_slots_frame)) % 1024;

        fapi_nr_ul_config_request_pdu_t *pdu =
            lockGet_ul_config(mac, msgA_pusch_frame, msgA_pusch_slot, FAPI_NR_UL_CONFIG_TYPE_PUSCH);
        if (!pdu)
          return;

        // Config Msg3/MsgA-PUSCH PDU
        NR_tda_info_t tda_info = {0};
        AssertFatal(msgA_PUSCH_Resource->startSymbolAndLengthMsgA_PO_r16,
                    "Only SLIV based on startSymbolAndLengthMsgA_PO_r16 implemented\n");

        int ret = nr_config_pusch_pdu(mac,
                                      &tda_info,
                                      &pdu->pusch_config_pdu,
                                      NULL,
                                      NULL,
                                      NULL,
                                      mac->ra.ra_rnti,
                                      NR_SearchSpace__searchSpaceType_PR_common,
                                      NR_DCI_NONE);

        if (ret != 0)
          remove_ul_config_last_item(pdu);
        release_ul_config(pdu, false);

        // Compute MsgB RNTI
        ra->MsgB_rnti =
            nr_get_MsgB_rnti(prach_occasion_info_p->start_symbol, prach_occasion_info_p->slot, prach_occasion_info_p->fdm, 0);
        LOG_D(NR_MAC, "ra->ra_state %s\n", nrra_ue_text[ra->ra_state]);
        ra->ra_state = nrRA_WAIT_MSGB;
        ra->t_crnti = 0;
        mac->crnti = 0;
        ra->ra_rnti = 0;
      } else {
        AssertFatal(false, "RA type %d not implemented!\n", ra->ra_type);
      }

      // rnti = ra->t_crnti;
    } // is_nr_prach_slot
  } // if is_nr_UL_slot
}

typedef struct {
  uint8_t phr_len;
  uint sdu_length_total;
  NR_SINGLE_ENTRY_PHR_MAC_CE phr;
  type_bsr_t bsr;
  uint8_t *pdu_end;
  uint8_t *end_for_tailer;
  uint8_t *cur_ptr;
  uint num_sdus;
  // int highest_priority = 16;
  //  variable used to store the lcid data status during lcp
  bool lcids_data_status[NR_MAX_NUM_LCID];
  uint32_t lcp_allocation_counter;
} NR_UE_MAC_CE_INFO;

static void nr_ue_get_sdu_mac_ce_pre(NR_UE_MAC_INST_t *mac,
                                     int CC_id,
                                     frame_t frameP,
                                     sub_frame_t subframe,
                                     uint8_t gNB_index,
                                     uint8_t *ulsch_buffer,
                                     uint32_t buflen,
                                     uint32_t *LCG_bytes,
                                     NR_UE_MAC_CE_INFO *mac_ce_p,
                                     int tx_power,
                                     int P_CMAX)
{
  // Preparing the MAC CEs sub-PDUs and get the total size
  *mac_ce_p = (NR_UE_MAC_CE_INFO){.pdu_end = ulsch_buffer + buflen, .cur_ptr = ulsch_buffer};
  memset(mac_ce_p->lcids_data_status, 1, NR_MAX_NUM_LCID);

  // Compute BSR Length if Regular or Periodic BSR is triggered
  // If we decide here to send BSR long, we may decide later to use BSR short
  // if there remains less than 1 LCGROUP with data after Tx
  int bsr_len = 0;
  if (mac->scheduling_info.BSR_reporting_active) {
    // count the active channels
    int num_lcg_id_with_data = 0;
    for (int i = 0; i < NR_MAX_NUM_LCGID; i++) {
      if (LCG_bytes[i])
        num_lcg_id_with_data++;
    }
    // A Regular or Periodic BSR can only be sent if TBS is sufficient
    // as transmitting only a BSR is not allowed if UE has data to transmit
    int size_for_long = num_lcg_id_with_data + sizeof(NR_BSR_LONG) + sizeof(NR_MAC_SUBHEADER_SHORT);
    int size_for_short = sizeof(NR_BSR_SHORT) + sizeof(NR_MAC_SUBHEADER_FIXED);
    if (num_lcg_id_with_data > 1 && buflen >= size_for_long)
      bsr_len = size_for_long;
    else if ((num_lcg_id_with_data > 0 && buflen >= size_for_short)
             || mac->scheduling_info.BSR_reporting_active & NR_BSR_TRIGGER_PERIODIC)
      bsr_len = size_for_short;
  }
  nr_phr_info_t *phr_info = &mac->scheduling_info.phr_info;
  if (phr_info->is_configured && phr_info->phr_reporting > 0) {
    int needed = sizeof(NR_MAC_SUBHEADER_FIXED) + sizeof(NR_SINGLE_ENTRY_PHR_MAC_CE);
    if (buflen >= bsr_len + needed) {
      if (mac->scheduling_info.phr_info.phr_reporting) {
        mac_ce_p->phr_len = needed;
        nr_ue_fill_phr(mac, &mac_ce_p->phr, P_CMAX, tx_power, frameP, subframe);
      }
    }
  }
  mac_ce_p->end_for_tailer = mac_ce_p->pdu_end - bsr_len - mac_ce_p->phr_len;
}

/*
  nr_ue_get_sdu_mac_ce_post recalculates length and prepares the mac_ce field
  Need the following from mac_ce_p:
  bsr_ce_len
  bsr_len
  Update the following in mac_ce_p:
  bsr_ce_len
  bsr_header_len
  bsr_len
  tot_mac_ce_len
  bsr_s
  bsr_l
  bsr_t
*/
static void nr_ue_get_sdu_mac_ce_post(NR_UE_MAC_INST_t *mac,
                                      frame_t frame,
                                      slot_t slot,
                                      NR_UE_MAC_CE_INFO *mac_ce_p,
                                      uint32_t *LCG_bytes,
                                      bool *BSRsent)
{
  // Compute BSR Values and update Nb LCGID with data after multiplexing
  int lcg_id_bsr_max = 0;
  NR_UE_SCHEDULING_INFO *sched_info = &mac->scheduling_info;
  // Search the LCGID within waiting data for the highest priority LC
  for (int i = 0; i < mac->lc_ordered_list.count; i++) {
    int lcid = mac->lc_ordered_list.array[i]->lcid;
    NR_LC_SCHEDULING_INFO *sched_info = get_scheduling_info_from_lcid(mac, lcid);
    if (sched_info->LCID_buffer_remain) {
      // this is the highest priority LC with data to transmit
      lcg_id_bsr_max = sched_info->LCGID;
      break;
    }
  }

  // TS 38.321 Section 5.4.5
  // Check BSR padding: it is done after PHR according to Logical Channel Prioritization order
  // Check for max padding size, ie MAC Hdr for last RLC PDU = 1
  /* For Padding BSR:
     -  if the number of padding bits is equal to or larger than the size of the Short BSR plus its subheader but smaller than the
     size of the Long BSR plus its subheader:
     -  if more than one LCG has data available for transmission in the TTI where the BSR is transmitted: report Truncated BSR of
     the LCG with the highest priority logical channel with data available for transmission;
     -  else report Short BSR.
     -  else if the number of padding bits is equal to or larger than the size of the Long BSR plus its subheader, report Long BSR.
  */
  uint32_t padding_len = mac_ce_p->pdu_end - mac_ce_p->cur_ptr - mac_ce_p->phr_len;
  AssertFatal(padding_len >= 0, "Error in buffer size compute");
  mac_ce_p->bsr.type_bsr = b_none;
  int short_bsr_sz = sizeof(NR_BSR_SHORT) + sizeof(NR_MAC_SUBHEADER_FIXED);
  int num_lcg_id_with_data = 0;
  for (int lcg_id = 0; lcg_id < NR_MAX_NUM_LCGID; lcg_id++)
    num_lcg_id_with_data += LCG_bytes > 0;
  int long_bsr_sz = num_lcg_id_with_data + sizeof(NR_BSR_LONG) + sizeof(NR_MAC_SUBHEADER_SHORT);
  if (num_lcg_id_with_data < 2 && padding_len >= short_bsr_sz) {
    mac_ce_p->bsr.type_bsr = b_short;
    mac_ce_p->bsr.bsr.s.LcgID = lcg_id_bsr_max;
    mac_ce_p->bsr.bsr.s.Buffer_size =
        nr_locate_BsrIndexByBufferSize(NR_SHORT_BSR_TABLE, NR_SHORT_BSR_TABLE_SIZE, LCG_bytes[lcg_id_bsr_max]);
    LOG_D(NR_MAC,
          "[UE %d] sfn %d.%d BSR Trigger=0x%x report SHORT BSR with level %d for LCGID %d\n",
          mac->ue_id,
          frame,
          slot,
          sched_info->BSR_reporting_active,
          mac_ce_p->bsr.bsr.s.Buffer_size,
          lcg_id_bsr_max);
  } else if (padding_len >= long_bsr_sz) {
    /* if the number of padding bits is equal to or larger than the size of the Long BSR plus its subheader,
       report Long BSR whatever periodic or regular BSR*/
    mac_ce_p->bsr.type_bsr = b_long;
    uint8_t *tmp = mac_ce_p->bsr.bsr.lcg_bsr;
    for (int lcg_id = 0; lcg_id < 8; lcg_id++) {
      tmp[lcg_id] = nr_locate_BsrIndexByBufferSize(NR_LONG_BSR_TABLE, NR_LONG_BSR_TABLE_SIZE, LCG_bytes[lcg_id]);
    }
    LOG_D(NR_MAC,
          "[UE %d] sfn %d.%d BSR Trig=0x%x report LONG BSR (level LCGID %d %d %d %d %d %d %d %d)\n",
          mac->ue_id,
          frame,
          slot,
          sched_info->BSR_reporting_active,
          tmp[0],
          tmp[1],
          tmp[2],
          tmp[3],
          tmp[4],
          tmp[5],
          tmp[6],
          tmp[7]);
  } else if (padding_len == short_bsr_sz) {
    mac_ce_p->bsr.type_bsr = b_short_trunc;
    mac_ce_p->bsr.bsr.s.LcgID = lcg_id_bsr_max;
    mac_ce_p->bsr.bsr.s.Buffer_size =
        nr_locate_BsrIndexByBufferSize(NR_SHORT_BSR_TABLE, NR_SHORT_BSR_TABLE_SIZE, LCG_bytes[lcg_id_bsr_max]);
  } else if (padding_len >= sizeof(NR_BSR_LONG) + sizeof(NR_MAC_SUBHEADER_SHORT)) {
    mac_ce_p->bsr.type_bsr = b_long_trunc;
    //  Fixme: this should be sorted by (TS 38.321, 5.4.5)
    // the logical channels having data available for
    // transmission following a decreasing order of the highest priority logical channel (with or without data
    // available for transmission) in each of these LCG(s), and in case of equal priority, in increasing order of LCGID
    uint8_t *tmp = mac_ce_p->bsr.bsr.lcg_bsr;
    for (int lcg_id = 0; lcg_id < 8; lcg_id++) {
      tmp[lcg_id] = nr_locate_BsrIndexByBufferSize(NR_LONG_BSR_TABLE, NR_LONG_BSR_TABLE_SIZE, LCG_bytes[lcg_id]);
    }
  } else
    LOG_D(NR_MAC, "Can't add any BSR, not enough padding\n");

  /* Actions when a BSR is sent */
  if (mac_ce_p->bsr.type_bsr != b_none) {
    LOG_D(NR_MAC,
          "[UE %d] MAC BSR Sent! buff_len %ld triggering LCID %ld\n",
          mac->ue_id,
          mac_ce_p->pdu_end - mac_ce_p->cur_ptr,
          sched_info->regularBSR_trigger_lcid);
    *BSRsent = true;
    // Reset ReTx BSR Timer
    nr_timer_start(&sched_info->retxBSR_Timer);
    // Reset Periodic Timer except when BSR is truncated
    if (mac_ce_p->bsr.type_bsr != b_long_trunc && mac_ce_p->bsr.type_bsr != b_short_trunc) {
      nr_timer_start(&sched_info->periodicBSR_Timer);
      LOG_D(NR_MAC, "[UE %d] MAC Periodic BSR Timer Reset\n", mac->ue_id);
    }

    if (sched_info->regularBSR_trigger_lcid > 0) {
      nr_lcordered_info_t *lc_info = get_lc_info_from_lcid(mac, sched_info->regularBSR_trigger_lcid);
      AssertFatal(lc_info, "Couldn't find logical channel with LCID %ld\n", sched_info->regularBSR_trigger_lcid);
      if (lc_info->sr_id >= 0 && lc_info->sr_id < NR_MAX_SR_ID) {
        LOG_D(NR_MAC, "[UE %d][SR] Gave SDU to PHY, clearing scheduling request with ID %d\n", mac->ue_id, lc_info->sr_id);
        nr_sr_info_t *sr = &sched_info->sr_info[lc_info->sr_id];
        sr->pending = false;
        sr->counter = 0;
        nr_timer_stop(&sr->prohibitTimer);
      }
    }
  }

  int size =
      nr_write_ce_ulsch_pdu(mac_ce_p->cur_ptr, mac, mac_ce_p->phr_len ? &mac_ce_p->phr : NULL, &mac_ce_p->bsr, mac_ce_p->pdu_end);
  LOG_D(NR_MAC, "Added %d bytes of BSR\n", size);
  mac_ce_p->cur_ptr += size;
}

uint32_t get_count_lcids_same_priority(uint8_t start, uint8_t total_active_lcids, nr_lcordered_info_t *lcid_ordered_array)
{
  // count number of logical channels with same priority as curr_lcid
  uint8_t same_priority_count = 0;
  uint8_t curr_lcid = lcid_ordered_array[start].lcid;
  for (uint8_t index = start; index < total_active_lcids; index++) {
    if (lcid_ordered_array[start].priority == lcid_ordered_array[index].priority) {
      same_priority_count++;
    }
  }
  LOG_D(NR_MAC, "Number of lcids with same priority as that of lcid %d is %d\n", curr_lcid, same_priority_count);
  return same_priority_count;
}

static long get_num_bytes_to_reqlc(NR_UE_MAC_INST_t *mac,
                                   uint8_t same_priority_count,
                                   uint8_t lc_num,
                                   uint32_t buflen_remain_ep,
                                   int32_t buflen_remain,
                                   uint8_t round_id,
                                   uint32_t *bytes_read_fromlc,
                                   long *target)
{
  /* Calculates the number of bytes the logical channel should request from the correcponding RLC buffer*/
  nr_lcordered_info_t *lc_info = get_lc_info_from_lcid(mac, lc_num);
  AssertFatal(lc_info, "Couldn't find logical channel with LCID %d\n", lc_num);
  const uint32_t pbr = lc_info->pbr;
  NR_LC_SCHEDULING_INFO *sched_info = get_scheduling_info_from_lcid(mac, lc_num);
  int32_t lcid_remain_buffer = sched_info->LCID_buffer_remain;
  *target = (same_priority_count > 1) ? min(buflen_remain_ep, pbr) : pbr;

  long num_remaining_bytes = 0;
  long num_bytes_requested = 0;
  if (round_id == 0) { // initial round
    uint32_t pdu_remain = (same_priority_count > 1) ? buflen_remain_ep : buflen_remain;
    num_bytes_requested = (pdu_remain < pbr) ? min(pdu_remain, lcid_remain_buffer) : min(pbr, lcid_remain_buffer);
    num_remaining_bytes = *target - bytes_read_fromlc[lc_num - 1];
    num_bytes_requested = min(num_bytes_requested, num_remaining_bytes);
  } else { // from first round
    if (same_priority_count > 1) {
      num_bytes_requested = min(buflen_remain_ep, lcid_remain_buffer);
      num_remaining_bytes = buflen_remain_ep - bytes_read_fromlc[lc_num - 1];
      num_bytes_requested = min(num_bytes_requested, num_remaining_bytes);
    } else {
      num_bytes_requested = min(buflen_remain, lcid_remain_buffer);
    }
  }
  AssertFatal(num_remaining_bytes >= 0 && num_bytes_requested <= buflen_remain,
              "the total number of bytes allocated until target length is greater than expected\n");
  LOG_D(NR_MAC, "number of bytes requested for lcid %d is %li\n", lc_num, num_bytes_requested);

  return num_bytes_requested;
}

bool get_dataavailability_buffers(uint8_t total_active_lcids, nr_lcordered_info_t *lcid_ordered_array, bool *data_status_lcbuffers)
{
  // check whether there is any data in the rlc buffer corresponding to active lcs
  for (uint8_t id = 0; id < total_active_lcids; id++) {
    int lcid = lcid_ordered_array[id].lcid;
    if (data_status_lcbuffers[lcid_buffer_index(lcid)]) {
      return true;
    }
  }
  return false;
}

static uint select_logical_channels(NR_UE_MAC_INST_t *mac, nr_lcordered_info_t *active_lcids)
{
  // (TODO: selection of logical channels for logical channel prioritization procedure as per 5.4.3.1.2 Selection of logical
  // channels, TS38.321)
  int nb = 0;
  // selection of logical channels with Bj > 0
  for (int i = 0; i < mac->lc_ordered_list.count; i++) {
    int lcid = mac->lc_ordered_list.array[i]->lcid;
    NR_LC_SCHEDULING_INFO *sched_info = get_scheduling_info_from_lcid(mac, lcid);
    if (sched_info->Bj > 0) {
      active_lcids[nb++] = *mac->lc_ordered_list.array[i];
      LOG_D(NR_MAC, "The available lcid is %d with total active channels count = %d\n", lcid, nb);
    }
  }
  return nb;
}

static bool fill_mac_sdu(NR_UE_MAC_INST_t *mac,
                         frame_t frame,
                         slot_t slot,
                         uint8_t gNB_index,
                         int lcid,
                         uint count_same_priority_lcids,
                         uint32_t buflen_ep,
                         uint32_t *lcids_bytes_tot,
                         NR_UE_MAC_CE_INFO *mac_ce_p,
                         uint *num_lcids_same_priority,
                         uint32_t *LCG_bytes)
{
  /* prepare the MAC sdu */
  NR_LC_SCHEDULING_INFO *sched_info = get_scheduling_info_from_lcid(mac, lcid);
  int32_t lcid_remain_buffer = sched_info->LCID_buffer_remain;
  uint usable = mac_ce_p->end_for_tailer - mac_ce_p->cur_ptr;
  LOG_D(NR_MAC,
        "[UE %d] [%d.%d] lcp round = %d, remaining mac pdu length before BSR = %d, lcid buffer remaining = %d, lcid = %d \n",
        mac->ue_id,
        frame,
        slot,
        mac_ce_p->lcp_allocation_counter,
        usable,
        lcid_remain_buffer,
        lcid);

  if (mac_ce_p->end_for_tailer - mac_ce_p->cur_ptr < sizeof(NR_MAC_SUBHEADER_LONG))
    // We can't add one byte after the header
    return false;

  // Pointer used to build the MAC sub-PDU headers in the ULSCH buffer for each SDU
  NR_MAC_SUBHEADER_LONG *header = (NR_MAC_SUBHEADER_LONG *)mac_ce_p->cur_ptr;

  // number of bytes requested from RLC for each LCID
  long target = 0;
  // NR_MAC_SUBHEADER_SHORT can handle up to 256 bytes message
  int header_sz = usable < 256 ? sizeof(NR_MAC_SUBHEADER_SHORT) : sizeof(NR_MAC_SUBHEADER_LONG);
  long bytes_requested = get_num_bytes_to_reqlc(mac,
                                                count_same_priority_lcids,
                                                lcid,
                                                buflen_ep,
                                                usable - header_sz,
                                                mac_ce_p->lcp_allocation_counter,
                                                lcids_bytes_tot,
                                                &target);
  header_sz = bytes_requested < 256 ? sizeof(NR_MAC_SUBHEADER_SHORT) : sizeof(NR_MAC_SUBHEADER_LONG);

  uint16_t sdu_length = mac_rlc_data_req(mac->ue_id,
                                         mac->ue_id,
                                         gNB_index,
                                         frame,
                                         ENB_FLAG_NO,
                                         MBMS_FLAG_NO,
                                         lcid,
                                         bytes_requested,
                                         (char *)mac_ce_p->cur_ptr + header_sz,
                                         0,
                                         0);

  AssertFatal(bytes_requested >= sdu_length,
              "LCID = 0x%02x RLC has segmented %d bytes but MAC has max %li remaining bytes\n",
              lcid,
              sdu_length,
              bytes_requested);

  // Decrement Bj by the total size of MAC SDUs(RLC PDU) served to logical channel
  // currently the Bj is drecremented by size of MAC SDus everytime it is served to logical channel, so by this approach there
  // will be more chance for lower priority logical channels to be served in the next TTI
  // second approach can also be followed where Bj is decremented only in the first round but not in the subsequent rounds
  sched_info->Bj -= sdu_length; // TODO avoid Bj to go below 0
  LOG_D(NR_MAC,
        "decrement Bj of the lcid %d by size of sdu length = %d and new Bj for lcid %d is %d\n",
        lcid,
        sdu_length,
        lcid,
        sched_info->Bj);

  int lc_idx = lcid_buffer_index(lcid);
  if (sdu_length > 0) {
    mac_ce_p->num_sdus++;
    LOG_D(NR_MAC,
          "[UE %d] [%d.%d] UL-DXCH -> ULSCH, Generating UL MAC sub-PDU for SDU %d, length %d bytes, RB with LCID "
          "0x%02x (buflen (TBS) %ld bytes)\n",
          mac->ue_id,
          frame,
          slot,
          mac_ce_p->num_sdus,
          sdu_length,
          lcid,
          mac_ce_p->end_for_tailer - mac_ce_p->cur_ptr);
    if (header_sz == sizeof(NR_MAC_SUBHEADER_SHORT)) {
      *(NR_MAC_SUBHEADER_SHORT *)header = (NR_MAC_SUBHEADER_SHORT){.R = 0, .F = 0, .LCID = lcid, .L = sdu_length};
    } else {
      *header = (NR_MAC_SUBHEADER_LONG){.R = 0, .F = 1, .LCID = lcid, .L = htons(sdu_length)};
#ifdef ENABLE_MAC_PAYLOAD_DEBUG
      LOG_I(NR_MAC, "dumping MAC SDU with length %d: \n", sduL);
      log_dump(NR_MAC, header, sduL, LOG_DUMP_CHAR, "\n");
#endif
    }
    mac_ce_p->cur_ptr += header_sz + sdu_length;
  } else {
    mac_ce_p->lcids_data_status[lc_idx] = false;
    (*num_lcids_same_priority)--;
    LOG_D(NR_MAC, "No data to transmit for RB with LCID 0x%02x\n and hence set to false", lcid);
    return false;
  }

  // Update Buffer remain and BSR bytes after transmission
  sched_info->LCID_buffer_remain -= sdu_length;
  LCG_bytes[sched_info->LCGID] -= sdu_length;
  LOG_D(NR_MAC,
        "[UE %d] Update BSR [%d.%d] LCG_bytes for LCG%ld = %d\n",
        mac->ue_id,
        frame,
        slot,
        sched_info->LCGID,
        LCG_bytes[sched_info->LCGID]);

  // update number of bytes served from the current lcid
  lcids_bytes_tot[lc_idx] += (sdu_length + (count_same_priority_lcids > 1 ? 1 : 0) * sizeof(NR_MAC_SUBHEADER_LONG));

  if ((mac_ce_p->lcp_allocation_counter == 0 && lcids_bytes_tot[lc_idx] >= target)
      || (count_same_priority_lcids > 1
          && lcids_bytes_tot[lc_idx] >= buflen_ep)) { // only prioritized bit rate should be taken from logical channel in
    // the first lcp run except when infinity
    LOG_D(NR_MAC,
          "Total number bytes read from rlc buffer for lcid %d are %d\n",
          lcid,
          lcids_bytes_tot[lc_idx]);
    (*num_lcids_same_priority)--;
    return false;
  }
  return true;
}

/**
 * Function:      to fetch data to be transmitted from RLC, place it in the ULSCH PDU buffer
 to generate the complete MAC PDU with sub-headers and MAC CEs according to ULSCH MAC PDU generation (6.1.2 TS 38.321)
 the selected sub-header for the payload sub-PDUs is NR_MAC_SUBHEADER_LONG
 * @module_idP    Module ID
 * @CC_id         Component Carrier index
 * @frame         current UL frame
 * @slot          current UL slot
 * @gNB_index     gNB index
 * @ulsch_buffer  Pointer to ULSCH PDU
 * @buflen        TBS
 */
static uint8_t nr_ue_get_sdu(NR_UE_MAC_INST_t *mac,
                             int CC_id,
                             frame_t frame,
                             slot_t slot,
                             uint8_t gNB_index,
                             uint8_t *ulsch_buffer,
                             const uint32_t buflen,
                             int tx_power,
                             int P_CMAX,
                             bool *BSRsent)
{
  NR_UE_MAC_CE_INFO mac_ce_info;

  // in the first run all the lc are allocated as per bj and prioritized bit rate but in subsequent runs, no need to consider
  // bj and prioritized bit rate but just consider priority
  uint32_t buflen_ep = 0; // this variable holds the length in bytes in mac pdu when multiple equal priority channels are present
  // because as per standard(TS38.321), all equal priority channels should be served equally

  // Call BSR procedure as described in Section 5.4.5 in 38.321
  // Check whether BSR is triggered before scheduling ULSCH
  uint32_t LCG_bytes[NR_MAX_NUM_LCGID] = {0};
  nr_update_bsr(mac, LCG_bytes);

  nr_ue_get_sdu_mac_ce_pre(mac, CC_id, frame, slot, gNB_index, ulsch_buffer, buflen, LCG_bytes, &mac_ce_info, tx_power, P_CMAX);

  LOG_D(NR_MAC,
        "[UE %d] [%d.%d] process UL transport block with size TBS = %d bytes, number of existing LCids %d \n",
        mac->ue_id,
        frame,
        slot,
        buflen,
        mac->lc_ordered_list.count);

  // variable used to build the lcids with positive Bj
  nr_lcordered_info_t lcids_bj_pos[mac->lc_ordered_list.count];
  int avail_lcids_count = select_logical_channels(mac, lcids_bj_pos);

  // multiplex in the order of highest priority
  do {
    /*
      go until there is space availabile in the MAC PDU and there is data available in RLC buffers of active logical channels
    */
    uint num_lcids_same_priority = 0;
    uint count_same_priority_lcids = 0;

    // variable used to store the total bytes read from rlc for each lcid
    uint32_t lcids_bytes_tot[NR_MAX_NUM_LCID] = {0};

    for (uint id = 0; id < avail_lcids_count; id++) {
      /*
  loop over all logical channels in the order of priority. As stated in TS138.321 Section 5.4.3.1, in the first run, only
  prioritized number of bytes are taken out from the corresponding RLC buffers of all active logical channels and if there is
  still space availble in the MAC PDU, then from the next run all the remaining data from the higher priority logical channel
  is placed in the MAC PDU before going on to next high priority logical channel
      */
      int lcid = lcids_bj_pos[id].lcid;
      int idx = lcid_buffer_index(lcid);
      // skip the logical channel if no data in the buffer initially or the data in the buffer was zero because it was written in to
      // MAC PDU
      if (!mac_ce_info.lcids_data_status[idx]) {
        continue;
      }

      // count number of lc with same priority as lcid
      if (!num_lcids_same_priority) {
        num_lcids_same_priority = count_same_priority_lcids = get_count_lcids_same_priority(id, avail_lcids_count, lcids_bj_pos);
      }

      int remain = mac_ce_info.end_for_tailer - mac_ce_info.cur_ptr;
      LOG_D(NR_MAC,
            "[UE %d] [%d.%d] UL-DXCH -> ULSCH, RLC with LCID 0x%02x buflen_remain %d bytes)\n",
            mac->ue_id,
            frame,
            slot,
            lcid,
            remain);

      if (num_lcids_same_priority == count_same_priority_lcids) {
        buflen_ep = (remain - (count_same_priority_lcids * sizeof(NR_MAC_SUBHEADER_LONG))) / count_same_priority_lcids;
        /* after serving equal priority LCIDs in the first round, buflen_remain could be > 0 and < (count_same_priority_lcids * sh_size)
           if above division yeilds a remainder. hence the following sets buflen_ep to 0 if there is not enough buffer left for subsequent rounds
        */
        buflen_ep = buflen_ep < 0 ? 0 : buflen_ep;
      }

      while (mac_ce_info.end_for_tailer - mac_ce_info.cur_ptr > 0) {
        /*
          loops until the requested number of bytes from MAC to RLC are placed in the MAC PDU. The number of requested bytes
          depends on whether it is the first run or otherwise because in the first run only prioritited number of bytes of all
          active logical channels in the order of priority are placed in the MAC PDU. The 'get_num_bytes_to_reqlc' calculates
          the target number of bytes to request from RLC via 'mac_rlc_data_req'
        */
        if (!fill_mac_sdu(mac,
                          frame,
                          slot,
                          gNB_index,
                          lcid,
                          count_same_priority_lcids,
                          buflen_ep,
                          lcids_bytes_tot,
                          &mac_ce_info,
                          &num_lcids_same_priority,
                          LCG_bytes)) {
          break;
        }
      }
    }

    mac_ce_info.lcp_allocation_counter++;
  } while (mac_ce_info.end_for_tailer - mac_ce_info.cur_ptr > sizeof(NR_MAC_SUBHEADER_LONG)
           && get_dataavailability_buffers(avail_lcids_count, lcids_bj_pos, mac_ce_info.lcids_data_status));

  nr_ue_get_sdu_mac_ce_post(mac, frame, slot, &mac_ce_info, LCG_bytes, BSRsent);

  // Compute final offset for padding and fill remainder of ULSCH with 0
  int remain = mac_ce_info.pdu_end - mac_ce_info.cur_ptr;
  if (remain) {
    LOG_D(NR_MAC, "Filling remainder %d bytes to the UL PDU \n", remain);
    *(NR_MAC_SUBHEADER_FIXED *)mac_ce_info.cur_ptr = (NR_MAC_SUBHEADER_FIXED){.R = 0, .LCID = UL_SCH_LCID_PADDING};
    mac_ce_info.cur_ptr++;
    memset(mac_ce_info.cur_ptr, 0, mac_ce_info.pdu_end - mac_ce_info.cur_ptr);
  }
#ifdef ENABLE_MAC_PAYLOAD_DEBUG
  LOG_I(NR_MAC, "MAC PDU %d bytes \n", mac_ce_p->cur_ptr - ulsch_buffer);
  log_dump(NR_MAC, ulsch_buffer, buflen, LOG_DUMP_CHAR, "\n");
#endif

  return mac_ce_info.num_sdus > 0; // success if we got at least one sdu
}

static void schedule_ta_command(fapi_nr_dl_config_request_t *dl_config, NR_UE_MAC_INST_t *mac)
{
  NR_UL_TIME_ALIGNMENT_t *ul_time_alignment = &mac->ul_time_alignment;
  fapi_nr_ta_command_pdu *ta = &dl_config->dl_config_list[dl_config->number_pdus].ta_command_pdu;
  ta->ta_frame = ul_time_alignment->frame;
  ta->ta_slot = ul_time_alignment->slot;
  ta->ta_offset = mac->n_ta_offset;
  ta->is_rar = ul_time_alignment->ta_apply == rar_ta;
  ta->ta_command = ul_time_alignment->ta_command;
  dl_config->dl_config_list[dl_config->number_pdus].pdu_type = FAPI_NR_CONFIG_TA_COMMAND;
  dl_config->number_pdus += 1;
  ul_time_alignment->ta_apply = no_ta;
}

static void nr_ue_fill_phr(NR_UE_MAC_INST_t *mac,
                           NR_SINGLE_ENTRY_PHR_MAC_CE *phr,
                           float P_CMAX,
                           float tx_power,
                           frame_t frameP,
                           sub_frame_t subframe)
{
  nr_phr_info_t *phr_info = &mac->scheduling_info.phr_info;
  // Value mapping according to 38.133 10.1.18.1
  const int PC_MAX_00 = -29;
  *phr = (NR_SINGLE_ENTRY_PHR_MAC_CE){.PCMAX = max(0, (int)P_CMAX - PC_MAX_00) & 0x3f};
  // Value mapping according to 38.133 10.1.17.1.1
  const int POWER_HEADROOM_55 = 22;
  int headroom = P_CMAX - tx_power;
  if (headroom < POWER_HEADROOM_55) {
    const int POWER_HEADROOM_0 = -32;
    phr->PH = max(0, headroom - POWER_HEADROOM_0);
  } else {
    phr->PH = min(0x3f, 55 + (headroom - POWER_HEADROOM_55) / 2);
  }

  LOG_D(NR_MAC,
        "PHR Reporting sfn.subframe %d.%d reason = %d, P_CMAX = %d (%5.2f dBm), headrom = %d (%d dB) tx_power = %5.2f dBm\n",
        frameP,
        subframe,
        phr_info->phr_reporting,
        phr->PCMAX,
        P_CMAX,
        phr->PH,
        headroom,
        tx_power);

  phr_info->PathlossLastValue = compute_nr_SSB_PL(mac, mac->ssb_measurements.ssb_rsrp_dBm);
  // Restart both timers according to 38.321
  nr_timer_start(&phr_info->periodicPHR_Timer);
  nr_timer_start(&phr_info->prohibitPHR_Timer);
  phr_info->phr_reporting = 0;
}
