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

/*
  gnb_config.c
  -------------------
  AUTHOR  : Lionel GAUTHIER, navid nikaein, Laurent Winckel, WEI-TAI CHEN
  COMPANY : EURECOM, NTUST
  EMAIL   : Lionel.Gauthier@eurecom.fr, navid.nikaein@eurecom.fr, kroempa@gmail.com
*/

// IMPORTANT

#include "executables/softmodem-common.h"
#include "gnb_config.h"
#include "enb_paramdef.h"
#include "UTIL/OTG/otg.h"
#include "sctp_default_values.h"
#include "f1ap_common.h"
#include "PHY/INIT/nr_phy_init.h"
#include "radio/ETHERNET/ethernet_lib.h"
#include "nfapi_vnf.h"
#include "nfapi_pnf.h"
#include "prs_nr_paramdef.h"
#include "L1_nr_paramdef.h"
#include "MACRLC_nr_paramdef.h"
#include "gnb_paramdef.h"
#include "NR_MAC_gNB/mac_proto.h"
#include "RRC/NR/nr_rrc_extern.h"
#include "nfapi/oai_integration/vendor_ext.h"
#ifdef ENABLE_AERIAL
#include "nfapi/oai_integration/aerial/fapi_vnf_p5.h"
#endif
#include "lib/f1ap_interface_management.h"

static int DEFBANDS[] = {7};
static int DEFENBS[] = {0};
static int DEFBFW[] = {0x00007fff};
static int DEFRUTPCORES[] = {-1,-1,-1,-1};

/**
 * @brief Helper define to allocate and initialize SetupRelease structures
 */
#define INIT_SETUP_RELEASE(type, element)                                    \
  do {                                                                       \
    element = calloc_or_fail(1, sizeof(*element));                           \
    (element)->present = NR_SetupRelease_##type##_PR_setup;                  \
    (element)->choice.setup = CALLOC(1, sizeof(*((element)->choice.setup))); \
  } while (0)

/**
 * Allocate memory and initialize ServingCellConfigCommon struct members
 */
void prepare_scc(NR_ServingCellConfigCommon_t *scc)
{
  // NR_ServingCellConfigCommon
  scc->physCellId = calloc_or_fail(1, sizeof(*scc->physCellId));
  scc->n_TimingAdvanceOffset = calloc_or_fail(1, sizeof(*scc->n_TimingAdvanceOffset));
  scc->ssb_PositionsInBurst = calloc_or_fail(1, sizeof(*scc->ssb_PositionsInBurst));
  scc->ssb_periodicityServingCell = calloc_or_fail(1, sizeof(*scc->ssb_periodicityServingCell));
  scc->ssbSubcarrierSpacing = calloc_or_fail(1, sizeof(*scc->ssbSubcarrierSpacing));
  scc->tdd_UL_DL_ConfigurationCommon = calloc_or_fail(1, sizeof(*scc->tdd_UL_DL_ConfigurationCommon));
  // scc->tdd_UL_DL_ConfigurationCommon->pattern2 = calloc_or_fail(1, sizeof(*scc->tdd_UL_DL_ConfigurationCommon->pattern2));
  scc->downlinkConfigCommon = calloc_or_fail(1, sizeof(*scc->downlinkConfigCommon));
  scc->downlinkConfigCommon->frequencyInfoDL = calloc_or_fail(1, sizeof(*scc->downlinkConfigCommon->frequencyInfoDL));
  scc->downlinkConfigCommon->initialDownlinkBWP = calloc_or_fail(1, sizeof(*scc->downlinkConfigCommon->initialDownlinkBWP));

  NR_FrequencyInfoDL_t *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
  frequencyInfoDL->absoluteFrequencySSB = calloc_or_fail(1, sizeof(*frequencyInfoDL->absoluteFrequencySSB));
  NR_FreqBandIndicatorNR_t *dl_frequencyBandList = calloc_or_fail(1, sizeof(*dl_frequencyBandList));
  asn1cSeqAdd(&frequencyInfoDL->frequencyBandList.list, dl_frequencyBandList);
  struct NR_SCS_SpecificCarrier *dl_scs_SpecificCarrierList = calloc_or_fail(1, sizeof(*dl_scs_SpecificCarrierList));
  asn1cSeqAdd(&frequencyInfoDL->scs_SpecificCarrierList.list, dl_scs_SpecificCarrierList);

  INIT_SETUP_RELEASE(PDCCH_ConfigCommon, scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon);
  NR_SetupRelease_PDCCH_ConfigCommon_t *pdcch_ConfigCommon = scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon;
  struct NR_PDCCH_ConfigCommon *pdcchCCSetup = pdcch_ConfigCommon->choice.setup;
  pdcchCCSetup->controlResourceSetZero = calloc_or_fail(1, sizeof(*pdcchCCSetup->controlResourceSetZero));
  pdcchCCSetup->searchSpaceZero = calloc_or_fail(1, sizeof(*pdcchCCSetup->searchSpaceZero));
  pdcchCCSetup->commonControlResourceSet = NULL;

  INIT_SETUP_RELEASE(PDSCH_ConfigCommon, scc->downlinkConfigCommon->initialDownlinkBWP->pdsch_ConfigCommon);
  NR_SetupRelease_PDSCH_ConfigCommon_t *pdsch_ConfigCommon = scc->downlinkConfigCommon->initialDownlinkBWP->pdsch_ConfigCommon;
  pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList =
      calloc_or_fail(1, sizeof(*pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList));

  scc->uplinkConfigCommon = calloc_or_fail(1, sizeof(*scc->uplinkConfigCommon));
  scc->uplinkConfigCommon->frequencyInfoUL = calloc_or_fail(1, sizeof(*scc->uplinkConfigCommon->frequencyInfoUL));
  scc->uplinkConfigCommon->initialUplinkBWP = calloc_or_fail(1, sizeof(*scc->uplinkConfigCommon->initialUplinkBWP));

  NR_FrequencyInfoUL_t *frequencyInfoUL = scc->uplinkConfigCommon->frequencyInfoUL;
  NR_FreqBandIndicatorNR_t *ul_frequencyBandList = calloc_or_fail(1, sizeof(*ul_frequencyBandList));
  frequencyInfoUL->frequencyBandList = calloc_or_fail(1, sizeof(*frequencyInfoUL->frequencyBandList));
  asn1cSeqAdd(&frequencyInfoUL->frequencyBandList->list, ul_frequencyBandList);
  frequencyInfoUL->absoluteFrequencyPointA = calloc_or_fail(1, sizeof(*frequencyInfoUL->absoluteFrequencyPointA));
  frequencyInfoUL->p_Max = calloc_or_fail(1, sizeof(*frequencyInfoUL->p_Max));

  struct NR_SCS_SpecificCarrier *ul_scs_SpecificCarrierList = calloc_or_fail(1, sizeof(*ul_scs_SpecificCarrierList));
  asn1cSeqAdd(&frequencyInfoUL->scs_SpecificCarrierList.list, ul_scs_SpecificCarrierList);

  INIT_SETUP_RELEASE(RACH_ConfigCommon, scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon);
  NR_SetupRelease_RACH_ConfigCommon_t *rach_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon;
  struct NR_RACH_ConfigCommon *rachCCSetup = rach_ConfigCommon->choice.setup;
  rachCCSetup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB =
      calloc_or_fail(1, sizeof(*rachCCSetup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB));
  rachCCSetup->rsrp_ThresholdSSB = calloc_or_fail(1, sizeof(*rachCCSetup->rsrp_ThresholdSSB));
  rachCCSetup->msg1_SubcarrierSpacing = calloc_or_fail(1, sizeof(*rachCCSetup->msg1_SubcarrierSpacing));
  rachCCSetup->msg3_transformPrecoder = calloc_or_fail(1, sizeof(*rachCCSetup->msg3_transformPrecoder));
  *rachCCSetup->msg3_transformPrecoder = NR_PUSCH_Config__transformPrecoder_disabled;

  INIT_SETUP_RELEASE(PUSCH_ConfigCommon, scc->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon);
  NR_SetupRelease_PUSCH_ConfigCommon_t *pusch_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon;
  struct NR_PUSCH_ConfigCommon *puschCCSetup = pusch_ConfigCommon->choice.setup;
  puschCCSetup->groupHoppingEnabledTransformPrecoding = NULL;
  puschCCSetup->pusch_TimeDomainAllocationList = calloc_or_fail(1, sizeof(*puschCCSetup->pusch_TimeDomainAllocationList));
  puschCCSetup->msg3_DeltaPreamble = calloc_or_fail(1, sizeof(*puschCCSetup->msg3_DeltaPreamble));
  puschCCSetup->p0_NominalWithGrant = calloc_or_fail(1, sizeof(*puschCCSetup->p0_NominalWithGrant));

  INIT_SETUP_RELEASE(PUCCH_ConfigCommon, scc->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon);
  NR_SetupRelease_PUCCH_ConfigCommon_t *pucch_ConfigCommon = scc->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon;
  struct NR_PUCCH_ConfigCommon *pucchCCSetup = pucch_ConfigCommon->choice.setup;
  pucchCCSetup->p0_nominal = calloc_or_fail(1, sizeof(*pucchCCSetup->p0_nominal));
  pucchCCSetup->pucch_ResourceCommon = calloc_or_fail(1, sizeof(*pucchCCSetup->pucch_ResourceCommon));
  pucchCCSetup->hoppingId = calloc_or_fail(1, sizeof(*pucchCCSetup->hoppingId));

  scc->ext2 = calloc_or_fail(1, sizeof(*scc->ext2));
  scc->ext2->ntn_Config_r17 = calloc_or_fail(1, sizeof(*scc->ext2->ntn_Config_r17));
  scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17 = calloc_or_fail(1, sizeof(*scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17));

  scc->ext2->ntn_Config_r17->ephemerisInfo_r17 = calloc_or_fail(1, sizeof(*scc->ext2->ntn_Config_r17->ephemerisInfo_r17));
  scc->ext2->ntn_Config_r17->ta_Info_r17 = calloc_or_fail(1, sizeof(*scc->ext2->ntn_Config_r17->ta_Info_r17));

  scc->ext2->ntn_Config_r17->ephemerisInfo_r17->present = NR_EphemerisInfo_r17_PR_positionVelocity_r17;
  scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17 =
      calloc_or_fail(1, sizeof(*scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17));
}
void prepare_msgA_scc(NR_ServingCellConfigCommon_t *scc) {
  NR_BWP_UplinkCommon_t *initialUplinkBWP = scc->uplinkConfigCommon->initialUplinkBWP;
  // Add the struct ext1
  initialUplinkBWP->ext1 = calloc(1, sizeof(*initialUplinkBWP->ext1));
  initialUplinkBWP->ext1->msgA_ConfigCommon_r16 = calloc(1, sizeof(*initialUplinkBWP->ext1->msgA_ConfigCommon_r16));
  initialUplinkBWP->ext1->msgA_ConfigCommon_r16->present = NR_SetupRelease_MsgA_ConfigCommon_r16_PR_setup;
  initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup =
      calloc(1, sizeof(*initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup));
  NR_MsgA_ConfigCommon_r16_t *NR_MsgA_ConfigCommon_r16 = initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup;
  NR_MsgA_ConfigCommon_r16->rach_ConfigCommonTwoStepRA_r16.rach_ConfigGenericTwoStepRA_r16.msgB_ResponseWindow_r16 =
      calloc(1, sizeof(long));
  NR_MsgA_ConfigCommon_r16->rach_ConfigCommonTwoStepRA_r16.msgA_RSRP_Threshold_r16 = calloc(1, sizeof(NR_RSRP_Range_t));

  NR_MsgA_ConfigCommon_r16->rach_ConfigCommonTwoStepRA_r16.msgA_CB_PreamblesPerSSB_PerSharedRO_r16 = calloc(1, sizeof(long));

  NR_MsgA_ConfigCommon_r16->msgA_PUSCH_Config_r16 = calloc(1, sizeof(NR_MsgA_PUSCH_Config_r16_t));
  NR_MsgA_PUSCH_Config_r16_t *msgA_PUSCH_Config_r16 = NR_MsgA_ConfigCommon_r16->msgA_PUSCH_Config_r16;
  msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16 = calloc(1, sizeof(NR_MsgA_PUSCH_Resource_r16_t));
  NR_MsgA_PUSCH_Resource_r16_t *msgA_PUSCH_Resource = msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16;
  msgA_PUSCH_Resource->startSymbolAndLengthMsgA_PO_r16 = calloc(1, sizeof(long));
  msgA_PUSCH_Config_r16->msgA_TransformPrecoder_r16 = calloc(1, sizeof(long));
}

// Section 4.1 in 38.213
NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR get_ssb_len(NR_ServingCellConfigCommon_t *scc)
{
  NR_FrequencyInfoDL_t *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
  int scs = *scc->ssbSubcarrierSpacing;
  int nr_band = *frequencyInfoDL->frequencyBandList.list.array[0];
  long freq = from_nrarfcn(nr_band, scs, frequencyInfoDL->absoluteFrequencyPointA);
  frame_type_t frame_type = get_frame_type(nr_band, scs);
  if (scs == 0) {
    if (freq < 3000000000)
      return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_shortBitmap;
    else
      return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap;
  }
  if (scs == 1) {
    if (nr_band == 5 || nr_band == 24 || nr_band == 66 || frame_type == FDD) { // case B or paired spectrum
      if (freq < 3000000000)
        return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_shortBitmap;
      else
        return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap;
    }
    else {  // case C and unpaired spectrum
      if (freq < 1880000000)
        return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_shortBitmap;
      else
        return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap;
    }
  }
  // FR2
  return NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_longBitmap;
}

static struct NR_SCS_SpecificCarrier configure_scs_carrier(int mu, int N_RB)
{
  struct NR_SCS_SpecificCarrier scs_sc = {0};
  scs_sc.offsetToCarrier = 0;
  scs_sc.subcarrierSpacing = mu;
  scs_sc.carrierBandwidth = N_RB;
  return scs_sc;
}

static struct NR_PUSCH_TimeDomainResourceAllocation *add_PUSCH_TimeDomainResourceAllocation(int startSymbolAndLength)
{
  struct NR_PUSCH_TimeDomainResourceAllocation *pusch_alloc = calloc_or_fail(1, sizeof(*pusch_alloc));
  pusch_alloc->k2 = calloc_or_fail(1, sizeof(*pusch_alloc->k2));
  *pusch_alloc->k2 = 6;
  pusch_alloc->mappingType = NR_PUSCH_TimeDomainResourceAllocation__mappingType_typeB;
  pusch_alloc->startSymbolAndLength = startSymbolAndLength;
  return pusch_alloc;
}

/**
 * Fill ServingCellConfigCommon struct members for unitary simulators
 */
void fill_scc_sim(NR_ServingCellConfigCommon_t *scc, uint64_t *ssb_bitmap, int N_RB_DL, int N_RB_UL, int mu_dl, int mu_ul)
{
  *scc->physCellId = 0;
  *scc->ssb_periodicityServingCell = NR_ServingCellConfigCommon__ssb_periodicityServingCell_ms20;
  scc->dmrs_TypeA_Position = NR_ServingCellConfigCommon__dmrs_TypeA_Position_pos2;
  *scc->ssbSubcarrierSpacing = mu_dl;

  struct NR_FrequencyInfoDL *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
  if (mu_dl == 0) {
    *frequencyInfoDL->absoluteFrequencySSB = 520432;
    *frequencyInfoDL->frequencyBandList.list.array[0] = 38;
    frequencyInfoDL->absoluteFrequencyPointA = 520000;
  } else {
    *frequencyInfoDL->absoluteFrequencySSB = 641032;
    *frequencyInfoDL->frequencyBandList.list.array[0] = 78;
    frequencyInfoDL->absoluteFrequencyPointA = 640000;
  }

  *frequencyInfoDL->scs_SpecificCarrierList.list.array[0] = configure_scs_carrier(mu_dl, N_RB_DL);
  struct NR_BWP_DownlinkCommon *initialDownlinkBWP = scc->downlinkConfigCommon->initialDownlinkBWP;

  initialDownlinkBWP->genericParameters.locationAndBandwidth = PRBalloc_to_locationandbandwidth(N_RB_DL, 0);
  initialDownlinkBWP->genericParameters.subcarrierSpacing = mu_dl;
  *initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->controlResourceSetZero = 12;
  *initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->searchSpaceZero = 0;
  struct NR_PDSCH_TimeDomainResourceAllocation *timedomainresourceallocation0 =
      calloc_or_fail(1, sizeof(*timedomainresourceallocation0));
  timedomainresourceallocation0->mappingType=NR_PDSCH_TimeDomainResourceAllocation__mappingType_typeA;
  timedomainresourceallocation0->startSymbolAndLength = 54;
  asn1cSeqAdd(&initialDownlinkBWP->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList->list,
              timedomainresourceallocation0);
  struct NR_PDSCH_TimeDomainResourceAllocation *timedomainresourceallocation1 =
      calloc_or_fail(1, sizeof(*timedomainresourceallocation1));
  timedomainresourceallocation1->mappingType = NR_PDSCH_TimeDomainResourceAllocation__mappingType_typeA;
  timedomainresourceallocation1->startSymbolAndLength = 57;
  asn1cSeqAdd(&initialDownlinkBWP->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList->list,
              timedomainresourceallocation1);

  struct NR_FrequencyInfoUL *frequencyInfoUL = scc->uplinkConfigCommon->frequencyInfoUL;
  *frequencyInfoUL->frequencyBandList->list.array[0] = mu_ul ? 78 : 38;
  *frequencyInfoUL->absoluteFrequencyPointA = -1;
  *frequencyInfoUL->scs_SpecificCarrierList.list.array[0] = configure_scs_carrier(mu_ul, N_RB_UL);
  *frequencyInfoUL->p_Max = 20;

  struct NR_BWP_UplinkCommon *initialUplinkBWP = scc->uplinkConfigCommon->initialUplinkBWP;
  initialUplinkBWP->genericParameters.locationAndBandwidth = PRBalloc_to_locationandbandwidth(N_RB_UL, 0);
  initialUplinkBWP->genericParameters.subcarrierSpacing = mu_ul;

  struct NR_SetupRelease_RACH_ConfigCommon *rach_ConfigCommon = initialUplinkBWP->rach_ConfigCommon;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.prach_ConfigurationIndex = 98;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.msg1_FDM = NR_RACH_ConfigGeneric__msg1_FDM_one;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.msg1_FrequencyStart = 0;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.zeroCorrelationZoneConfig = 13;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.preambleReceivedTargetPower = -118;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.preambleTransMax = NR_RACH_ConfigGeneric__preambleTransMax_n10;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.powerRampingStep = NR_RACH_ConfigGeneric__powerRampingStep_dB2;
  rach_ConfigCommon->choice.setup->rach_ConfigGeneric.ra_ResponseWindow = NR_RACH_ConfigGeneric__ra_ResponseWindow_sl20;
  rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->present =
      NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_one;
  rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->choice.one =
      NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB__one_n64;
  rach_ConfigCommon->choice.setup->ra_ContentionResolutionTimer = NR_RACH_ConfigCommon__ra_ContentionResolutionTimer_sf64;
  *rach_ConfigCommon->choice.setup->rsrp_ThresholdSSB = 19;
  rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.present = NR_RACH_ConfigCommon__prach_RootSequenceIndex_PR_l139;
  rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.choice.l139 = 0;
  rach_ConfigCommon->choice.setup->restrictedSetConfig = NR_RACH_ConfigCommon__restrictedSetConfig_unrestrictedSet;
  *rach_ConfigCommon->choice.setup->msg1_SubcarrierSpacing = mu_ul;
  struct NR_SetupRelease_PUSCH_ConfigCommon *pusch_ConfigCommon = initialUplinkBWP->pusch_ConfigCommon;

  asn1cSeqAdd(&pusch_ConfigCommon->choice.setup->pusch_TimeDomainAllocationList->list, add_PUSCH_TimeDomainResourceAllocation(55));
  asn1cSeqAdd(&pusch_ConfigCommon->choice.setup->pusch_TimeDomainAllocationList->list, add_PUSCH_TimeDomainResourceAllocation(38));

  *pusch_ConfigCommon->choice.setup->msg3_DeltaPreamble = 1;
  *pusch_ConfigCommon->choice.setup->p0_NominalWithGrant = -90;
  struct NR_SetupRelease_PUCCH_ConfigCommon *pucch_ConfigCommon = initialUplinkBWP->pucch_ConfigCommon;
  pucch_ConfigCommon->choice.setup->pucch_GroupHopping = NR_PUCCH_ConfigCommon__pucch_GroupHopping_neither;
  *pucch_ConfigCommon->choice.setup->hoppingId = 40;
  *pucch_ConfigCommon->choice.setup->p0_nominal = -90;
  scc->ssb_PositionsInBurst->present = NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap;
  *ssb_bitmap = 0xff;

  struct NR_TDD_UL_DL_ConfigCommon *tdd_UL_DL_ConfigurationCommon = scc->tdd_UL_DL_ConfigurationCommon;
  tdd_UL_DL_ConfigurationCommon->referenceSubcarrierSpacing = mu_dl;

  NR_TDD_UL_DL_Pattern_t *p1 = &tdd_UL_DL_ConfigurationCommon->pattern1;
  p1->dl_UL_TransmissionPeriodicity = (mu_dl == 0) ? NR_TDD_UL_DL_Pattern__dl_UL_TransmissionPeriodicity_ms10
                                                   : NR_TDD_UL_DL_Pattern__dl_UL_TransmissionPeriodicity_ms5;
  p1->nrofDownlinkSlots = 7;
  p1->nrofDownlinkSymbols = 6;
  p1->nrofUplinkSlots = 2;
  p1->nrofUplinkSymbols = 4;

  struct NR_TDD_UL_DL_Pattern *p2 = tdd_UL_DL_ConfigurationCommon->pattern2;
  p2->dl_UL_TransmissionPeriodicity = 321;
  p2->nrofDownlinkSlots = -1;
  p2->nrofDownlinkSymbols = -1;
  p2->nrofUplinkSlots = -1;
  p2->nrofUplinkSymbols = -1;

  scc->ss_PBCH_BlockPower = 20;
}


void fix_scc(NR_ServingCellConfigCommon_t *scc, uint64_t ssbmap)
{
  scc->ssb_PositionsInBurst->present = get_ssb_len(scc);
  uint8_t curr_bit;

  // changing endianicity of ssbmap and filling the ssb_PositionsInBurst buffers
  if(scc->ssb_PositionsInBurst->present == NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_shortBitmap) {
    scc->ssb_PositionsInBurst->choice.shortBitmap.size = 1;
    scc->ssb_PositionsInBurst->choice.shortBitmap.bits_unused = 4;
    scc->ssb_PositionsInBurst->choice.shortBitmap.buf = CALLOC(1, 1);
    scc->ssb_PositionsInBurst->choice.shortBitmap.buf[0] = 0;
    for (int i = 0; i < 8; i++) {
      if (i<scc->ssb_PositionsInBurst->choice.shortBitmap.bits_unused)
        curr_bit = 0;
      else
        curr_bit = (ssbmap >> (7 - i)) & 0x01;
      scc->ssb_PositionsInBurst->choice.shortBitmap.buf[0] |= curr_bit << i;
    }
  } else if(scc->ssb_PositionsInBurst->present == NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap) {
    scc->ssb_PositionsInBurst->choice.mediumBitmap.size = 1;
    scc->ssb_PositionsInBurst->choice.mediumBitmap.bits_unused = 0;
    scc->ssb_PositionsInBurst->choice.mediumBitmap.buf = CALLOC(1, 1);
    scc->ssb_PositionsInBurst->choice.mediumBitmap.buf[0] = 0;
    for (int i = 0; i < 8; i++)
      scc->ssb_PositionsInBurst->choice.mediumBitmap.buf[0] |= (((ssbmap >> (7 - i)) & 0x01) << i);
  } else {
    scc->ssb_PositionsInBurst->choice.longBitmap.size = 8;
    scc->ssb_PositionsInBurst->choice.longBitmap.bits_unused = 0;
    scc->ssb_PositionsInBurst->choice.longBitmap.buf = CALLOC(1, 8);
    for (int j = 0; j < 8; j++) {
       scc->ssb_PositionsInBurst->choice.longBitmap.buf[j] = 0;
       curr_bit = (ssbmap >> (j << 3)) & 0xff;
       for (int i = 0; i < 8; i++)
         scc->ssb_PositionsInBurst->choice.longBitmap.buf[j] |= (((curr_bit >> (7 - i)) & 0x01) << i);
    }
  }

  // fix SS0 and Coreset0
  NR_PDCCH_ConfigCommon_t *pdcch_cc = scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup;
  if((int)*pdcch_cc->searchSpaceZero == -1) {
    free(pdcch_cc->searchSpaceZero);
    pdcch_cc->searchSpaceZero = NULL;
  }
  if((int)*pdcch_cc->controlResourceSetZero == -1) {
    free(pdcch_cc->controlResourceSetZero);
    pdcch_cc->controlResourceSetZero = NULL;
  }

  // fix UL absolute frequency
  if ((int)*scc->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA==-1) {
     free(scc->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA);
     scc->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA = NULL;
  }

  // default value for msg3 precoder is NULL (0 means enabled)
  if (*scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg3_transformPrecoder!=0)
    scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg3_transformPrecoder = NULL;

  frame_type_t frame_type = get_frame_type((int)*scc->downlinkConfigCommon->frequencyInfoDL->frequencyBandList.list.array[0], *scc->ssbSubcarrierSpacing);

  // prepare DL Allocation lists
  nr_rrc_config_dl_tda(scc->downlinkConfigCommon->initialDownlinkBWP->pdsch_ConfigCommon->choice.setup->pdsch_TimeDomainAllocationList,
                       frame_type, scc->tdd_UL_DL_ConfigurationCommon,
                       scc->downlinkConfigCommon->frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth);

  if (frame_type == FDD) {
    ASN_STRUCT_FREE(asn_DEF_NR_TDD_UL_DL_ConfigCommon, scc->tdd_UL_DL_ConfigurationCommon);
    scc->tdd_UL_DL_ConfigurationCommon = NULL;
  } else { // TDD
    // if (scc->tdd_UL_DL_ConfigurationCommon->pattern2->dl_UL_TransmissionPeriodicity > 320 ) {
    //   free(scc->tdd_UL_DL_ConfigurationCommon->pattern2);
    //   scc->tdd_UL_DL_ConfigurationCommon->pattern2 = NULL;
    // }

  }

  if ((int)*scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg1_SubcarrierSpacing == -1) {
    free(scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg1_SubcarrierSpacing);
    scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg1_SubcarrierSpacing=NULL;
  }

  if (scc->uplinkConfigCommon->initialUplinkBWP->ext1
      && (int)scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16
                 ->msgA_PUSCH_ResourceGroupA_r16->msgA_PUSCH_TimeDomainOffset_r16
             == 0) {
    free(scc->uplinkConfigCommon->initialUplinkBWP->ext1);
    scc->uplinkConfigCommon->initialUplinkBWP->ext1 = NULL;
  }

  if ((int)*scc->n_TimingAdvanceOffset == -1) {
    free(scc->n_TimingAdvanceOffset);
    scc->n_TimingAdvanceOffset = NULL;
  }

  // check pucch_ResourceConfig
  // AssertFatal(*scc->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->pucch_ResourceCommon < 2,
	//       "pucch_ResourceConfig should be 0 or 1 for now\n");

  if (*scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17 == 0) {
    free(scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17);
    scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17 = NULL;
  }
  if (scc->ext2->ntn_Config_r17->ta_Info_r17->ta_Common_r17 == -1) {
    free(scc->ext2->ntn_Config_r17->ta_Info_r17);
    scc->ext2->ntn_Config_r17->ta_Info_r17 = NULL;
  }
  if (scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionX_r17 == LONG_MAX
      && scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionY_r17 == LONG_MAX
      && scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionZ_r17 == LONG_MAX
      && scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVX_r17 == LONG_MAX
      && scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVY_r17 == LONG_MAX
      && scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVZ_r17 == LONG_MAX) {
    free(scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17);
    free(scc->ext2->ntn_Config_r17->ephemerisInfo_r17);
    scc->ext2->ntn_Config_r17->ephemerisInfo_r17 = NULL;
  }

  if (!scc->ext2->ntn_Config_r17->cellSpecificKoffset_r17 && !scc->ext2->ntn_Config_r17->ta_Info_r17
      && !scc->ext2->ntn_Config_r17->ephemerisInfo_r17) {
    free(scc->ext2->ntn_Config_r17);
    free(scc->ext2);
    scc->ext2 = NULL;
  }
}

/* Function to allocate dedicated serving cell config strutures */
void prepare_scd(NR_ServingCellConfig_t *scd) {
  // Allocate downlink structures
  scd->downlinkBWP_ToAddModList = calloc_or_fail(1, sizeof(*scd->downlinkBWP_ToAddModList));
  scd->uplinkConfig = calloc_or_fail(1, sizeof(*scd->uplinkConfig));
  scd->uplinkConfig->uplinkBWP_ToAddModList = calloc_or_fail(1, sizeof(*scd->uplinkConfig->uplinkBWP_ToAddModList));
  scd->bwp_InactivityTimer = calloc_or_fail(1, sizeof(*scd->bwp_InactivityTimer));
  scd->uplinkConfig->firstActiveUplinkBWP_Id = calloc_or_fail(1, sizeof(*scd->uplinkConfig->firstActiveUplinkBWP_Id));
  scd->firstActiveDownlinkBWP_Id = calloc_or_fail(1, sizeof(*scd->firstActiveDownlinkBWP_Id));
  *scd->firstActiveDownlinkBWP_Id = 1;
  *scd->uplinkConfig->firstActiveUplinkBWP_Id = 1;
  scd->defaultDownlinkBWP_Id = calloc_or_fail(1, sizeof(*scd->defaultDownlinkBWP_Id));
  *scd->defaultDownlinkBWP_Id = 0;

  for (int j = 0; j < NR_MAX_NUM_BWP; j++) {

    // Downlink bandwidth part
    NR_BWP_Downlink_t *bwp = calloc_or_fail(1, sizeof(*bwp));
    bwp->bwp_Id = j+1;

    // Allocate downlink dedicated bandwidth part and PDSCH structures
    bwp->bwp_Common = calloc_or_fail(1, sizeof(*bwp->bwp_Common));
    bwp->bwp_Common->pdcch_ConfigCommon = calloc_or_fail(1, sizeof(*bwp->bwp_Common->pdcch_ConfigCommon));
    bwp->bwp_Common->pdsch_ConfigCommon = calloc_or_fail(1, sizeof(*bwp->bwp_Common->pdsch_ConfigCommon));
    bwp->bwp_Dedicated = calloc_or_fail(1, sizeof(*bwp->bwp_Dedicated));
    bwp->bwp_Dedicated->pdsch_Config = calloc_or_fail(1, sizeof(*bwp->bwp_Dedicated->pdsch_Config));
    struct NR_SetupRelease_PDSCH_Config *pdsch_Config = bwp->bwp_Dedicated->pdsch_Config;
    pdsch_Config->present = NR_SetupRelease_PDSCH_Config_PR_setup;
    pdsch_Config->choice.setup = calloc_or_fail(1, sizeof(*pdsch_Config->choice.setup));
    struct NR_PDSCH_Config *pc_setup = pdsch_Config->choice.setup;
    pc_setup->dmrs_DownlinkForPDSCH_MappingTypeA = calloc_or_fail(1, sizeof(*pc_setup->dmrs_DownlinkForPDSCH_MappingTypeA));
    struct NR_SetupRelease_DMRS_DownlinkConfig *typeA = pc_setup->dmrs_DownlinkForPDSCH_MappingTypeA;
    typeA->present = NR_SetupRelease_DMRS_DownlinkConfig_PR_setup;

    // Allocate DL DMRS and PTRS configuration
    typeA->choice.setup = calloc_or_fail(1, sizeof(*typeA->choice.setup));
    NR_DMRS_DownlinkConfig_t *NR_DMRS_DownlinkCfg = typeA->choice.setup;
    NR_DMRS_DownlinkCfg->phaseTrackingRS = calloc_or_fail(1, sizeof(*NR_DMRS_DownlinkCfg->phaseTrackingRS));
    NR_DMRS_DownlinkCfg->phaseTrackingRS->present = NR_SetupRelease_PTRS_DownlinkConfig_PR_setup;
    NR_DMRS_DownlinkCfg->phaseTrackingRS->choice.setup =
        calloc_or_fail(1, sizeof(*NR_DMRS_DownlinkCfg->phaseTrackingRS->choice.setup));
    NR_PTRS_DownlinkConfig_t *NR_PTRS_DownlinkCfg = NR_DMRS_DownlinkCfg->phaseTrackingRS->choice.setup;
    NR_PTRS_DownlinkCfg->frequencyDensity = calloc_or_fail(1, sizeof(*NR_PTRS_DownlinkCfg->frequencyDensity));
    long *dl_rbs = calloc_or_fail(2, sizeof(*dl_rbs));
    for (int i=0;i<2;i++) {
      asn1cSeqAdd(&NR_PTRS_DownlinkCfg->frequencyDensity->list, &dl_rbs[i]);
    }
    NR_PTRS_DownlinkCfg->timeDensity = calloc_or_fail(1, sizeof(*NR_PTRS_DownlinkCfg->timeDensity));
    long *dl_mcs = calloc_or_fail(3, sizeof(*dl_mcs));
    for (int i=0;i<3;i++) {
      asn1cSeqAdd(&NR_PTRS_DownlinkCfg->timeDensity->list, &dl_mcs[i]);
    }
    NR_PTRS_DownlinkCfg->epre_Ratio = calloc_or_fail(1, sizeof(*NR_PTRS_DownlinkCfg->epre_Ratio));
    NR_PTRS_DownlinkCfg->resourceElementOffset = calloc_or_fail(1, sizeof(*NR_PTRS_DownlinkCfg->resourceElementOffset));
    *NR_PTRS_DownlinkCfg->resourceElementOffset = 0;
    asn1cSeqAdd(&scd->downlinkBWP_ToAddModList->list,bwp);

    // Allocate uplink structures

    NR_PUSCH_Config_t *pusch_Config = calloc_or_fail(1, sizeof(*pusch_Config));

    // Allocate UL DMRS and PTRS structures
    pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB = calloc_or_fail(1, sizeof(*pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB));
    pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->present = NR_SetupRelease_DMRS_UplinkConfig_PR_setup;
    pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup =
        calloc_or_fail(1, sizeof(*pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup));
    NR_DMRS_UplinkConfig_t *NR_DMRS_UplinkConfig = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup;
    NR_DMRS_UplinkConfig->phaseTrackingRS = calloc_or_fail(1, sizeof(*NR_DMRS_UplinkConfig->phaseTrackingRS));
    NR_DMRS_UplinkConfig->phaseTrackingRS->present = NR_SetupRelease_PTRS_UplinkConfig_PR_setup;
    NR_DMRS_UplinkConfig->phaseTrackingRS->choice.setup =
        calloc_or_fail(1, sizeof(*NR_DMRS_UplinkConfig->phaseTrackingRS->choice.setup));
    NR_PTRS_UplinkConfig_t *NR_PTRS_UplinkConfig = NR_DMRS_UplinkConfig->phaseTrackingRS->choice.setup;
    NR_PTRS_UplinkConfig->transformPrecoderDisabled = calloc_or_fail(1, sizeof(*NR_PTRS_UplinkConfig->transformPrecoderDisabled));
    NR_PTRS_UplinkConfig->transformPrecoderDisabled->frequencyDensity =
        calloc_or_fail(1, sizeof(*NR_PTRS_UplinkConfig->transformPrecoderDisabled->frequencyDensity));
    long *n_rbs = calloc_or_fail(2, sizeof(*n_rbs));
    for (int i=0;i<2;i++) {
      asn1cSeqAdd(&NR_PTRS_UplinkConfig->transformPrecoderDisabled->frequencyDensity->list, &n_rbs[i]);
    }
    NR_PTRS_UplinkConfig->transformPrecoderDisabled->timeDensity =
        calloc_or_fail(1, sizeof(*NR_PTRS_UplinkConfig->transformPrecoderDisabled->timeDensity));
    long *ptrs_mcs = calloc_or_fail(3, sizeof(*ptrs_mcs));
    for (int i = 0; i < 3; i++) {
      asn1cSeqAdd(&NR_PTRS_UplinkConfig->transformPrecoderDisabled->timeDensity->list, &ptrs_mcs[i]);
    }
    NR_PTRS_UplinkConfig->transformPrecoderDisabled->resourceElementOffset =
        calloc_or_fail(1, sizeof(*NR_PTRS_UplinkConfig->transformPrecoderDisabled->resourceElementOffset));
    *NR_PTRS_UplinkConfig->transformPrecoderDisabled->resourceElementOffset = 0;

    // UL bandwidth part
    NR_BWP_Uplink_t *ubwp = calloc_or_fail(1, sizeof(*ubwp));
    ubwp->bwp_Id = j+1;
    ubwp->bwp_Common = calloc_or_fail(1, sizeof(*ubwp->bwp_Common));
    ubwp->bwp_Dedicated = calloc_or_fail(1, sizeof(*ubwp->bwp_Dedicated));

    ubwp->bwp_Dedicated->pusch_Config = calloc_or_fail(1, sizeof(*ubwp->bwp_Dedicated->pusch_Config));
    ubwp->bwp_Dedicated->pusch_Config->present = NR_SetupRelease_PUSCH_Config_PR_setup;
    ubwp->bwp_Dedicated->pusch_Config->choice.setup = pusch_Config;

    asn1cSeqAdd(&scd->uplinkConfig->uplinkBWP_ToAddModList->list,ubwp);
  }
}

/* This function checks dedicated serving cell configuration and performs fixes as needed */
void fix_scd(NR_ServingCellConfig_t *scd) {

  // Remove unused BWPs
  int b = 0;
  while (b<scd->downlinkBWP_ToAddModList->list.count) {
    if (scd->downlinkBWP_ToAddModList->list.array[b]->bwp_Common->genericParameters.locationAndBandwidth == 0) {
      asn_sequence_del(&scd->downlinkBWP_ToAddModList->list,b,1);
    } else {
      b++;
    }
  }

  b = 0;
  while (b<scd->uplinkConfig->uplinkBWP_ToAddModList->list.count) {
    if (scd->uplinkConfig->uplinkBWP_ToAddModList->list.array[b]->bwp_Common->genericParameters.locationAndBandwidth == 0) {
      asn_sequence_del(&scd->uplinkConfig->uplinkBWP_ToAddModList->list,b,1);
    } else {
      b++;
    }
  }

  // Check for DL PTRS parameters validity
  for (int bwp_i = 0 ; bwp_i<scd->downlinkBWP_ToAddModList->list.count; bwp_i++) {

    NR_DMRS_DownlinkConfig_t *dmrs_dl_config = scd->downlinkBWP_ToAddModList->list.array[bwp_i]->bwp_Dedicated->pdsch_Config->choice.setup->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup;
    
    if (dmrs_dl_config->phaseTrackingRS) {
      // If any of the frequencyDensity values are not set or are out of bounds, PTRS is assumed to be not present
      for (int i = dmrs_dl_config->phaseTrackingRS->choice.setup->frequencyDensity->list.count - 1; i >= 0; i--) {
        if ((*dmrs_dl_config->phaseTrackingRS->choice.setup->frequencyDensity->list.array[i] < 1)
            || (*dmrs_dl_config->phaseTrackingRS->choice.setup->frequencyDensity->list.array[i] > 276)) {
          // LOG_I(GNB_APP, "DL PTRS frequencyDensity %d not set. Assuming PTRS not present! \n", i);
          free(dmrs_dl_config->phaseTrackingRS);
          dmrs_dl_config->phaseTrackingRS = NULL;
          break;
        }
      }
    }

    if (dmrs_dl_config->phaseTrackingRS) {
      // If any of the timeDensity values are not set or are out of bounds, PTRS is assumed to be not present
      for (int i = dmrs_dl_config->phaseTrackingRS->choice.setup->timeDensity->list.count - 1; i >= 0; i--) {
        if ((*dmrs_dl_config->phaseTrackingRS->choice.setup->timeDensity->list.array[i] < 0)
            || (*dmrs_dl_config->phaseTrackingRS->choice.setup->timeDensity->list.array[i] > 29)) {
          // LOG_I(GNB_APP, "DL PTRS timeDensity %d not set. Assuming PTRS not present! \n", i);
          free(dmrs_dl_config->phaseTrackingRS);
          dmrs_dl_config->phaseTrackingRS = NULL;
          break;
        }
      }
    }

    if (dmrs_dl_config->phaseTrackingRS) {
      if (*dmrs_dl_config->phaseTrackingRS->choice.setup->resourceElementOffset > 2) {
        // LOG_I(GNB_APP, "Freeing DL PTRS resourceElementOffset \n");
        free(dmrs_dl_config->phaseTrackingRS->choice.setup->resourceElementOffset);
        dmrs_dl_config->phaseTrackingRS->choice.setup->resourceElementOffset = NULL;
      }
      if (*dmrs_dl_config->phaseTrackingRS->choice.setup->epre_Ratio > 1) {
        // LOG_I(GNB_APP, "Freeing DL PTRS epre_Ratio \n");
        free(dmrs_dl_config->phaseTrackingRS->choice.setup->epre_Ratio);
        dmrs_dl_config->phaseTrackingRS->choice.setup->epre_Ratio = NULL;
      }
    }
  }

  // Check for UL PTRS parameters validity
  for (int bwp_i = 0 ; bwp_i<scd->uplinkConfig->uplinkBWP_ToAddModList->list.count; bwp_i++) {

    NR_DMRS_UplinkConfig_t *dmrs_ul_config = scd->uplinkConfig->uplinkBWP_ToAddModList->list.array[bwp_i]->bwp_Dedicated->pusch_Config->choice.setup->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup;
    
    if (dmrs_ul_config->phaseTrackingRS) {
      // If any of the frequencyDensity values are not set or are out of bounds, PTRS is assumed to be not present
      for (int i = dmrs_ul_config->phaseTrackingRS->choice.setup->transformPrecoderDisabled->frequencyDensity->list.count-1; i >= 0; i--) {
        if ((*dmrs_ul_config->phaseTrackingRS->choice.setup->transformPrecoderDisabled->frequencyDensity->list.array[i] < 1)
            || (*dmrs_ul_config->phaseTrackingRS->choice.setup->transformPrecoderDisabled->frequencyDensity->list.array[i] > 276)) {
          // LOG_I(GNB_APP, "UL PTRS frequencyDensity %d not set. Assuming PTRS not present! \n", i);
          free(dmrs_ul_config->phaseTrackingRS);
          dmrs_ul_config->phaseTrackingRS = NULL;
          break;
        }
      }
    }

    if (dmrs_ul_config->phaseTrackingRS) {
      // If any of the timeDensity values are not set or are out of bounds, PTRS is assumed to be not present
      for (int i = dmrs_ul_config->phaseTrackingRS->choice.setup->transformPrecoderDisabled->timeDensity->list.count-1; i >= 0; i--) {
        if ((*dmrs_ul_config->phaseTrackingRS->choice.setup->transformPrecoderDisabled->timeDensity->list.array[i] < 0)
            || (*dmrs_ul_config->phaseTrackingRS->choice.setup->transformPrecoderDisabled->timeDensity->list.array[i] > 29)) {
          // LOG_I(GNB_APP, "UL PTRS timeDensity %d not set. Assuming PTRS not present! \n", i);
          free(dmrs_ul_config->phaseTrackingRS);
          dmrs_ul_config->phaseTrackingRS = NULL;
          break;
        }
      }
    }

    if (dmrs_ul_config->phaseTrackingRS) {
      // Check for UL PTRS parameters validity
      if (*dmrs_ul_config->phaseTrackingRS->choice.setup->transformPrecoderDisabled->resourceElementOffset > 2) {
        // LOG_I(GNB_APP, "Freeing UL PTRS resourceElementOffset \n");
        free(dmrs_ul_config->phaseTrackingRS->choice.setup->transformPrecoderDisabled->resourceElementOffset);
        dmrs_ul_config->phaseTrackingRS->choice.setup->transformPrecoderDisabled->resourceElementOffset = NULL;
      }
    }

  }

  if (scd->downlinkBWP_ToAddModList->list.count == 0) {
    free(scd->downlinkBWP_ToAddModList);
    scd->downlinkBWP_ToAddModList = NULL;
  }

  if (scd->uplinkConfig->uplinkBWP_ToAddModList->list.count == 0) {
    free(scd->uplinkConfig->uplinkBWP_ToAddModList);
    scd->uplinkConfig->uplinkBWP_ToAddModList = NULL;
  }
}

static void verify_gnb_param_notset(paramdef_t *params, int paramidx, const char *paramname)
{
  char aprefix[MAX_OPTNAME_SIZE * 2 + 8];
  sprintf(aprefix, "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0);
  // AssertFatal(!config_isparamset(params, paramidx),
  //             "Option \"%s." GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON
  //             ".%s\" is not allowed in this config, please remove it\n",
  //             aprefix,
  //             paramname);
}
static void verify_section_notset(configmodule_interface_t *cfg, char *aprefix, const char *secname)
{
  paramlist_def_t pl = {0};
  strncpy(pl.listname, secname, sizeof(pl.listname) - 1);
  config_getlist(cfg, &pl, NULL, 0, aprefix);
  // AssertFatal(pl.numelt == 0, "Section \"%s.%s\" not allowed in this config, please remove it\n", aprefix ? aprefix : "", secname);
}
void RCconfig_verify(configmodule_interface_t *cfg, ngran_node_t node_type)
{
  // printf("testing--> inside RCconfig_verify\n");
  paramdef_t GNBSParams[] = GNBSPARAMS_DESC;
  // config_get(cfg, GNBSParams, sizeof(GNBSParams) / sizeof(paramdef_t), NULL);
  int num_gnbs = global_vnf.numactiveGNBs;
  AssertFatal(num_gnbs == 1, "need to have a " GNB_CONFIG_STRING_GNB_LIST " section, but %d found\n", num_gnbs);
  paramlist_def_t GNBParamList = {GNB_CONFIG_STRING_GNB_LIST, NULL, 0};
  paramdef_t GNBParams[] = GNBPARAMS_DESC;

}

void RCconfig_nr_prs(void)
{
  // printf("testing--> inside RCconfig_nr_prs\n");
  uint16_t  j = 0, k = 0;
  prs_config_t *prs_config = NULL;
  char str[7][100] = {0};


  paramdef_t PRS_Params[] = PRS_PARAMS_DESC;
  paramlist_def_t PRS_ParamList = {CONFIG_STRING_PRS_CONFIG,NULL,0};

  if (global_configs.defvals.numPRSconfigs > 0) {
    for (j = 0; j < RC.nb_nr_L1_inst; j++) {

      RC.gNB[j]->prs_vars.NumPRSResources = *(PRS_ParamList.paramarray[j][NUM_PRS_RESOURCES].uptr);
      for (k = 0; k < RC.gNB[j]->prs_vars.NumPRSResources; k++)
      {
        prs_config = &RC.gNB[j]->prs_vars.prs_cfg[k];
        prs_config->PRSResourceSetPeriod[0]  = PRS_ParamList.paramarray[j][PRS_RESOURCE_SET_PERIOD_LIST].uptr[0];
        prs_config->PRSResourceSetPeriod[1]  = PRS_ParamList.paramarray[j][PRS_RESOURCE_SET_PERIOD_LIST].uptr[1];
        // per PRS resources parameters
        prs_config->SymbolStart              = PRS_ParamList.paramarray[j][PRS_SYMBOL_START_LIST].uptr[k];
        prs_config->NumPRSSymbols            = PRS_ParamList.paramarray[j][PRS_NUM_SYMBOLS_LIST].uptr[k];
        prs_config->REOffset                 = PRS_ParamList.paramarray[j][PRS_RE_OFFSET_LIST].uptr[k];
        prs_config->PRSResourceOffset        = PRS_ParamList.paramarray[j][PRS_RESOURCE_OFFSET_LIST].uptr[k];
        prs_config->NPRSID                   = PRS_ParamList.paramarray[j][PRS_ID_LIST].uptr[k];
        // Common parameters to all PRS resources
        prs_config->NumRB                    = *(PRS_ParamList.paramarray[j][PRS_NUM_RB].uptr);
        prs_config->RBOffset                 = *(PRS_ParamList.paramarray[j][PRS_RB_OFFSET].uptr);
        prs_config->CombSize                 = *(PRS_ParamList.paramarray[j][PRS_COMB_SIZE].uptr);
        prs_config->PRSResourceRepetition    = *(PRS_ParamList.paramarray[j][PRS_RESOURCE_REPETITION].uptr);
        prs_config->PRSResourceTimeGap       = *(PRS_ParamList.paramarray[j][PRS_RESOURCE_TIME_GAP].uptr);
        prs_config->MutingBitRepetition      = *(PRS_ParamList.paramarray[j][PRS_MUTING_BIT_REPETITION].uptr);
        for (int l = 0; l < PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN1_LIST].numelt; l++)
        {
          prs_config->MutingPattern1[l]      = PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN1_LIST].uptr[l];
          if (k == 0) // print only for 0th resource 
            snprintf(str[5]+strlen(str[5]),sizeof(str[5])-strlen(str[5]),"%d, ",prs_config->MutingPattern1[l]);
        }
        for (int l = 0; l < PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN2_LIST].numelt; l++)
        {
          prs_config->MutingPattern2[l]      = PRS_ParamList.paramarray[j][PRS_MUTING_PATTERN2_LIST].uptr[l];
          if (k == 0) // print only for 0th resource
            snprintf(str[6]+strlen(str[6]),sizeof(str[6])-strlen(str[6]),"%d, ",prs_config->MutingPattern2[l]);
        }

        // print to buffer
        snprintf(str[0]+strlen(str[0]),sizeof(str[0])-strlen(str[0]),"%d, ",prs_config->SymbolStart);
        snprintf(str[1]+strlen(str[1]),sizeof(str[1])-strlen(str[1]),"%d, ",prs_config->NumPRSSymbols);
        snprintf(str[2]+strlen(str[2]),sizeof(str[2])-strlen(str[2]),"%d, ",prs_config->REOffset);
        snprintf(str[3]+strlen(str[3]),sizeof(str[3])-strlen(str[3]),"%d, ",prs_config->PRSResourceOffset);
        snprintf(str[4]+strlen(str[4]),sizeof(str[4])-strlen(str[4]),"%d, ",prs_config->NPRSID);
      } // for k

      prs_config = &RC.gNB[j]->prs_vars.prs_cfg[0];

    }
  }
  else
  {
    // LOG_I(PHY,"No " CONFIG_STRING_PRS_CONFIG " configuration found..!!\n");
    SM_Logs(LOG_INFO, _PHY_, "POSITIONING CONFIGURATION PROCEDURE STATUS: \033[0;31mFAILED\033[0m ToA And AoA Configuration Not Found.");

  }
}

#define ALL_SYMBOLS_TAKEN 0x3FFF

/**
 * @brief Get number or blacklisted UL PRBs and their mapping from gNB config
 */
static int get_prb_blacklist(uint8_t instance, uint16_t *prbbl)
{
  paramdef_t GNBSParams[] = GNBSPARAMS_DESC;
  paramlist_def_t GNBParamList = {GNB_CONFIG_STRING_GNB_LIST, NULL, 0};
  // config_get(config_get_if(), GNBSParams, sizeofArray(GNBSParams), NULL);
  int num_gnbs = global_vnf.numactiveGNBs;
  AssertFatal(num_gnbs > 0, "Failed to parse config file, no GNBs found in field %s \n", GNB_CONFIG_STRING_ACTIVE_GNBS);
  paramdef_t GNBParams[] = GNBPARAMS_DESC;
  // config_getlist(config_get_if(), &GNBParamList, GNBParams, sizeofArray(GNBParams), NULL);

  int num_prbbl = 0;
  char *ulprbbl = global_vnf.ULPRBBlacklist;

  if (!ulprbbl)
    return -1;

  // LOG_D(NR_PHY, "PRB blacklist found: %s\n", ulprbbl);
  char *save = NULL;
  char *pt = strtok_r(ulprbbl, ",", &save);

  while (pt) {
    const int rb = atoi(pt);
    // AssertFatal(rb < MAX_BWP_SIZE, "RB %d out of bounds (max 275 PRBs)\n", rb);
    prbbl[rb] = ALL_SYMBOLS_TAKEN;
    // LOG_D(NR_PHY, "Blacklisting prb %d\n", rb);
    pt = strtok_r(NULL, ",", &save);
    num_prbbl++;
  }
  return num_prbbl;
}

void RCconfig_NR_L1(void)
{


  for (int j = 0; j < RC.nb_nr_L1_inst; j++) {
    PHY_VARS_gNB *gNB = RC.gNB[j];

    if (NFAPI_MODE != NFAPI_MODE_PNF) {
      paramdef_t GNBSParams[] = GNBSPARAMS_DESC;
      paramlist_def_t GNBParamList = {GNB_CONFIG_STRING_GNB_LIST, NULL, 0};
      int num_gnbs = global_vnf.numactiveGNBs;
      paramdef_t GNBParams[] = GNBPARAMS_DESC;
      int N1 = global_configs.defvals.pdsch_AntennaPorts_N1;
      int N2 = global_configs.defvals.pdsch_AntennaPorts_N2;
      int XP = global_configs.defvals.pdsch_AntennaPorts_XP;

      // PRB Blacklist
      uint16_t prbbl[MAX_BWP_SIZE] = {0};
      int num_ulprbbl = get_prb_blacklist(j, prbbl);
      if (num_ulprbbl != -1) {
        RC.gNB[j]->num_ulprbbl = num_ulprbbl;
        // LOG_D(NR_PHY, "Copying %d blacklisted PRB to L1 context\n", RC.gNB[j]->num_ulprbbl);
        memcpy(RC.gNB[j]->ulprbbl, prbbl, MAX_BWP_SIZE * sizeof(prbbl[0]));
      }

      // Antenna ports
      gNB->ap_N1 = N1;
      gNB->ap_N2 = N2;
      gNB->ap_XP = XP;
    }
    //printf("just before L1 Params\n");
    // L1 params
    paramdef_t L1_Params[] = L1PARAMS_DESC;
    paramlist_def_t L1_ParamList = {CONFIG_STRING_L1_LIST, NULL, 0};
    // config_getlist(config_get_if(), &L1_ParamList, L1_Params, sizeofArray(L1_Params), NULL);
    if (global_vnf.numL1 > 0) {
      AssertFatal(global_configs.defvals.L1_THREAD_POOL_SIZ == 2022,
                  "thread_pool_size removed, please use --thread-pool\n");
      gNB->ofdm_offset_divisor = global_configs.defvals.OFDM_offset_divisor;
      gNB->pucch0_thres = global_configs.L1s.pucch0_dtx_threshold;
      gNB->prach_thres = global_configs.L1s.prach_dtx_threshold;
      gNB->pusch_thres = global_configs.L1s.pusch_dtx_threshold;
      gNB->srs_thres = global_configs.defvals.L1_SRS_DTX_THRES;
      gNB->max_ldpc_iterations = global_configs.L1s.max_ldpc_iterations;
      gNB->L1_rx_thread_core = global_configs.L1s.L1_rx_thread_core;
      gNB->L1_tx_thread_core = global_configs.L1s.L1_tx_thread_core;
      LOG_I(NR_PHY, "L1_RX_THREAD_CORE %d (%d)\n", global_configs.L1s.L1_rx_thread_core, L1_RX_THREAD_CORE);
      gNB->TX_AMP = (int16_t)(32767.0 / pow(10.0, .05 * (double)(global_configs.L1s.tx_amp_backoff_dB)));
      gNB->phase_comp = global_configs.L1s.phase_compensation;
      LOG_I(NR_PHY, "TX_AMP = %d (-%d dBFS)\n", gNB->TX_AMP, global_configs.L1s.tx_amp_backoff_dB);
      SM_Logs(LOG_INFO, _ARIA_, "LAYER 1 Parameters: Transmission Amplitude: %d , Receiving Thread Cores: %d", RC.gNB[j]->TX_AMP, (global_configs.L1s.L1_rx_thread_core) );
      AssertFatal(gNB->TX_AMP > 300, "TX_AMP is too small, must be larger than 300 (is %d)\n", gNB->TX_AMP);
      // Midhaul configuration
      if (strcmp(global_configs.L1s.tr_n_preference, "local_mac") == 0) {
        // do nothing
      } else if (strcmp(global_configs.L1s.tr_n_preference, "nfapi") == 0) {
        gNB->eth_params_n.my_addr = strdup(global_configs.L1s.local_n_address);
        gNB->eth_params_n.remote_addr = strdup(global_configs.L1s.remote_n_address);
        gNB->eth_params_n.my_portc = global_configs.L1s.local_n_portc;
        gNB->eth_params_n.remote_portc = global_configs.L1s.remote_n_portc;
        gNB->eth_params_n.my_portd = global_configs.L1s.local_n_portd;
        gNB->eth_params_n.remote_portd = global_configs.L1s.remote_n_portd;
        gNB->eth_params_n.transp_preference = ETH_UDP_MODE;
        printf("just before configure_nr_nfapi_pnf\n");
        configure_nr_nfapi_pnf(gNB->eth_params_n.remote_addr,
                               gNB->eth_params_n.remote_portc,
                               gNB->eth_params_n.my_addr,
                               gNB->eth_params_n.my_portd,
                               gNB->eth_params_n.remote_portd);
                               printf("just after configure_nr_nfapi_pnf\n");
      } else {
        // other midhaul, do nothing
      }
      // LOG_D(NR_PHY, "Initializing northbound interface for L1\n");
    SM_Logs(LOG_INFO, _PHY_, "NORTHBOUND INTERFACE FOR LAYER 1 PREPARED" );
      l1_north_init_gNB();
    } else {
      // LOG_E(NR_PHY, "No " CONFIG_STRING_L1_LIST " configuration found");
    }
  }
}


void Config_gscc(NR_ServingCellConfigCommon_t *scc, paramdef_t *SCCsParams, size_t param_count) {

    if (scc == NULL || SCCsParams == NULL || global_vnf.GSCC == NULL) {
        printf("Invalid parameters. Ensure 'scc', 'SCCsParams', and 'global_vnf.GSCC' are not NULL.\n");
        return;
    }
    for (size_t i = 0; i < param_count; i++) {
        paramdef_t *param = &SCCsParams[i];

        if (param->i64ptr != NULL) {
                *scc->physCellId = (global_vnf.GSCC->physCellId);// ? global_vnf.GSCC->physCellId : param->defint64val;
                *scc->n_TimingAdvanceOffset = (global_vnf.GSCC->n_TimingAdvanceOffset != 0) ? global_vnf.GSCC->n_TimingAdvanceOffset : param->defint64val;

                *scc->ssb_periodicityServingCell                                                                                                = (global_vnf.GSCC->ssb_periodicityServingCell != 0) ? global_vnf.GSCC->ssb_periodicityServingCell : param->defint64val;
                scc->dmrs_TypeA_Position                                                                                                       = (global_vnf.GSCC->dmrs_TypeA_Position);// ? global_vnf.GSCC->dmrs_TypeA_Position : param->defint64val;
                *scc->ssbSubcarrierSpacing                                                                                                     = (global_vnf.GSCC->ssbSubcarrierSpacing != 0) ? global_vnf.GSCC->ssbSubcarrierSpacing : param->defint64val;
                *scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencySSB                                                              = (global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencySSB != 0) ? global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencySSB : param->defint64val;
                *scc->downlinkConfigCommon->frequencyInfoDL->frequencyBandList.list.array[0]                                                   = (global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->frequencyBandList->list.array[0] != 0) ? global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->frequencyBandList->list.array[0] : param->defint64val;
                scc->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencyPointA                                                            = (global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencyPointA != 0) ? global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencyPointA : param->defint64val;
                scc->downlinkConfigCommon->frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->offsetToCarrier                             = global_vnf.dloffsetToCarrier;
                scc->downlinkConfigCommon->frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing                           = global_vnf.dlsubcarrierSpacing;
                scc->downlinkConfigCommon->frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth                            = global_vnf.dlcarrierBandwidth;
                scc->downlinkConfigCommon->initialDownlinkBWP->genericParameters.locationAndBandwidth                                          = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->genericParameters->locationAndBandwidth != 0) ? global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->genericParameters->locationAndBandwidth : param->defint64val;
                scc->downlinkConfigCommon->initialDownlinkBWP->genericParameters.subcarrierSpacing                                             = (global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->genericParameters->subcarrierSpacing != 0) ? global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->genericParameters->subcarrierSpacing : param->defint64val;
                *scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->controlResourceSetZero                        = (global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->controlResourceSetZero != 0) ? global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->controlResourceSetZero : param->defint64val;
                *scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->searchSpaceZero                               = (global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->searchSpaceZero); //? global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->searchSpaceZero : param->defint64val;
                *scc->uplinkConfigCommon->frequencyInfoUL->frequencyBandList->list.array[0]                                                     = (global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->frequencyBandList->list.array[0] != 0) ? global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->frequencyBandList->list.array[0] : param->defint64val;
                *scc->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA                                                              = (global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA != 0) ? global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA : param->defint64val; 
                scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->offsetToCarrier                               = global_vnf.uloffsetToCarrier;
                scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing                             = global_vnf.ulsubcarrierSpacing;
                scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth                              = global_vnf.ulcarrierBandwidth;
                *scc->uplinkConfigCommon->frequencyInfoUL->p_Max                                                                                = (global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->p_Max != 0) ? global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->p_Max : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->genericParameters.locationAndBandwidth                                              = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->genericParameters->locationAndBandwidth != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->genericParameters->locationAndBandwidth : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->genericParameters.subcarrierSpacing                                                 = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->genericParameters->subcarrierSpacing != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->genericParameters->subcarrierSpacing : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric.prach_ConfigurationIndex        = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->prach_ConfigurationIndex != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->prach_ConfigurationIndex : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric.msg1_FDM                        = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->msg1_FDM); // ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->msg1_FDM : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric.msg1_FrequencyStart             = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->msg1_FrequencyStart);// ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->msg1_FrequencyStart : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric.zeroCorrelationZoneConfig       = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->zeroCorrelationZoneConfig != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->zeroCorrelationZoneConfig : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric.preambleReceivedTargetPower     = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->preambleReceivedTargetPower != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->preambleReceivedTargetPower : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric.preambleTransMax                = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->preambleTransMax != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->preambleTransMax : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric.powerRampingStep                = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->powerRampingStep != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->powerRampingStep : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric.ra_ResponseWindow               = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->ra_ResponseWindow != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->ra_ResponseWindow : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->present = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->present != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->present : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->choice.one = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->choice.one != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->choice.one : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ra_ContentionResolutionTimer                       = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ra_ContentionResolutionTimer != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ra_ContentionResolutionTimer : param->defint64val;
                *scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rsrp_ThresholdSSB                                  = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rsrp_ThresholdSSB != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rsrp_ThresholdSSB : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.present                    = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.present != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.present : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.choice.l139                = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.choice.l139 != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.choice.l139 : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->restrictedSetConfig                                = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->restrictedSetConfig);// ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->restrictedSetConfig : param->defint64val;
                *scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg3_transformPrecoder                             = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg3_transformPrecoder != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg3_transformPrecoder : param->defint64val;
                *scc->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->msg3_DeltaPreamble                                = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->msg3_DeltaPreamble != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->msg3_DeltaPreamble : param->defint64val;
                *scc->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->p0_NominalWithGrant                               = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->p0_NominalWithGrant != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->p0_NominalWithGrant : param->defint64val;
                scc->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->pucch_GroupHopping                                = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.pucchGroupHopping;
                *scc->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->hoppingId                                         = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.hoppingId;
                *scc->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->p0_nominal                                        = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.p0_nominal;
                *scc->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->pucch_ResourceCommon                              = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->pucch_ResourceCommon);// ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->pucch_ResourceCommon : param->defint64val;
                scc->tdd_UL_DL_ConfigurationCommon->referenceSubcarrierSpacing                                                                 = (global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->referenceSubcarrierSpacing != 0) ? global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->referenceSubcarrierSpacing : param->defint64val;
                scc->tdd_UL_DL_ConfigurationCommon->pattern1.dl_UL_TransmissionPeriodicity                                                     = (global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->dl_UL_TransmissionPeriodicity != 0) ? global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->dl_UL_TransmissionPeriodicity : param->defint64val;
                scc->tdd_UL_DL_ConfigurationCommon->pattern1.nrofDownlinkSlots                                                                 = (global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofDownlinkSlots != 0) ? global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofDownlinkSlots : param->defint64val;
                scc->tdd_UL_DL_ConfigurationCommon->pattern1.nrofDownlinkSymbols                                                               = (global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofDownlinkSymbols != 0) ? global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofDownlinkSymbols : param->defint64val;
                scc->tdd_UL_DL_ConfigurationCommon->pattern1.nrofUplinkSlots                                                                   = (global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofUplinkSlots != 0) ? global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofUplinkSlots : param->defint64val;
                scc->tdd_UL_DL_ConfigurationCommon->pattern1.nrofUplinkSymbols                                                                 = (global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofUplinkSymbols != 0) ? global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofUplinkSymbols : param->defint64val;


                (scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionX_r17) = global_vnf.ephemerisPositionX;
                (scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionY_r17) = global_vnf.ephemerisPositionY;
                  (scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->positionZ_r17) = global_vnf.ephemerisPositionZ;
                (scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVX_r17) =  global_vnf.ephemerisVelocityVX;
                  (scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVY_r17) = global_vnf.ephemerisVelocityVY;
                  (scc->ext2->ntn_Config_r17->ephemerisInfo_r17->choice.positionVelocity_r17->velocityVZ_r17) = global_vnf.ephemerisVelocityVZ;
                  (scc->ext2->ntn_Config_r17->ta_Info_r17->ta_Common_r17) = global_vnf.taCommon;
                        



                                                                        
                scc->ss_PBCH_BlockPower                                                                                                        = global_vnf.tddConfigCommon.ssPBCH_BlockPower;
                *scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg1_SubcarrierSpacing                            = (global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg1_SubcarrierSpacing != 0) ? global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg1_SubcarrierSpacing : param->defint64val;
                
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->totalNumberOfRA_Preambles                          = 0;
                scc->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rsrp_ThresholdSSB_SUL                              = 0;
                *scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->rach_ConfigCommonTwoStepRA_r16.rach_ConfigGenericTwoStepRA_r16.msgB_ResponseWindow_r16              = global_vnf.mSCC.msgB_ResponseWindow_r16;
                *scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->rach_ConfigCommonTwoStepRA_r16.msgA_RSRP_Threshold_r16                                              = global_vnf.mSCC.msgA_RSRP_Threshold_r16;
                scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->msgA_MCS_r16                                   = global_vnf.mSCC.msgA_MCS_r16;
                scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->nrofSlotsMsgA_PUSCH_r16                        = global_vnf.mSCC.nrofSlotsMsgA_PUSCH_r16;
                scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->nrofMsgA_PO_PerSlot_r16                        = global_vnf.mSCC.nrofMsgA_PO_PerSlot_r16 ;
                scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->msgA_PUSCH_TimeDomainOffset_r16                = global_vnf.mSCC.msgA_PUSCH_TimeDomainOffset_r16;
                *scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->startSymbolAndLengthMsgA_PO_r16               = global_vnf.mSCC.startSymbolAndLengthMsgA_PO_r16;
                scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->mappingTypeMsgA_PUSCH_r16                      = global_vnf.mSCC.mappingTypeMsgA_PUSCH_r16;
                scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->guardBandMsgA_PUSCH_r16                        = global_vnf.mSCC.guardBandMsgA_PUSCH_r16;
                scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->frequencyStartMsgA_PUSCH_r16                   = global_vnf.mSCC.frequencyStartMsgA_PUSCH_r16;
                scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->nrofPRBs_PerMsgA_PO_r16                        = global_vnf.mSCC.nrofPRBs_PerMsgA_PO_r16;
                scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->nrofMsgA_PO_FDM_r16                            = global_vnf.mSCC.nrofMsgA_PO_FDM_r16;
                scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->msgA_DMRS_Config_r16.msgA_PUSCH_NrofPorts_r16  = global_vnf.mSCC.msgA_PUSCH_NrofPorts_r16;
                scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_PUSCH_ResourceGroupA_r16->nrofDMRS_Sequences_r16                         = global_vnf.mSCC.nrofDMRS_Sequences_r16;
                *scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->msgA_PUSCH_Config_r16->msgA_TransformPrecoder_r16                                                   = global_vnf.mSCC.msgA_TransformPrecoder_r16;
                *scc->uplinkConfigCommon->initialUplinkBWP->ext1->msgA_ConfigCommon_r16->choice.setup->rach_ConfigCommonTwoStepRA_r16.msgA_CB_PreamblesPerSSB_PerSharedRO_r16                              = global_vnf.mSCC.msgA_CB_PreamblesPerSSB_PerSharedRO_r16;

        } else {
            //printf("Parameter index %zu is invalid (param->i64ptr is NULL).\n", i);
            ;
        }
    }
}


static NR_ServingCellConfigCommon_t *get_scc_config(configmodule_interface_t *cfg, int minRXTXTIME)
{
  // printf("testing--> inside get_scc_config\n");
  NR_ServingCellConfigCommon_t *scc = calloc_or_fail(1, sizeof(*scc));
  uint64_t ssb_bitmap=0xff;
  prepare_scc(scc);
  paramdef_t SCCsParams[] = SCCPARAMS_DESC(scc);
  prepare_msgA_scc(scc);
  // printf("just after prepare_msgA_scc\n");
  paramdef_t MsgASCCsParams[] = MSGASCCPARAMS_DESC(scc);

  // config_getlist(cfg, &MsgASCCsParamList, NULL, 0, aprefix);

  if (global_vnf.numServingcellcmn > 0 || global_vnf.numMsgASCCsParam > 0) {
    // printf("just inside if (global_vnf.numServingcellcmn > 0 || global_vnf.numMsgASCCsParam > 0) {\n");
    // sprintf(aprefix, "%s.[%i].%s.[%i]", GNB_CONFIG_STRING_GNB_LIST,0,GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON, 0);
    // config_get(cfg, SCCsParams, sizeofArray(SCCsParams), aprefix);
    // config_get(cfg, MsgASCCsParams, sizeofArray(MsgASCCsParams), aprefix);

    size_t param_count = sizeof(SCCsParams) / sizeof(SCCsParams[0]);
    // printf("just before Config_gscc\n");
    Config_gscc(scc, SCCsParams, param_count);
    // printf("just after Config_gscc\n");
    struct NR_FrequencyInfoDL *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;

    uint64_t ssb_bitmap=0x1;


    SM_Logs(LOG_INFO, _RRC_, "ARIA CELL ID: %d, DOWNLINK BAND: %d",(int)*scc->physCellId, (int)*frequencyInfoDL->frequencyBandList.list.array[0] );
    uint64_t ssb_freq = from_nrarfcn(*frequencyInfoDL->frequencyBandList.list.array[0],
                                     *scc->ssbSubcarrierSpacing,
                                     *frequencyInfoDL->absoluteFrequencySSB);
    // LOG_I(RRC, "absoluteFrequencySSB %ld corresponds to %lu Hz\n", *frequencyInfoDL->absoluteFrequencySSB, ssb_freq);
    if (IS_SA_MODE(get_softmodem_params()))
      check_ssb_raster(ssb_freq, *frequencyInfoDL->frequencyBandList.list.array[0], *scc->ssbSubcarrierSpacing);
    fix_scc(scc, ssb_bitmap);
  }
  nr_rrc_config_ul_tda(scc, minRXTXTIME);

  NR_PDCCH_ConfigCommon_t *pcc = scc->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup;
  // AssertFatal(pcc != NULL && pcc->commonSearchSpaceList == NULL, "memory leak\n");
  pcc->commonSearchSpaceList = calloc_or_fail(1, sizeof(*pcc->commonSearchSpaceList));

  NR_SearchSpace_t *ss1 = rrc_searchspace_config(true, 1, 0);
  asn1cSeqAdd(&pcc->commonSearchSpaceList->list, ss1);
  NR_SearchSpace_t *ss2 = rrc_searchspace_config(true, 2, 0);
  asn1cSeqAdd(&pcc->commonSearchSpaceList->list, ss2);
  NR_SearchSpace_t *ss3 = rrc_searchspace_config(true, 3, 0);
  asn1cSeqAdd(&pcc->commonSearchSpaceList->list, ss3);

  asn1cCallocOne(pcc->searchSpaceSIB1,  0);
  asn1cCallocOne(pcc->ra_SearchSpace, 1);
  asn1cCallocOne(pcc->pagingSearchSpace, 2);
  asn1cCallocOne(pcc->searchSpaceOtherSystemInformation, 3);

  return scc;
}

static NR_ServingCellConfig_t *get_scd_config(configmodule_interface_t *cfg)
{
  NR_ServingCellConfig_t *scd = calloc(1, sizeof(*scd));
  prepare_scd(scd);
  paramdef_t SCDsParams[] = SCDPARAMS_DESC(scd);
  paramlist_def_t SCDsParamList = {GNB_CONFIG_STRING_SERVINGCELLCONFIGDEDICATED, NULL, 0};

  char aprefix[MAX_OPTNAME_SIZE * 2 + 8];
  snprintf(aprefix, sizeof(aprefix), "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0);
  // config_getlist(cfg, &SCDsParamList, NULL, 0, aprefix);
  if (global_vnf.numSCDconfigs > 0) {
    sprintf(aprefix, "%s.[%i].%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0, GNB_CONFIG_STRING_SERVINGCELLCONFIGDEDICATED, 0);
    config_get(cfg, SCDsParams, sizeof(SCDsParams) / sizeof(paramdef_t), aprefix);
    const NR_BWP_UplinkDedicated_t *bwp_Dedicated = scd->uplinkConfig->uplinkBWP_ToAddModList->list.array[0]->bwp_Dedicated;
    const NR_PTRS_UplinkConfig_t *setup =
        bwp_Dedicated->pusch_Config->choice.setup->dmrs_UplinkForPUSCH_MappingTypeB->choice.setup->phaseTrackingRS->choice.setup;
  }
  fix_scd(scd);

  return scd;
}

static int read_du_cell_info(configmodule_interface_t *cfg,
                             bool separate_du,
                             uint32_t *gnb_id,
                             uint64_t *gnb_du_id,
                             char **name,
                             f1ap_served_cell_info_t *info,
                             int max_cell_info)
{
  // AssertFatal(max_cell_info == 1, "only one cell supported\n");
  memset(info, 0, sizeof(*info));

  paramdef_t GNBSParams[] = GNBSPARAMS_DESC;
  paramdef_t GNBParams[]  = GNBPARAMS_DESC;
  paramlist_def_t GNBParamList = {GNB_CONFIG_STRING_GNB_LIST,NULL,0};
  //config_get(cfg, GNBSParams, sizeof(GNBSParams) / sizeof(paramdef_t), NULL);
  //int num_gnbs = GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt;
  int num_gnbs = global_vnf.numactiveGNBs;
  AssertFatal(num_gnbs == 1, "cannot configure DU: required config section \"gNBs\" missing\n");

  // Output a list of all eNBs.
  //config_getlist(cfg, &GNBParamList, GNBParams, sizeof(GNBParams) / sizeof(paramdef_t), NULL);

  // read the gNB-ID. The DU itself only needs the gNB-DU ID, but some (e.g.,
  // E2 agent) need the gNB-ID as well if it is running in a separate process
  //AssertFatal(config_isparamset(GNBParamList.paramarray[0], GNB_GNB_ID_IDX), "%s is not defined in configuration file\n", GNB_CONFIG_STRING_GNB_ID);
  *gnb_id = global_vnf.gNBs->gNB_ID;
  // printf("GNB ID: %lu\n", *gnb_id);

  AssertFatal(strcmp(GlobC_ARIAConfs.FunctionalNodes, GlobC_ARIAConfs.FunctionalNodes) == 0,
              "no active gNB found/mismatch of gNBs: %s vs %s\n",
              global_vnf.active_gNBs,
              global_vnf.gNBs->gNB_name);

  char aprefix[MAX_OPTNAME_SIZE * 2 + 8];
  sprintf(aprefix, "%s.[0]", GNB_CONFIG_STRING_GNB_LIST);
  paramdef_t PLMNParams[] = GNBPLMNPARAMS_DESC;
  /* map parameter checking array instances to parameter definition array instances */
  checkedparam_t config_check_PLMNParams[] = PLMNPARAMS_CHECK;
  for (int I = 0; I < sizeof(PLMNParams) / sizeof(paramdef_t); ++I)
    PLMNParams[I].chkPptr = &(config_check_PLMNParams[I]);
  paramlist_def_t PLMNParamList = {GNB_CONFIG_STRING_PLMN_LIST, NULL, 0};
  // printf("Hi i am also: \n");
  //config_getlist(cfg, &PLMNParamList, PLMNParams, sizeof(PLMNParams) / sizeof(paramdef_t), aprefix);
  //AssertFatal(PLMNParamList.numelt > 0, "need to have a PLMN in PLMN section\n");
  //AssertFatal(PLMNParamList.numelt == 1, "cannot have more than one PLMN\n");

  // if fronthaul is F1, require gNB_DU_ID, else use gNB_ID
  if (separate_du) {
    //AssertFatal(config_isparamset(GNBParamList.paramarray[0], GNB_GNB_DU_ID_IDX), "%s is not defined in configuration file\n", GNB_CONFIG_STRING_GNB_DU_ID);
    *gnb_du_id = global_vnf.gNBs->gNB_DU_ID;
  } else {
    // AssertFatal(!config_isparamset(GNBParamList.paramarray[0], GNB_GNB_DU_ID_IDX),
    //             "%s should not be defined in configuration file for monolithic gNB\n",
    //             GNB_CONFIG_STRING_GNB_DU_ID);
    *gnb_du_id = *gnb_id; // the gNB-DU ID is equal to the gNB ID, since the config has no gNB-DU ID
  }

  *name = global_vnf.gNBs->gNB_name;
  info->tac = malloc(sizeof(*info->tac));
  AssertFatal(info->tac != NULL, "out of memory\n");
  *info->tac = (uint32_t)global_vnf.gNBs->tracking_area_code;;
  info->plmn.mcc = global_vnf.plmn_list->mcc;
  info->plmn.mnc = global_vnf.plmn_list->mnc;
  info->plmn.mnc_digit_length = global_vnf.plmn_list->mnc_length;
  AssertFatal((info->plmn.mnc_digit_length == 2) || (info->plmn.mnc_digit_length == 3),
              "BAD MNC DIGIT LENGTH %d",
              info->plmn.mnc_digit_length);
  info->nr_cellid = (uint64_t) global_vnf.gNBs->nr_cellid;

  paramdef_t SNSSAIParams[] = GNBSNSSAIPARAMS_DESC;
  paramlist_def_t SNSSAIParamList = {GNB_CONFIG_STRING_SNSSAI_LIST, NULL, 0};
  checkedparam_t config_check_SNSSAIParams[] = SNSSAIPARAMS_CHECK;
  for (int J = 0; J < sizeofArray(SNSSAIParams); ++J)
    SNSSAIParams[J].chkPptr = &(config_check_SNSSAIParams[J]);
  char snssaistr[MAX_OPTNAME_SIZE * 2 + 8];
  sprintf(snssaistr, "%s.[0].%s.[0]", GNB_CONFIG_STRING_GNB_LIST, GNB_CONFIG_STRING_PLMN_LIST);
  //config_getlist(config_get_if(), &SNSSAIParamList, SNSSAIParams, sizeofArray(SNSSAIParams), snssaistr);
  info->num_ssi = SNSSAIParamList.numelt;
  for (int s = 0; s < info->num_ssi; ++s) {
    info->nssai[s].sst = global_vnf.plmn_list->snssaiList->sst;
    info->nssai[s].sd = 0xffffff; //Need to be hardcoded somewhere else
    AssertFatal(info->nssai[s].sd <= 0xffffff, "SD cannot be bigger than 0xffffff, but is %d\n", info->nssai[s].sd);
  }

  return 1;
}

static f1ap_tdd_info_t read_tdd_config(const NR_ServingCellConfigCommon_t *scc)
{
  const NR_FrequencyInfoDL_t *dl = scc->downlinkConfigCommon->frequencyInfoDL;
  f1ap_tdd_info_t tdd = {
      .freqinfo.arfcn = dl->absoluteFrequencyPointA,
      .freqinfo.band = *dl->frequencyBandList.list.array[0],
      .tbw.scs = dl->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing,
      .tbw.nrb = dl->scs_SpecificCarrierList.list.array[0]->carrierBandwidth,
  };
  return tdd;
}

static f1ap_fdd_info_t read_fdd_config(const NR_ServingCellConfigCommon_t *scc)
{
  const NR_FrequencyInfoDL_t *dl = scc->downlinkConfigCommon->frequencyInfoDL;
  const NR_FrequencyInfoUL_t *ul = scc->uplinkConfigCommon->frequencyInfoUL;
  f1ap_fdd_info_t fdd = {
      .dl_freqinfo.arfcn = dl->absoluteFrequencyPointA,
      .ul_freqinfo.arfcn = *ul->absoluteFrequencyPointA,
      .dl_tbw.scs = dl->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing,
      .ul_tbw.scs = ul->scs_SpecificCarrierList.list.array[0]->subcarrierSpacing,
      .dl_tbw.nrb = dl->scs_SpecificCarrierList.list.array[0]->carrierBandwidth,
      .ul_tbw.nrb = ul->scs_SpecificCarrierList.list.array[0]->carrierBandwidth,
      .dl_freqinfo.band = *dl->frequencyBandList.list.array[0],
      .ul_freqinfo.band = *ul->frequencyBandList->list.array[0],
  };
  return fdd;
}

static f1ap_setup_req_t *RC_read_F1Setup(uint64_t id,
                                         const char *name,
                                         const f1ap_served_cell_info_t *info,
                                         const NR_ServingCellConfigCommon_t *scc,
                                         NR_BCCH_BCH_Message_t *mib,
                                         const NR_BCCH_DL_SCH_Message_t *sib1)
{
  f1ap_setup_req_t *req = calloc(1, sizeof(*req));
  // AssertFatal(req != NULL, "out of memory\n");
  req->gNB_DU_id = id;
  req->gNB_DU_name = strdup(name);
  req->num_cells_available = 1;
  req->cell[0].info = *info;
  // LOG_I(GNB_APP,
  //       "F1AP: gNB idx %d gNB_DU_id %ld, gNB_DU_name %s, TAC %d MCC/MNC/length %d/%d/%d cellID %ld\n",
  //       0,
  //       req->gNB_DU_id,
  //       req->gNB_DU_name,
  //       *req->cell[0].info.tac,
  //       req->cell[0].info.plmn.mcc,
  //       req->cell[0].info.plmn.mnc,
  //       req->cell[0].info.plmn.mnc_digit_length,
  //       req->cell[0].info.nr_cellid);
  SM_Logs(LOG_INFO, _ARIA_, "F1AP CRITICAL PARAMETERS ARE SET AS: MCC %d, MNC %d TAC %d", req->cell[0].info.plmn.mcc, req->cell[0].info.plmn.mnc, *req->cell[0].info.tac);

  req->cell[0].info.nr_pci = *scc->physCellId;
  if (scc->tdd_UL_DL_ConfigurationCommon) {
    // LOG_I(GNB_APP, "ngran_DU: Configuring Cell %d for TDD\n", 0);
    SM_Logs(LOG_INFO, _GNB_APP_, "Detected Cell Configured according to: TDD");
    req->cell[0].info.mode = F1AP_MODE_TDD;
    req->cell[0].info.tdd = read_tdd_config(scc);
  } else {
    // LOG_I(GNB_APP, "ngran_DU: Configuring Cell %d for FDD\n", 0);
    SM_Logs(LOG_INFO, _GNB_APP_, "Detected Cell Configured according to: FDD");
    req->cell[0].info.mode = F1AP_MODE_FDD;
    req->cell[0].info.fdd = read_fdd_config(scc);
  }

  NR_MeasurementTimingConfiguration_t *mtc = get_new_MeasurementTimingConfiguration(scc);
  uint8_t buf[1024];
  int len = encode_MeasurementTimingConfiguration(mtc, buf, sizeof(buf));
  // DevAssert(len <= sizeof(buf));
  free_MeasurementTimingConfiguration(mtc);
  uint8_t *mtc_buf = calloc(len, sizeof(*mtc_buf));
  // AssertFatal(mtc_buf != NULL, "out of memory\n");
  memcpy(mtc_buf, buf, len);
  req->cell[0].info.measurement_timing_config = mtc_buf;
  req->cell[0].info.measurement_timing_config_len = len;

  if (IS_SA_MODE(get_softmodem_params())) {
    // in NSA we don't transmit SIB1, so cannot fill DU system information
    // so cannot send MIB either

    int buf_len = 3; // this is what we assume in monolithic
    req->cell[0].sys_info = calloc(1, sizeof(*req->cell[0].sys_info));
    AssertFatal(req->cell[0].sys_info != NULL, "out of memory\n");
    f1ap_gnb_du_system_info_t *sys_info = req->cell[0].sys_info;
    sys_info->mib = calloc(buf_len, sizeof(*sys_info->mib));
    // DevAssert(sys_info->mib != NULL);
    // DevAssert(mib != NULL);
    // encode only the mib message itself
    sys_info->mib_length = encode_MIB_NR_setup(mib->message.choice.mib, 0, sys_info->mib, buf_len);
    // DevAssert(sys_info->mib_length == buf_len);

    // DevAssert(sib1 != NULL);
    NR_SIB1_t *bcch_SIB1 = sib1->message.choice.c1->choice.systemInformationBlockType1;
    sys_info->sib1 = calloc(NR_MAX_SIB_LENGTH / 8, sizeof(*sys_info->sib1));
    asn_enc_rval_t enc_rval = uper_encode_to_buffer(&asn_DEF_NR_SIB1, NULL, (void *)bcch_SIB1, sys_info->sib1, NR_MAX_SIB_LENGTH / 8);
    // AssertFatal(enc_rval.encoded > 0, "ASN1 message encoding failed (%s, %lu)!\n", enc_rval.failed_type->name, enc_rval.encoded);
    sys_info->sib1_length = (enc_rval.encoded + 7) / 8;
  }

  int num = read_version(TO_STRING(NR_RRC_VERSION), &req->rrc_ver[0], &req->rrc_ver[1], &req->rrc_ver[2]);
  // AssertFatal(num == 3, "could not read RRC version string %s\n", TO_STRING(NR_RRC_VERSION));

  return req;
}

void RCconfig_nr_macrlc(configmodule_interface_t *cfg)
{
  // printf("testing--> inside RCconfig_nr_macrlc\n");
  int j = 0;

  paramlist_def_t GNBParamList = {GNB_CONFIG_STRING_GNB_LIST, NULL, 0};
  paramdef_t GNBSParams[] = GNBSPARAMS_DESC;
  // config_get(cfg, GNBSParams, sizeofArray(GNBSParams), NULL);
  int num_gnbs = global_vnf.numactiveGNBs;
  // AssertFatal(num_gnbs == 1,
  //             "Failed to parse config file: number of gnbs for gNB %s is %d != 1\n",
  //             GNB_CONFIG_STRING_ACTIVE_GNBS,
  //             num_gnbs);
  paramdef_t GNBParams[] = GNBPARAMS_DESC;
  /* map parameter checking array instances to parameter definition array instances */
  checkedparam_t config_check_GNBParams[] = GNBPARAMS_CHECK;
  for (int i = 0; i < sizeofArray(GNBParams); ++i)
    GNBParams[i].chkPptr = &(config_check_GNBParams[i]);
  // config_getlist(cfg, &GNBParamList, GNBParams, sizeofArray(GNBParams), NULL);

  paramdef_t MacRLC_Params[] = MACRLCPARAMS_DESC;
  paramlist_def_t MacRLC_ParamList = {CONFIG_STRING_MACRLC_LIST, NULL, 0};
  /* map parameter checking array instances to parameter definition array instances */
  checkedparam_t config_check_MacRLCParams[] = MACRLCPARAMS_CHECK;
  for (int i = 0; i < sizeofArray(MacRLC_Params); ++i)
    MacRLC_Params[i].chkPptr = &(config_check_MacRLCParams[i]);
  // config_getlist(config_get_if(), &MacRLC_ParamList, MacRLC_Params, sizeofArray(MacRLC_Params), NULL);

  nr_mac_config_t config = {0};
  config.pdsch_AntennaPorts.N1 = global_configs.defvals.pdsch_AntennaPorts_N1;
  config.pdsch_AntennaPorts.N2 = global_configs.defvals.pdsch_AntennaPorts_N2;
  config.pdsch_AntennaPorts.XP = global_configs.defvals.pdsch_AntennaPorts_XP;
  config.pusch_AntennaPorts = global_configs.defvals.pusch_AntennaPorts_idx;
  // LOG_I(GNB_APP,
  //       "pdsch_AntennaPorts N1 %d N2 %d XP %d pusch_AntennaPorts %d\n",
  //       config.pdsch_AntennaPorts.N1,
  //       config.pdsch_AntennaPorts.N2,
  //       config.pdsch_AntennaPorts.XP,
  //       config.pusch_AntennaPorts);

  paramdef_t RUParams[] = RUPARAMS_DESC;
  paramlist_def_t RUParamList = {CONFIG_STRING_RU_LIST, NULL, 0};
  // config_getlist(config_get_if(), &RUParamList, RUParams, sizeofArray(RUParams), NULL);
  int num_tx = 0;
  if (global_configs.numRUs > 0) {
    for (int i = 0; i < global_configs.numRUs; i++)
      num_tx += global_configs.RUs.nb_tx;
    // AssertFatal(num_tx >= config.pdsch_AntennaPorts.XP * config.pdsch_AntennaPorts.N1 * config.pdsch_AntennaPorts.N2,
    //             "Number of logical antenna ports (set in config file with pdsch_AntennaPorts) cannot be larger than physical antennas (nb_tx)\n");
  }
  else {
    // TODO temporary solution for 3rd party RU or nFAPI, in which case we don't have RU section present in the config file
    num_tx = config.pdsch_AntennaPorts.XP * config.pdsch_AntennaPorts.N1 * config.pdsch_AntennaPorts.N2;
    // LOG_E(GNB_APP, "RU information not present in config file. Assuming physical antenna ports equal to logical antenna ports %d\n", num_tx);
  }
  config.minRXTXTIME = global_configs.defvals.minRXTXTIME;
  LOG_I(GNB_APP, "minTXRXTIME %d\n", config.minRXTXTIME);
  config.sib1_tda = global_configs.defvals.sib1_tda;
  LOG_I(GNB_APP, "SIB1 TDA %d\n", config.sib1_tda);
  config.do_CSIRS = global_configs.defvals.do_CSIRS;
  config.do_SRS = global_configs.defvals.do_SRS;
  config.force_256qam_off = global_configs.defvals.force_256qam_dis;
  config.force_UL256qam_off = global_configs.defvals.force_UL256qam_off;
  config.use_deltaMCS = global_configs.defvals.use_deltaMCS != 0;
  config.maxMIMO_layers = global_configs.defvals.maxMIMO_layers;
  config.disable_harq = global_configs.defvals.disable_harq;
  config.num_dlharq = global_configs.defvals.num_dlharq;
  config.num_ulharq =  global_configs.defvals.num_ulharq;
  if (config.disable_harq)
    LOG_W(GNB_APP, "\"disable_harq\" is a REL17 feature and is incompatible with REL15 and REL16 UEs!\n");
  LOG_I(GNB_APP,
        "CSI-RS %d, SRS %d, 256 QAM %s, delta_MCS %s, maxMIMO_Layers %d, HARQ feedback %s, num DLHARQ:%d, num ULHARQ:%d\n",
        config.do_CSIRS,
        config.do_SRS,
        config.force_256qam_off ? "force off" : "may be on",
        config.use_deltaMCS ? "on" : "off",
        config.maxMIMO_layers,
        config.disable_harq ? "disabled" : "enabled",
        config.num_dlharq,
        config.num_ulharq);
  int tot_ant = config.pdsch_AntennaPorts.N1 * config.pdsch_AntennaPorts.N2 * config.pdsch_AntennaPorts.XP;
  AssertFatal(config.maxMIMO_layers != 0 && config.maxMIMO_layers <= tot_ant, "Invalid maxMIMO_layers %d\n", config.maxMIMO_layers);

  // paramdef_t Timers_Params[] = GNB_TIMERS_PARAMS_DESC;
  // char aprefix[MAX_OPTNAME_SIZE * 2 + 8];
  // sprintf(aprefix, "%s.[0].%s", GNB_CONFIG_STRING_GNB_LIST, GNB_CONFIG_STRING_TIMERS_CONFIG);
  // config_get(config_get_if(), Timers_Params, sizeofArray(Timers_Params), aprefix);

  config.timer_config.sr_ProhibitTimer = global_configs.defvals.sr_ProhibitTimer;
  config.timer_config.sr_TransMax = global_configs.defvals.sr_TransMax;
  config.timer_config.sr_ProhibitTimer_v1700 = global_configs.defvals.sr_ProhibitTimer_v1700;
  config.timer_config.t300 = global_configs.defvals.t300;
  config.timer_config.t301 = global_configs.defvals.t301;
  config.timer_config.t310 = global_configs.defvals.t310;
  config.timer_config.n310 = global_configs.defvals.n310;
  config.timer_config.t311 = global_configs.defvals.t311;
  config.timer_config.n311 = global_configs.defvals.n311;
  config.timer_config.t319 = global_configs.defvals.t319;
  // LOG_I(GNB_APP,
  //       "sr_ProhibitTimer %d, sr_TransMax %d, sr_ProhibitTimer_v1700 %d, "
  //       "t300 %d, t301 %d, t310 %d, n310 %d, t311 %d, n311 %d, t319 %d\n",
  //       config.timer_config.sr_ProhibitTimer,
  //       config.timer_config.sr_TransMax,
  //       config.timer_config.sr_ProhibitTimer_v1700,
  //       config.timer_config.t300,
  //       config.timer_config.t301,
  //       config.timer_config.t310,
  //       config.timer_config.n310,
  //       config.timer_config.t311,
  //       config.timer_config.n311,
  //       config.timer_config.t319);

  if (global_configs.defvals.GNBwtsidx) {
    int n = global_configs.defvals.GNB_BEAMWEIGH_IDX;
    AssertFatal(n % num_tx == 0, "Error! Number of beam input needs to be multiple of TX antennas\n");
    // each beam is described by a set of weights (one for each antenna)
    // on the other hand in case of analog beamforming an index to the RU beam identifier is provided
    config.nb_bfw[0] = num_tx;  // number of tx antennas
    config.nb_bfw[1] = n / num_tx; // number of beams
    config.bw_list = malloc16_clear(n * sizeof(*config.bw_list));
    for (int b = 0; b < n; b++) {
      config.bw_list[b] = GNBParamList.paramarray[0][GNB_BEAMWEIGHTS_IDX].iptr[b];
    }
  }
  // printf("just before calling get_scc_config\n");
  NR_ServingCellConfigCommon_t *scc = get_scc_config(cfg, config.minRXTXTIME);
  //xer_fprint(stdout, &asn_DEF_NR_ServingCellConfigCommon, scc);
  NR_ServingCellConfig_t *scd = get_scd_config(cfg);

  if (global_vnf.numMACRLCs > 0) {
    ngran_node_t node_type = get_node_type();
    mac_top_init_gNB(node_type, scc, scd, &config);
    // printf("just after calling mac_top_init_gNB\n");
    RC.nb_nr_mac_CC = (int *)malloc(RC.nb_nr_macrlc_inst * sizeof(int));

    for (j = 0; j < RC.nb_nr_macrlc_inst; j++) {
      RC.nb_nr_mac_CC[j] = global_vnf.macrlcs->num_cc;
      RC.nrmac[j]->pusch_target_snrx10 = global_configs.defvals.PUSCH_target_snrx10;
      RC.nrmac[j]->pucch_target_snrx10 = global_configs.defvals.PUCCH_target_snrx10;
      RC.nrmac[j]->ul_prbblack_SNR_threshold = global_configs.defvals.UL_PRBblack_SNR_thres;
      RC.nrmac[j]->pucch_failure_thres = global_configs.defvals.PUCCH_fail_thres;
      RC.nrmac[j]->pusch_failure_thres = global_vnf.macrlcs->pusch_failure_thres;

      // LOG_I(NR_MAC,
      //       "PUSCH Target %d, PUCCH Target %d, PUCCH Failure %d, PUSCH Failure %d\n",
      //       RC.nrmac[j]->pusch_target_snrx10,
      //       RC.nrmac[j]->pucch_target_snrx10,
      //       RC.nrmac[j]->pucch_failure_thres,
      //       RC.nrmac[j]->pusch_failure_thres);
      SM_Logs(LOG_INFO, _MAC_, "PUSCH LIMITS: GOAL: %d, Error: %d",RC.nrmac[j]->pusch_target_snrx10,  RC.nrmac[j]->pusch_failure_thres);
      SM_Logs(LOG_INFO, _MAC_, "PUCCH LIMITS: GOAL: %d, Error: %d",RC.nrmac[j]->pucch_target_snrx10, RC.nrmac[j]->pucch_failure_thres);

      // printf("global_vnf.macrlcs->tr_n_preference : %s\n",global_vnf.macrlcs->tr_n_preference);

      if (strcmp(global_vnf.macrlcs->tr_n_preference, "local_RRC") == 0) {
        // check number of instances is same as RRC/PDCP

      } else if (strcmp(global_vnf.macrlcs->tr_n_preference, "f1") == 0
                 || strcmp(global_vnf.macrlcs->tr_n_preference, "cudu") == 0) {
        char **f1caddr = global_vnf.macrlcs->local_s_address;
        RC.nrmac[j]->eth_params_n.my_addr = strdup(global_vnf.macrlcs->local_s_address);
        RC.nrmac[j]->f1u_addr = strdup(global_vnf.macrlcs->local_s_address);
        RC.nrmac[j]->eth_params_n.remote_addr = strdup(global_vnf.macrlcs->remote_s_address);
        RC.nrmac[j]->eth_params_n.my_portc = global_vnf.macrlcs->local_s_portc;
        RC.nrmac[j]->eth_params_n.remote_portc = global_vnf.macrlcs->remote_s_portc;
        RC.nrmac[j]->eth_params_n.my_portd = global_vnf.macrlcs->local_s_portd;
        RC.nrmac[j]->eth_params_n.remote_portd = global_vnf.macrlcs->remote_s_portd;
        RC.nrmac[j]->eth_params_n.transp_preference = ETH_UDP_MODE;


      } else { // other midhaul
        AssertFatal(1 == 0, "MACRLC %d: %s unknown northbound midhaul\n", j, global_vnf.macrlcs->tr_n_preference);
      }

      if (strcmp(global_vnf.macrlcs->tr_s_preference, "local_L1") == 0) {
      } else if (strcmp(global_vnf.macrlcs->tr_s_preference, "nfapi") == 0) {
        RC.nrmac[j]->eth_params_s.my_addr = strdup(*(MacRLC_ParamList.paramarray[j][MACRLC_LOCAL_S_ADDRESS_IDX].strptr));
        RC.nrmac[j]->eth_params_s.remote_addr = strdup(*(MacRLC_ParamList.paramarray[j][MACRLC_REMOTE_S_ADDRESS_IDX].strptr));
        RC.nrmac[j]->eth_params_s.my_portc = *(MacRLC_ParamList.paramarray[j][MACRLC_LOCAL_S_PORTC_IDX].iptr);
        RC.nrmac[j]->eth_params_s.remote_portc = *(MacRLC_ParamList.paramarray[j][MACRLC_REMOTE_S_PORTC_IDX].iptr);
        RC.nrmac[j]->eth_params_s.my_portd = *(MacRLC_ParamList.paramarray[j][MACRLC_LOCAL_S_PORTD_IDX].iptr);
        RC.nrmac[j]->eth_params_s.remote_portd = *(MacRLC_ParamList.paramarray[j][MACRLC_REMOTE_S_PORTD_IDX].iptr);
        RC.nrmac[j]->eth_params_s.transp_preference = ETH_UDP_MODE;

        configure_nr_nfapi_vnf(
            RC.nrmac[j]->eth_params_s.my_addr, RC.nrmac[j]->eth_params_s.my_portc, RC.nrmac[j]->eth_params_s.remote_addr, RC.nrmac[j]->eth_params_s.remote_portd, RC.nrmac[j]->eth_params_s.my_portd);
      } else if(strcmp(global_vnf.macrlcs->tr_s_preference, "aerial") == 0){
#ifdef ENABLE_AERIAL
        RC.nrmac[j]->nvipc_params_s.nvipc_shm_prefix =
            strdup(*(MacRLC_ParamList.paramarray[j][MACRLC_TRANSPORT_S_SHM_PREFIX].strptr));
        RC.nrmac[j]->nvipc_params_s.nvipc_poll_core = *(MacRLC_ParamList.paramarray[j][MACRLC_TRANSPORT_S_POLL_CORE].i8ptr);
        // LOG_I(GNB_APP, "Configuring VNF for Aerial connection with prefix %s\n", RC.nrmac[j]->eth_params_s.local_if_name);
        aerial_configure_nr_fapi_vnf();
#endif
      } else { // other midhaul
        AssertFatal(1 == 0, "MACRLC %d: %s unknown southbound midhaul\n", j, global_vnf.macrlcs->tr_s_preference);
      }
      // printf("\njust before RC.nrmac[j]->ulsch_max_frame_inactivity\n");
      RC.nrmac[j]->ulsch_max_frame_inactivity = global_configs.defvals.ULSCH_max_frame;
      NR_bler_options_t *dl_bler_options = &RC.nrmac[j]->dl_bler;
      dl_bler_options->upper = global_configs.defvals.dloption_upper;
      dl_bler_options->lower = global_configs.defvals.dloption_lower;
      dl_bler_options->max_mcs = global_configs.defvals.dloption_maxmcs;
      // printf("uls_ch_max_frame_inactivity: %u\n", *(MacRLC_ParamList.paramarray[j][MACRLC_ULSCH_MAX_FRAME_INACTIVITY].uptr));
      // printf("dl_bler_options->upper: %f\n", *(MacRLC_ParamList.paramarray[j][MACRLC_DL_BLER_TARGET_UPPER_IDX].dblptr));
      // printf("dl_bler_options->lower: %f\n", *(MacRLC_ParamList.paramarray[j][MACRLC_DL_BLER_TARGET_LOWER_IDX].dblptr));
      // printf("dl_bler_options->max_mcs: %u\n", *(MacRLC_ParamList.paramarray[j][MACRLC_DL_MAX_MCS_IDX].u8ptr));
      // printf("dl_bler_options->harq_round_max: %u\n", dl_bler_options->harq_round_max);
      // printf("*(MacRLC_ParamList.paramarray[j][MACRLC_DL_HARQ_ROUND_MAX_IDX].u8ptr) : %u\n",*(MacRLC_ParamList.paramarray[j][MACRLC_DL_HARQ_ROUND_MAX_IDX].u8ptr));

      if (config.disable_harq)
        dl_bler_options->harq_round_max = 1;
      else
        dl_bler_options->harq_round_max = global_configs.defvals.dloption_harqmaxround;
      NR_bler_options_t *ul_bler_options = &RC.nrmac[j]->ul_bler;
      ul_bler_options->upper = global_configs.defvals.uloption_upper;
      ul_bler_options->lower = global_configs.defvals.uloption_lower;
      ul_bler_options->max_mcs = global_configs.defvals.uloption_maxmcs;
      // printf("ul_bler_options->upper: %f\n", *(MacRLC_ParamList.paramarray[j][MACRLC_UL_BLER_TARGET_UPPER_IDX].dblptr));
      // printf("ul_bler_options->lower: %f\n", *(MacRLC_ParamList.paramarray[j][MACRLC_UL_BLER_TARGET_LOWER_IDX].dblptr));
      // printf("ul_bler_options->max_mcs: %u\n", *(MacRLC_ParamList.paramarray[j][MACRLC_UL_MAX_MCS_IDX].u8ptr));
      // printf("ul_bler_options->harq_round_max: %u\n", ul_bler_options->harq_round_max);
      // printf("*(MacRLC_ParamList.paramarray[j][MACRLC_UL_HARQ_ROUND_MAX_IDX].u8ptr) : %u\n",*(MacRLC_ParamList.paramarray[j][MACRLC_UL_HARQ_ROUND_MAX_IDX].u8ptr));

      if (config.disable_harq)
        ul_bler_options->harq_round_max = 1;
      else
        ul_bler_options->harq_round_max = global_configs.defvals.uloption_harqmaxround;
      RC.nrmac[j]->min_grant_prb = global_configs.defvals.min_grant_PRB;
      RC.nrmac[j]->min_grant_mcs = global_configs.defvals.min_grant_MCS;
      RC.nrmac[j]->identity_pm = global_configs.defvals.identity_PM;

      // printf("min_grant_prb line 1728\n");
      
            // PRB Blacklist
      uint16_t prbbl[MAX_BWP_SIZE] = {0};
      int num_ulprbbl = get_prb_blacklist(j, prbbl);
      if (num_ulprbbl != -1) {
        // LOG_I(NR_PHY, "Copying %d blacklisted PRB to L1 context\n", num_ulprbbl);
        memcpy(RC.nrmac[j]->ulprbbl, prbbl, MAX_BWP_SIZE * sizeof(prbbl[0]));
      }
      bool ab = global_vnf.ANALOG_BEAMFORMG_IDX;
      if (ab) {
        NR_beam_info_t *beam_info = &RC.nrmac[j]->beam_info;
        int beams_per_period = *MacRLC_ParamList.paramarray[j][MACRLC_ANALOG_BEAMS_PERIOD_IDX].u8ptr;
        beam_info->beam_allocation = malloc16(beams_per_period * sizeof(int *));
        beam_info->beam_duration = *MacRLC_ParamList.paramarray[j][MACRLC_ANALOG_BEAM_DURATION_IDX].u8ptr;
        beam_info->beams_per_period = beams_per_period;
        beam_info->beam_allocation_size = -1; // to be initialized once we have information on frame configuration
      }
      // triggers also PHY initialization in case we have L1 via FAPI
      // printf("just before calling nr_mac_config_scc\n");
      nr_mac_config_scc(RC.nrmac[j], scc, &config);
      // printf("just after calling nr_mac_config_scc\n");
    } //  for (j=0;j<RC.nb_nr_macrlc_inst;j++)

    uint64_t gnb_du_id = 0;
    uint32_t gnb_id = 0;
    char *name = NULL;
    f1ap_served_cell_info_t info;
    read_du_cell_info(cfg, NODE_IS_DU(node_type), &gnb_id, &gnb_du_id, &name, &info, 1);

    if (IS_SA_MODE(get_softmodem_params())) {
      nr_mac_configure_sib1(RC.nrmac[0], &info.plmn, info.nr_cellid, *info.tac);
      if (scc->ext2 && scc->ext2->ntn_Config_r17)
        nr_mac_configure_sib19(RC.nrmac[0]);
    }
    
    // read F1 Setup information from config and generated MIB/SIB1
    // and store it at MAC for sending later
    NR_BCCH_BCH_Message_t *mib = RC.nrmac[0]->common_channels[0].mib;
    const NR_BCCH_DL_SCH_Message_t *sib1 = RC.nrmac[0]->common_channels[0].sib1;
    f1ap_setup_req_t *req = RC_read_F1Setup(gnb_du_id, name, &info, scc, mib, sib1);
    // AssertFatal(req != NULL, "could not read F1 Setup information\n");
    RC.nrmac[0]->f1_config.setup_req = req;
    RC.nrmac[0]->f1_config.gnb_id = gnb_id;

    free(name); /* read_du_cell_info() allocated memory */

  } else { // MacRLC_ParamList.numelt > 0
    // LOG_E(PHY, "No %s configuration found\n", CONFIG_STRING_MACRLC_LIST);
    // AssertFatal (0,"No " CONFIG_STRING_MACRLC_LIST " configuration found");
  }
  // printf("returning from RCconfig_nr_macrlc\n");
}

void config_security(gNB_RRC_INST *rrc)
{
  paramdef_t sec_params[] = SECURITY_GLOBALPARAMS_DESC;
  //int ret = config_get(config_get_if(), sec_params, sizeofArray(sec_params), CONFIG_STRING_SECURITY);
  int ret = 0;
  int i;

  if (ret < 0) {
    // LOG_W(RRC, "configuration file does not contain a \"security\" section, applying default parameters (nia2 nea0, integrity disabled for DRBs)\n");
    rrc->security.ciphering_algorithms[0]    = 0;  /* nea0 = no ciphering */
    rrc->security.ciphering_algorithms_count = 1;
    rrc->security.integrity_algorithms[0]    = 2;  /* nia2 */
    rrc->security.integrity_algorithms[1]    = 0;  /* nia0 = no integrity, as a fallback (but nia2 should be supported by all UEs) */
    rrc->security.integrity_algorithms_count = 2;
    rrc->security.do_drb_ciphering           = 1;  /* even if nea0 let's activate so that we don't generate cipheringDisabled in pdcp_Config */
    rrc->security.do_drb_integrity           = 0;
    return;
  }

  if (GlobC_ARIAConfs.numSECURITY > 4) {
    // LOG_E(RRC, "too much ciphering algorithms in section \"security\" of the configuration file, maximum is 4\n");
    SM_Logs(LOG_ERROR,_RRC_, "Exceeded the maximum limit of ciphering algorithms\n");
    exit(1);
  }
  if (GlobC_ARIAConfs.numINTEGalgo > 4) {
    // LOG_E(RRC, "too much integrity algorithms in section \"security\" of the configuration file, maximum is 4\n");
    SM_Logs(LOG_ERROR,_RRC_,"Exceeded the maximum limit of integrity algorithms\n");
    exit(1);
  }
  /* get ciphering algorithms */
  rrc->security.ciphering_algorithms_count = 0;
  for (i = 0; i < GlobC_ARIAConfs.numSECURITY; i++) {
    if (!strcmp(GlobC_ARIAConfs.y_security.CipherModes[i], "nea0")) {
      rrc->security.ciphering_algorithms[rrc->security.ciphering_algorithms_count] = 0;
      rrc->security.ciphering_algorithms_count++;
      continue;
    }
    if (!strcmp(GlobC_ARIAConfs.y_security.CipherModes[i], "nea1")) {
      rrc->security.ciphering_algorithms[rrc->security.ciphering_algorithms_count] = 1;
      rrc->security.ciphering_algorithms_count++;
      continue;
    }
    if (!strcmp(GlobC_ARIAConfs.y_security.CipherModes[i], "nea2")) {
      rrc->security.ciphering_algorithms[rrc->security.ciphering_algorithms_count] = 2;
      rrc->security.ciphering_algorithms_count++;
      continue;
    }
    if (!strcmp(GlobC_ARIAConfs.y_security.CipherModes[i], "nea3")) {
      rrc->security.ciphering_algorithms[rrc->security.ciphering_algorithms_count] = 3;
      rrc->security.ciphering_algorithms_count++;
      continue;
    }
    LOG_E(RRC, "unknown ciphering algorithm \"%s\" in section \"security\" of the configuration file\n",
          GlobC_ARIAConfs.y_security.CipherModes[i]);
    SM_Logs(LOG_ERROR,_RRC_, "Unknown algorithm\n");
    exit(1);
  }

  /* get integrity algorithms */
  rrc->security.integrity_algorithms_count = 0;
  for (i = 0; i < GlobC_ARIAConfs.numINTEGalgo; i++) {
    if (!strcmp(GlobC_ARIAConfs.y_security.AuthModes[i] , "nia0")) {
      rrc->security.integrity_algorithms[rrc->security.integrity_algorithms_count] = 0;
      rrc->security.integrity_algorithms_count++;
      continue;
    }
    if (!strcmp(GlobC_ARIAConfs.y_security.AuthModes[i], "nia1")) {
      rrc->security.integrity_algorithms[rrc->security.integrity_algorithms_count] = 1;
      rrc->security.integrity_algorithms_count++;
      continue;
    }
    if (!strcmp(GlobC_ARIAConfs.y_security.AuthModes[i], "nia2")) {
      rrc->security.integrity_algorithms[rrc->security.integrity_algorithms_count] = 2;
      rrc->security.integrity_algorithms_count++;
      continue;
    }
    if (!strcmp(GlobC_ARIAConfs.y_security.AuthModes[i], "nia3")) {
      rrc->security.integrity_algorithms[rrc->security.integrity_algorithms_count] = 3;
      rrc->security.integrity_algorithms_count++;
      continue;
    }
    LOG_E(RRC, "unknown integrity algorithm \"%s\" in section \"security\" of the configuration file\n",
          GlobC_ARIAConfs.y_security.AuthModes[i]);
    exit(1);
  }

  if (rrc->security.ciphering_algorithms_count == 0) {
    // LOG_W(RRC, "no preferred ciphering algorithm set in configuration file, applying default parameters (no security)\n");
    rrc->security.ciphering_algorithms[0]    = 0;  /* nea0 = no ciphering */
    rrc->security.ciphering_algorithms_count = 1;
  }

  if (rrc->security.integrity_algorithms_count == 0) {
    // LOG_W(RRC, "no preferred integrity algorithm set in configuration file, applying default parameters (nia2)\n");
    rrc->security.integrity_algorithms[0]    = 2;  /* nia2 */
    rrc->security.integrity_algorithms[1]    = 0;  /* nia0 = no integrity */
    rrc->security.integrity_algorithms_count = 2;
  }

  if (!strcmp(GlobC_ARIAConfs.y_security.BchannelEncryp, "yes")) {
    rrc->security.do_drb_ciphering = 1;
  } else if (!strcmp(GlobC_ARIAConfs.y_security.BchannelEncryp, "no")) {
    rrc->security.do_drb_ciphering = 0;
  } else {
    LOG_E(RRC, "in configuration file, bad drb_ciphering value '%s', only 'yes' and 'no' allowed\n",
          GlobC_ARIAConfs.y_security.BchannelEncryp);
    exit(1);
  }

  if (!strcmp(GlobC_ARIAConfs.y_security.Bchannelauthentication, "yes")) {
    rrc->security.do_drb_integrity = 1;
  } else if (!strcmp(GlobC_ARIAConfs.y_security.Bchannelauthentication, "no")) {
    rrc->security.do_drb_integrity = 0;
  } else {
    LOG_E(RRC, "in configuration file, bad drb_integrity value '%s', only 'yes' and 'no' allowed\n",
          GlobC_ARIAConfs.y_security.Bchannelauthentication);
    exit(1);
  }
}

static int sort_neighbour_cell_config_by_cell_id(const void *a, const void *b)
{
  const neighbour_cell_configuration_t *config_a = (const neighbour_cell_configuration_t *)a;
  const neighbour_cell_configuration_t *config_b = (const neighbour_cell_configuration_t *)b;

  if (config_a->nr_cell_id < config_b->nr_cell_id) {
    return -1;
  } else if (config_a->nr_cell_id > config_b->nr_cell_id) {
    return 1;
  }

  return 0;
}

static void fill_neighbour_cell_configuration(uint8_t gnb_idx, gNB_RRC_INST *rrc)
{
  char gnbpath[MAX_OPTNAME_SIZE + 8];
  sprintf(gnbpath, "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, gnb_idx);

  paramdef_t neighbour_list_params[] = GNB_NEIGHBOUR_LIST_PARAM_LIST;
  paramlist_def_t neighbour_list_param_list = {GNB_CONFIG_STRING_NEIGHBOUR_LIST, NULL, 0};

  config_getlist(config_get_if(), &neighbour_list_param_list, neighbour_list_params, sizeofArray(neighbour_list_params), gnbpath);
  if (neighbour_list_param_list.numelt < 1)
    return;

  rrc->neighbour_cell_configuration = malloc(sizeof(seq_arr_t));
  seq_arr_init(rrc->neighbour_cell_configuration, sizeof(neighbour_cell_configuration_t));

  for (int elm = 0; elm < neighbour_list_param_list.numelt; ++elm) {
    neighbour_cell_configuration_t *cell = calloc(1, sizeof(neighbour_cell_configuration_t));
    // AssertFatal(cell != NULL, "out of memory\n");
    cell->nr_cell_id = (uint64_t)*neighbour_list_param_list.paramarray[elm][0].u64ptr;

    char neighbourpath[MAX_OPTNAME_SIZE + 8];
    sprintf(neighbourpath, "%s.[%i].%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, gnb_idx, GNB_CONFIG_STRING_NEIGHBOUR_LIST, elm);
    paramdef_t NeighbourCellParams[] = GNBNEIGHBOURCELLPARAMS_DESC;
    paramlist_def_t NeighbourCellParamList = {GNB_CONFIG_STRING_NEIGHBOUR_CELL_LIST, NULL, 0};
    config_getlist(config_get_if(), &NeighbourCellParamList, NeighbourCellParams, sizeofArray(NeighbourCellParams), neighbourpath);
    // LOG_D(GNB_APP, "HO LOG: For the Cell: %ld Neighbour Cell ELM NUM: %d\n", cell->nr_cell_id, NeighbourCellParamList.numelt);
    if (NeighbourCellParamList.numelt < 1)
      continue;

    cell->neighbour_cells = malloc(sizeof(seq_arr_t));
    // AssertFatal(cell->neighbour_cells != NULL, "Memory exhausted!!!");
    seq_arr_init(cell->neighbour_cells, sizeof(nr_neighbour_gnb_configuration_t));
    for (int l = 0; l < NeighbourCellParamList.numelt; ++l) {
      nr_neighbour_gnb_configuration_t *neighbourCell = calloc(1, sizeof(nr_neighbour_gnb_configuration_t));
      // AssertFatal(neighbourCell != NULL, "out of memory\n");
      neighbourCell->gNB_ID = *(NeighbourCellParamList.paramarray[l][GNB_CONFIG_N_CELL_GNB_ID_IDX].uptr);
      neighbourCell->nrcell_id = (uint64_t) * (NeighbourCellParamList.paramarray[l][GNB_CONFIG_N_CELL_NR_CELLID_IDX].u64ptr);
      neighbourCell->physicalCellId = *NeighbourCellParamList.paramarray[l][GNB_CONFIG_N_CELL_PHYSICAL_ID_IDX].uptr;
      neighbourCell->subcarrierSpacing = *NeighbourCellParamList.paramarray[l][GNB_CONFIG_N_CELL_SCS_IDX].uptr;
      neighbourCell->absoluteFrequencySSB = *NeighbourCellParamList.paramarray[l][GNB_CONFIG_N_CELL_ABS_FREQ_SSB_IDX].i64ptr;
      neighbourCell->tac = *NeighbourCellParamList.paramarray[l][GNB_CONFIG_N_CELL_TAC_IDX].uptr;

      char neighbour_plmn_path[CONFIG_MAXOPTLENGTH];
      sprintf(neighbour_plmn_path,
              "%s.%s.[%i].%s",
              neighbourpath,
              GNB_CONFIG_STRING_NEIGHBOUR_CELL_LIST,
              l,
              GNB_CONFIG_STRING_NEIGHBOUR_PLMN);

      paramdef_t NeighbourPlmn[] = GNBPLMNPARAMS_DESC;
      config_get(config_get_if(), NeighbourPlmn, sizeofArray(NeighbourPlmn), neighbour_plmn_path);

      neighbourCell->plmn.mcc = *NeighbourPlmn[GNB_MOBILE_COUNTRY_CODE_IDX].uptr;
      neighbourCell->plmn.mnc = *NeighbourPlmn[GNB_MOBILE_NETWORK_CODE_IDX].uptr;
      neighbourCell->plmn.mnc_digit_length = *NeighbourPlmn[GNB_MNC_DIGIT_LENGTH].uptr;
      seq_arr_push_back(cell->neighbour_cells, neighbourCell, sizeof(nr_neighbour_gnb_configuration_t));
    }

    seq_arr_push_back(rrc->neighbour_cell_configuration, cell, sizeof(neighbour_cell_configuration_t));
  }
  void *base = seq_arr_front(rrc->neighbour_cell_configuration);
  size_t nmemb = seq_arr_size(rrc->neighbour_cell_configuration);
  size_t element_size = sizeof(neighbour_cell_configuration_t);

  qsort(base, nmemb, element_size, sort_neighbour_cell_config_by_cell_id);
}

static void fill_measurement_configuration(uint8_t gnb_idx, gNB_RRC_INST *rrc)
{
  char measurement_path[MAX_OPTNAME_SIZE + 8];
  sprintf(measurement_path, "%s.[%i].%s", GNB_CONFIG_STRING_GNB_LIST, gnb_idx, GNB_CONFIG_STRING_MEASUREMENT_CONFIGURATION);

  nr_measurement_configuration_t *measurementConfig = &rrc->measurementConfiguration;
  // Periodical Event Configuration
  char periodic_event_path[MAX_OPTNAME_SIZE + 8];
  sprintf(periodic_event_path,
          "%s.[%i].%s.%s",
          GNB_CONFIG_STRING_GNB_LIST,
          gnb_idx,
          GNB_CONFIG_STRING_MEASUREMENT_CONFIGURATION,
          MEASUREMENT_EVENTS_PERIODICAL);
  paramdef_t Periodical_EventParams[] = MEASUREMENT_PERIODICAL_GLOBALPARAMS_DESC;
  config_get(config_get_if(), Periodical_EventParams, sizeofArray(Periodical_EventParams), periodic_event_path);
  if (*Periodical_EventParams[MEASUREMENT_EVENTS_ENABLE_IDX].i64ptr) {
    nr_per_event_t *periodic_event = (nr_per_event_t *)calloc(1, sizeof(nr_per_event_t));
    periodic_event->includeBeamMeasurements = *Periodical_EventParams[MEASUREMENT_EVENTS_INCLUDE_BEAM_MEAS_IDX].i64ptr;
    periodic_event->maxReportCells = *Periodical_EventParams[MEASUREMENT_EVENTS_MAX_RS_INDEX_TO_REPORT].i64ptr;

    measurementConfig->per_event = periodic_event;
  }

  // A2 Event Configuration
  char a2_path[MAX_OPTNAME_SIZE + 8];
  sprintf(a2_path,
          "%s.[%i].%s.%s",
          GNB_CONFIG_STRING_GNB_LIST,
          gnb_idx,
          GNB_CONFIG_STRING_MEASUREMENT_CONFIGURATION,
          MEASUREMENT_EVENTS_A2);
  paramdef_t A2_EventParams[] = MEASUREMENT_A2_GLOBALPARAMS_DESC;
  config_get(config_get_if(), A2_EventParams, sizeofArray(A2_EventParams), a2_path);
  if (*A2_EventParams[MEASUREMENT_EVENTS_ENABLE_IDX].i64ptr) {
    nr_a2_event_t *a2_event = (nr_a2_event_t *)calloc(1, sizeof(nr_a2_event_t));
    a2_event->threshold_RSRP = *A2_EventParams[MEASUREMENT_EVENTS_A2_THRESHOLD_IDX].i64ptr;
    a2_event->timeToTrigger = *A2_EventParams[MEASUREMENT_EVENTS_TIMETOTRIGGER_IDX].i64ptr;

    measurementConfig->a2_event = a2_event;
  }
  // A3 Event Configuration
  paramlist_def_t A3_EventList = {MEASUREMENT_EVENTS_A3, NULL, 0};
  paramdef_t A3_EventParams[] = MEASUREMENT_A3_GLOBALPARAMS_DESC;
  config_getlist(config_get_if(), &A3_EventList, A3_EventParams, sizeofArray(A3_EventParams), measurement_path);
  // LOG_D(GNB_APP, "HO LOG: A3 Configuration Exists: %d\n", A3_EventList.numelt);

  if (A3_EventList.numelt < 1)
    return;

  measurementConfig->a3_event_list = malloc(sizeof(seq_arr_t));
  seq_arr_init(measurementConfig->a3_event_list, sizeof(nr_a3_event_t));
  for (int i = 0; i < A3_EventList.numelt; i++) {
    nr_a3_event_t *a3_event = (nr_a3_event_t *)calloc(1, sizeof(nr_a3_event_t));
    // AssertFatal(a3_event != NULL, "out of memory\n");
    a3_event->pci = *A3_EventList.paramarray[i][MEASUREMENT_EVENTS_PCI_ID_IDX].i64ptr;
    // AssertFatal(a3_event->pci >= -1 && a3_event->pci < 1024,
    //             "entry %s.%s must be -1<=PCI<1024, but is %d\n",
    //             measurement_path,
    //             MEASUREMENT_EVENTS_PCI_ID,
    //             a3_event->pci);
    a3_event->timeToTrigger = *A3_EventList.paramarray[i][MEASUREMENT_EVENTS_TIMETOTRIGGER_IDX].i64ptr;
    a3_event->a3_offset = *A3_EventList.paramarray[i][MEASUREMENT_EVENTS_OFFSET_IDX].i64ptr;
    a3_event->hysteresis = *A3_EventList.paramarray[i][MEASUREMENT_EVENTS_HYSTERESIS_IDX].i64ptr;

    if (a3_event->pci == -1)
      measurementConfig->is_default_a3_configuration_exists = true;

    seq_arr_push_back(measurementConfig->a3_event_list, a3_event, sizeof(nr_a3_event_t));
  }
}

/**
 * @brief Allocates and initializes RRC instances
 *        Currently assuming 1 instance
 */
gNB_RRC_INST *RCconfig_NRRRC()
{
  // Allocate memory for 1 RRC instance
  gNB_RRC_INST *rrc = calloc(1, sizeof(*rrc));

  int num_gnbs = 0;
  char aprefix[MAX_OPTNAME_SIZE*2 + 8];
  int32_t gnb_id = 0;
  int k = 0;
  int i = 0;

  paramdef_t GNBSParams[] = GNBSPARAMS_DESC;
  ////////// Identification parameters
  paramdef_t GNBParams[]  = GNBPARAMS_DESC;
  paramlist_def_t GNBParamList = {GNB_CONFIG_STRING_GNB_LIST,NULL,0};

  ////////// Physical parameters

  /* get global parameters, defined outside any section in the config file */

  // config_get(config_get_if(), GNBSParams, sizeofArray(GNBSParams), NULL);
  num_gnbs = global_vnf.numactiveGNBs;
  AssertFatal (i < num_gnbs,"Failed to parse config file no %dth element in %s \n", i, GNB_CONFIG_STRING_ACTIVE_GNBS);
  AssertFatal(num_gnbs == 1, "required section \"gNBs\" not in config!\n");

  if (num_gnbs > 0) {

    // Output a list of all gNBs. ////////// Identification parameters
    //config_getlist(config_get_if(), &GNBParamList, GNBParams, sizeofArray(GNBParams), NULL);
    if (global_vnf.gNBs->gNB_ID == NULL) {
    // Calculate a default gNB ID
      if (IS_SA_MODE(get_softmodem_params())) {
        uint32_t hash;
        hash = ngap_generate_gNB_id ();
        gnb_id = i + (hash & 0xFFFFFF8);
      } else {
        gnb_id = i;
      }
    } else {
      gnb_id = global_vnf.gNBs->gNB_ID;
    }
    // printf("GNB ID line 2110: %u\n", *(GNBParamList.paramarray[i][GNB_GNB_ID_IDX].uptr));
    // printf("Transport Preference: %s\n", *(GNBParamList.paramarray[i][GNB_TRANSPORT_S_PREFERENCE_IDX].strptr));


    sprintf(aprefix, "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0);

    SM_Logs(LOG_INFO, _RRC_, "RRC Instance 0: Establishing Southbound Transport Layer with local MAC address binding.");
    //LOG_D(GNB_APP, "NRRRC %d: Southbound Transport %s\n", i, *(GNBParamList.paramarray[i][GNB_TRANSPORT_S_PREFERENCE_IDX].strptr));

    rrc->node_type = get_node_type();
    rrc->node_id        = gnb_id;
    if (NODE_IS_CU(rrc->node_type)) {
      paramdef_t SCTPParams[]  = GNBSCTPPARAMS_DESC;
      char aprefix[MAX_OPTNAME_SIZE*2 + 8];
      sprintf(aprefix,"%s.[%d].%s", GNB_CONFIG_STRING_GNB_LIST, i, GNB_CONFIG_STRING_SCTP_CONFIG);
      //config_get(config_get_if(), SCTPParams, sizeofArray(SCTPParams), aprefix);
      // LOG_I(GNB_APP,"F1AP: gNB_CU_id[%d] %d\n", k, rrc->node_id);
      rrc->node_name = strdup(GlobC_ARIAConfs.y_gNBs->EntityLabel);
      // LOG_I(GNB_APP,"F1AP: gNB_CU_name[%d] %s\n", k, rrc->node_name);
      rrc->eth_params_s.my_addr                  = strdup(GlobC_ARIAConfs.y_gNBs->HostServIP);
      rrc->eth_params_s.remote_addr              = strdup(GlobC_ARIAConfs.y_gNBs->ExtPointIP);
      rrc->eth_params_s.my_portc                 = GlobC_ARIAConfs.y_gNBs->HostSCrlPort;
      rrc->eth_params_s.remote_portc             = GlobC_ARIAConfs.y_gNBs->ExtScrlPort;
      rrc->eth_params_s.my_portd                 = GlobC_ARIAConfs.y_gNBs->HostSdtPort;
      rrc->eth_params_s.remote_portd             = GlobC_ARIAConfs.y_gNBs->ExtSdtPort;
      rrc->eth_params_s.transp_preference        = ETH_UDP_MODE;
    }

   

    rrc->nr_cellid        =  GlobC_ARIAConfs.y_gNBs->SegmentID;
    // printf("nr_cellid is: %llu\n",rrc->nr_cellid);
    if (strcmp(global_configs.defvals.gnb_tr_s_pref, "local_mac") == 0) {
      
    } else if (strcmp(global_configs.defvals.gnb_tr_s_pref, "cudu") == 0) {
      // rrc->eth_params_s.my_addr                  = strdup(*(GNBParamList.paramarray[i][GNB_LOCAL_S_ADDRESS_IDX].strptr));
      // rrc->eth_params_s.remote_addr              = strdup(*(GNBParamList.paramarray[i][GNB_REMOTE_S_ADDRESS_IDX].strptr));
      // rrc->eth_params_s.my_portc                 = *(GNBParamList.paramarray[i][GNB_LOCAL_S_PORTC_IDX].uptr);
      // rrc->eth_params_s.remote_portc             = *(GNBParamList.paramarray[i][GNB_REMOTE_S_PORTC_IDX].uptr);
      // rrc->eth_params_s.my_portd                 = *(GNBParamList.paramarray[i][GNB_LOCAL_S_PORTD_IDX].uptr);
      // rrc->eth_params_s.remote_portd             = *(GNBParamList.paramarray[i][GNB_REMOTE_S_PORTD_IDX].uptr);
      // rrc->eth_params_s.transp_preference        = ETH_UDP_MODE;
      ;
    } else { // other midhaul
    }       
    // printf("i am after: \n");
    // search if in active list
    
    gNB_RrcConfigurationReq nrrrc_config = {0};
    for (k=0; k <num_gnbs ; k++) {
      if (strcmp(GlobC_ARIAConfs.FunctionalNodes, GlobC_ARIAConfs.y_gNBs->EntityLabel) == 0 &&
           strcmp(GlobC_ARIAConfs.FunctionalNodes, GlobC_ARIAConfs.y_gNBs->EntityLabel) == 0) {
        
        // printf("i am after: 1\n");
        char gnbpath[MAX_OPTNAME_SIZE + 8];
        sprintf(gnbpath,"%s.[%i]",GNB_CONFIG_STRING_GNB_LIST,k);

        // fill_neighbour_cell_configuration(k, rrc);

        // fill_measurement_configuration(k, rrc);

        paramdef_t PLMNParams[] = GNBPLMNPARAMS_DESC;

        paramlist_def_t PLMNParamList = {GNB_CONFIG_STRING_PLMN_LIST, NULL, 0};
        /* map parameter checking array instances to parameter definition array instances */
        checkedparam_t config_check_PLMNParams [] = PLMNPARAMS_CHECK;

        for (int I = 0; I < sizeofArray(PLMNParams); ++I)
          PLMNParams[I].chkPptr = &(config_check_PLMNParams[I]);

        nrrrc_config.tac               = GlobC_ARIAConfs.y_gNBs->TAC;
        // AssertFatal(!GNBParamList.paramarray[i][GNB_MOBILE_COUNTRY_CODE_IDX_OLD].strptr
        //             && !GNBParamList.paramarray[i][GNB_MOBILE_NETWORK_CODE_IDX_OLD].strptr,
        //             "It seems that you use an old configuration file. Please change the existing\n"
        //             "    tracking_area_code  =  \"1\";\n"
        //             "    mobile_country_code =  \"208\";\n"
        //             "    mobile_network_code =  \"93\";\n"
        //             "to\n"
        //             "    tracking_area_code  =  1; // no string!!\n"
        //             "    plmn_list = ( { mcc = 208; mnc = 93; mnc_length = 2; } )\n");
       // config_getlist(config_get_if(), &PLMNParamList, PLMNParams, sizeofArray(PLMNParams), gnbpath);

        if (GlobC_ARIAConfs.numplmnlist < 1 || GlobC_ARIAConfs.numplmnlist > 6)
          AssertFatal(0, "The number of PLMN IDs must be in [1,6], but is %d\n",
                      GlobC_ARIAConfs.numplmnlist);

        nrrrc_config.num_plmn = GlobC_ARIAConfs.numplmnlist;

        for (int l = 0; l < GlobC_ARIAConfs.numplmnlist; ++l) {
	
	  nrrrc_config.mcc[l]               = GlobC_ARIAConfs.y_gNBs->p_plmn.MCC;
	  nrrrc_config.mnc[l]               = GlobC_ARIAConfs.y_gNBs->p_plmn.MNC;
	  nrrrc_config.mnc_digit_length[l]  = GlobC_ARIAConfs.y_gNBs->p_plmn.MNC_len;
    // printf("nrrrc_config.mcc[%d]: %u\n", l, nrrrc_config.mcc[l]);
    // printf("nrrrc_config.mnc[%d]: %u\n", l, nrrrc_config.mnc[l]);
    // printf("nrrrc_config.mnc_digit_length[%d]: %u\n", l,nrrrc_config.mnc_digit_length[l]);
	  AssertFatal((nrrrc_config.mnc_digit_length[l] == 2) ||
		      (nrrrc_config.mnc_digit_length[l] == 3),"BAD MNC DIGIT LENGTH %d",
		      nrrrc_config.mnc_digit_length[l]);
        }
        //nrrrc_config.enable_sdap = *GNBParamList.paramarray[i][GNB_ENABLE_SDAP_IDX].iptr;
        nrrrc_config.enable_sdap = false;

        // LOG_I(GNB_APP, "SDAP layer is %s\n", nrrrc_config.enable_sdap ? "enabled" : "disabled");
        SM_Logs(LOG_INFO, _GNB_APP_, "SDAP LAYER STATUS:%s",nrrrc_config.enable_sdap ? "\033[0;32misENABLED\033[0m" : "\033[0;31misDISABLED\033[0m");

        nrrrc_config.drbs = GlobC_ARIAConfs.y_defvalues.D_DRBs;
        nrrrc_config.um_on_default_drb = GlobC_ARIAConfs.y_defvalues.Default_DRBs;
        // LOG_I(GNB_APP, "Data Radio Bearer count %d\n", nrrrc_config.drbs);

      }//
    }//End for (k=0; k <num_gnbs ; k++)
    openair_rrc_gNB_configuration(rrc, &nrrrc_config);
  }//End if (num_gnbs>0)
  // printf("About to go into config_security: \n");
  config_security(rrc);
  // printf("after   config_security: \n");
  return rrc;
}

int RCconfig_NR_NG(MessageDef *msg_p, uint32_t i) {

  int               j,k = 0;
  int               gnb_id;
  int32_t           my_int;
  const char*       active_gnb[MAX_GNB];
  char             *address                       = NULL;
  char             *cidr                          = NULL;

  // for no gcc warnings 

  (void)  my_int;

  memset((char*)active_gnb,0,MAX_GNB* sizeof(char*));
  char*             gnb_ipv4_address_for_NGU      = NULL;
  uint32_t          gnb_port_for_NGU              = 0;
  char*             gnb_ipv4_address_for_S1U      = NULL;
  uint32_t          gnb_port_for_S1U              = 0;

  paramdef_t GNBSParams[] = GNBSPARAMS_DESC;
  paramdef_t GNBParams[]  = GNBPARAMS_DESC;
  paramlist_def_t GNBParamList = {GNB_CONFIG_STRING_GNB_LIST,NULL,0};

  /* get global parameters, defined outside any section in the config file */
  //config_get(config_get_if(), GNBSParams, sizeofArray(GNBSParams), NULL);

  AssertFatal (i<GlobC_ARIAConfs.numGNBparmlist,
     "Failed to parse config file %s, %uth attribute %s \n",
     RC.config_file_name, i, GNB_CONFIG_STRING_ACTIVE_GNBS);
    
  
  if (GlobC_ARIAConfs.numGNBparmlist>0) {
    // Output a list of all gNBs.
    //config_getlist(config_get_if(), &GNBParamList, GNBParams, sizeofArray(GNBParams), NULL);
    if (GlobC_ARIAConfs.numGNBparmlist > 0) {
      for (k = 0; k < GlobC_ARIAConfs.numGNBparmlist; k++) {
        if (GlobC_ARIAConfs.y_gNBs->NodeKey == NULL) {
          // Calculate a default gNB ID
          if (IS_SA_MODE(get_softmodem_params())) {
            uint32_t hash;
          
          hash = ngap_generate_gNB_id ();
          gnb_id = k + (hash & 0xFFFFFF8);
          } else {
            gnb_id = k;
          }
        } else {
          gnb_id = GlobC_ARIAConfs.y_gNBs->NodeKey;
        }
  
  
        // search if in active list
        if (strcmp(GlobC_ARIAConfs.FunctionalNodes, GlobC_ARIAConfs.y_gNBs->EntityLabel) == 0) {
        for (j=0; j < GlobC_ARIAConfs.numGNBparmlist; j++) {
            paramdef_t PLMNParams[] = GNBPLMNPARAMS_DESC;
            paramlist_def_t PLMNParamList = {GNB_CONFIG_STRING_PLMN_LIST, NULL, 0};
            paramdef_t SNSSAIParams[] = GNBSNSSAIPARAMS_DESC;
            paramlist_def_t SNSSAIParamList = {GNB_CONFIG_STRING_SNSSAI_LIST, NULL, 0};
            /* map parameter checking array instances to parameter definition array instances */
            checkedparam_t config_check_PLMNParams [] = PLMNPARAMS_CHECK;
            checkedparam_t config_check_SNSSAIParams [] = SNSSAIPARAMS_CHECK;

            for (int I = 0; I < sizeofArray(PLMNParams); ++I)
              PLMNParams[I].chkPptr = &(config_check_PLMNParams[I]);
            for (int J = 0; J < sizeofArray(SNSSAIParams); ++J)
              SNSSAIParams[J].chkPptr = &(config_check_SNSSAIParams[J]);

            paramdef_t NGParams[]  = GNBNGPARAMS_DESC;
            paramlist_def_t NGParamList = {GNB_CONFIG_STRING_AMF_IP_ADDRESS,NULL,0};
            
            paramdef_t SCTPParams[]  = GNBSCTPPARAMS_DESC;
            paramdef_t NETParams[]  =  GNBNETPARAMS_DESC;
            char aprefix[MAX_OPTNAME_SIZE*2 + 8];
            sprintf(aprefix, "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, k);
            
            NGAP_REGISTER_GNB_REQ (msg_p).gNB_id = gnb_id;
            
            if (strcmp(GlobC_ARIAConfs.y_defvalues.Cell_typ, "CELL_MACRO_GNB") == 0) {
              NGAP_REGISTER_GNB_REQ (msg_p).cell_type = CELL_MACRO_GNB;
            } else  if (strcmp(GlobC_ARIAConfs.y_defvalues.Cell_typ, "CELL_HOME_GNB") == 0) {
              NGAP_REGISTER_GNB_REQ (msg_p).cell_type = CELL_HOME_ENB;
            } else {
              // AssertFatal (0,
              // "Failed to parse gNB configuration file %s, gnb %u unknown value \"%s\" for cell_type choice: CELL_MACRO_GNB or CELL_HOME_GNB !\n",
              // RC.config_file_name, i, *(GNBParamList.paramarray[k][GNB_CELL_TYPE_IDX].strptr));
            }
            
            NGAP_REGISTER_GNB_REQ (msg_p).gNB_name         = strdup(GlobC_ARIAConfs.y_gNBs->EntityLabel);
            NGAP_REGISTER_GNB_REQ (msg_p).tac              = GlobC_ARIAConfs.y_gNBs->TAC;
            // AssertFatal(!GNBParamList.paramarray[k][GNB_MOBILE_COUNTRY_CODE_IDX_OLD].strptr
            //             && !GNBParamList.paramarray[k][GNB_MOBILE_NETWORK_CODE_IDX_OLD].strptr,
            //             "It seems that you use an old configuration file. Please change the existing\n"
            //             "    tracking_area_code  =  \"1\";\n"
            //             "    mobile_country_code =  \"208\";\n"
            //             "    mobile_network_code =  \"93\";\n"
            //             "to\n"
            //             "    tracking_area_code  =  1; // no string!!\n"
            //             "    plmn_list = ( { mcc = 208; mnc = 93; mnc_length = 2; } )\n");
          //  config_getlist(config_get_if(), &PLMNParamList, PLMNParams, sizeofArray(PLMNParams), aprefix);

            if (GlobC_ARIAConfs.numplmnlist < 1 || GlobC_ARIAConfs.numplmnlist > 6)
              AssertFatal(0, "The number of PLMN IDs must be in [1,6], but is %d\n",
                          GlobC_ARIAConfs.numplmnlist);

            NGAP_REGISTER_GNB_REQ(msg_p).num_plmn = GlobC_ARIAConfs.numplmnlist;

            for (int l = 0; l < GlobC_ARIAConfs.numplmnlist; ++l) {
              char snssaistr[MAX_OPTNAME_SIZE*2 + 8];
              sprintf(snssaistr, "%s.[%i].%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, k, GNB_CONFIG_STRING_PLMN_LIST, l);
             // config_getlist(config_get_if(), &SNSSAIParamList, SNSSAIParams, sizeofArray(SNSSAIParams), snssaistr);

              NGAP_REGISTER_GNB_REQ(msg_p).plmn[l].mcc = GlobC_ARIAConfs.y_gNBs->p_plmn.MCC;
              NGAP_REGISTER_GNB_REQ(msg_p).plmn[l].mnc = GlobC_ARIAConfs.y_gNBs->p_plmn.MNC;
              NGAP_REGISTER_GNB_REQ(msg_p).plmn[l].mnc_digit_length = GlobC_ARIAConfs.y_gNBs->p_plmn.MNC_len;
              NGAP_REGISTER_GNB_REQ (msg_p).default_drx      = 0;
              // AssertFatal((NGAP_REGISTER_GNB_REQ(msg_p).plmn[l].mnc_digit_length == 2)
              //                 || (NGAP_REGISTER_GNB_REQ(msg_p).plmn[l].mnc_digit_length == 3),
              //             "BAD MNC DIGIT LENGTH %d",
              //             NGAP_REGISTER_GNB_REQ(msg_p).plmn[l].mnc_digit_length);

              NGAP_REGISTER_GNB_REQ(msg_p).plmn[l].num_nssai =GlobC_ARIAConfs.numSNSSAIlist;
              for (int s = 0; s < GlobC_ARIAConfs.numSNSSAIlist; ++s) {
                NGAP_REGISTER_GNB_REQ(msg_p).plmn[l].s_nssai[s].sst =
                   GlobC_ARIAConfs.y_gNBs->p_plmn.s_snssai.SvcTypeID;
                // SD is optional
                // 0xffffff is "no SD", see 23.003 Sec 28.4.2
                NGAP_REGISTER_GNB_REQ(msg_p).plmn[l].s_nssai[s].sd =
                    (GlobC_ARIAConfs.y_gNBs->p_plmn.s_snssai.SliceDiff & 0xffffff);
              }
            }
            sprintf(aprefix,"%s.[%i]",GNB_CONFIG_STRING_GNB_LIST,k);
           // config_getlist(config_get_if(), &NGParamList, NGParams, sizeofArray(NGParams), aprefix);

            NGAP_REGISTER_GNB_REQ (msg_p).nb_amf = 0;
            
            for (int l = 0; l < GlobC_ARIAConfs.numNGparamlist; l++) {
              
              NGAP_REGISTER_GNB_REQ (msg_p).nb_amf += 1;
              strcpy(NGAP_REGISTER_GNB_REQ (msg_p).amf_ip_address[l].ipv4_address,GlobC_ARIAConfs.y_gNBs->a_amf_ip.AMFDevIPv4);
              NGAP_REGISTER_GNB_REQ (msg_p).amf_ip_address[j].ipv4 = 1;
              NGAP_REGISTER_GNB_REQ (msg_p).amf_ip_address[j].ipv6 = 0;

              /* not in configuration yet ...
              if (NGParamList.paramarray[l][GNB_AMF_BROADCAST_PLMN_INDEX].iptr)
                NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_num[l] = NGParamList.paramarray[l][GNB_AMF_BROADCAST_PLMN_INDEX].numelt;
              else
                NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_num[l] = 0;

              AssertFatal(NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_num[l] <= NGAP_REGISTER_GNB_REQ(msg_p).num_plmn,
                          "List of broadcast PLMN to be sent to AMF can not be longer than actual "
                          "PLMN list (max %d, but is %d)\n",
                          NGAP_REGISTER_GNB_REQ(msg_p).num_plmn,
                          NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_num[l]);

              for (int el = 0; el < NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_num[l]; ++el) {
                // UINTARRAY gets mapped to int, see config_libconfig.c:223
                NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_index[l][el] = NGParamList.paramarray[l][GNB_AMF_BROADCAST_PLMN_INDEX].iptr[el];
                AssertFatal(NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_index[l][el] >= 0
                            && NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_index[l][el] < NGAP_REGISTER_GNB_REQ(msg_p).num_plmn,
                            "index for AMF's MCC/MNC (%d) is an invalid index for the registered PLMN IDs (%d)\n",
                            NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_index[l][el],
                            NGAP_REGISTER_GNB_REQ(msg_p).num_plmn);
              }
              */

              /* if no broadcasst_plmn array is defined, fill default values */
              if (NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_num[l] == 0) {
                NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_num[l] = NGAP_REGISTER_GNB_REQ(msg_p).num_plmn;

                for (int el = 0; el < NGAP_REGISTER_GNB_REQ(msg_p).num_plmn; ++el)
                  NGAP_REGISTER_GNB_REQ(msg_p).broadcast_plmn_index[l][el] = el;
              }
              
            }

          
            // SCTP SETTING
            NGAP_REGISTER_GNB_REQ (msg_p).sctp_out_streams = SCTP_OUT_STREAMS;
            NGAP_REGISTER_GNB_REQ (msg_p).sctp_in_streams  = SCTP_IN_STREAMS;
            if (IS_SA_MODE(get_softmodem_params())) {
              sprintf(aprefix,"%s.[%i].%s",GNB_CONFIG_STRING_GNB_LIST,k,GNB_CONFIG_STRING_SCTP_CONFIG);
              //config_get(config_get_if(), SCTPParams, sizeofArray(SCTPParams), aprefix);
              NGAP_REGISTER_GNB_REQ (msg_p).sctp_in_streams = (uint16_t)GlobC_ARIAConfs.y_gNBs->C_SCTP.FlexReceptionStreamSet;
              NGAP_REGISTER_GNB_REQ (msg_p).sctp_out_streams = (uint16_t)GlobC_ARIAConfs.y_gNBs->C_SCTP.FlexTransmissionStreamSet;
            }

            sprintf(aprefix,"%s.[%i].%s",GNB_CONFIG_STRING_GNB_LIST,k,GNB_CONFIG_STRING_NETWORK_INTERFACES_CONFIG);
            // NETWORK_INTERFACES
            //config_get(config_get_if(), NETParams, sizeofArray(NETParams), aprefix);

            //    NGAP_REGISTER_GNB_REQ (msg_p).enb_interface_name_for_NGU = strdup(enb_interface_name_for_NGU);
            cidr = GlobC_ARIAConfs.y_gNBs->n_net_if.NgAmfDevIPv4 ;
            char *save = NULL;
            address = strtok_r(cidr, "/", &save);
            
            NGAP_REGISTER_GNB_REQ (msg_p).gnb_ip_address.ipv6 = 0;
            NGAP_REGISTER_GNB_REQ (msg_p).gnb_ip_address.ipv4 = 1;
            
            strcpy(NGAP_REGISTER_GNB_REQ (msg_p).gnb_ip_address.ipv4_address, address);
            
            break;
          }
        }
      }
    }
  }
  return 0;
}

static pthread_mutex_t rc_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool rc_done = false;

/**
 * @brief This function is initializing the RAN context
 *        the its various layer instances
 */
void NRRCConfig(void)
{
  // Call NRRCConfig only once
  pthread_mutex_lock(&rc_mutex);
  if (rc_done) {
    // LOG_E(GNB_APP, "RAN Context has been already initialized\n");
    pthread_mutex_unlock(&rc_mutex);
    return;
  }

  memset((void *)&RC, 0, sizeof(RC));

  paramdef_t GNBSParams[]         = GNBSPARAMS_DESC;
  // config_get(config_get_if(), GNBSParams, sizeofArray(GNBSParams), NULL);


  if (GlobC_ARIAConfs.CUActive) {
    SM_Logs(LOG_INFO, _EXTRA_, "FETCHING PARAMETERS FOR ARIA");
    RC.nb_nr_inst = GlobC_ARIAConfs.active_gnb_count;
    // printf("RC.nb_nr_inst : %d\n", RC.nb_nr_inst);

    RC.nb_nr_macrlc_inst = 0;
    // printf("RC.nb_nr_macrlc_inst : %d\n", RC.nb_nr_macrlc_inst);

    RC.nb_nr_L1_inst = 0;
    // printf("RC.nb_nr_L1_inst : %d\n", RC.nb_nr_L1_inst);

    RC.nb_RU = 0;
    // printf("RC.nb_RU : %d\n", RC.nb_RU);
  } 

  if (global_configs.L1DUactive) {
    SM_Logs(LOG_INFO, _EXTRA_, "FETCHING PARAMETERS FOR ARIA");
    RC.nb_nr_inst = 1;
    // printf("RC.nb_nr_inst : %d\n", RC.nb_nr_inst);

    RC.nb_nr_macrlc_inst = global_vnf.numMACRLCs;
    // printf("RC.nb_nr_macrlc_inst : %d\n", RC.nb_nr_macrlc_inst);

    RC.nb_nr_L1_inst = 1;
    // printf("RC.nb_nr_L1_inst : %d\n", RC.nb_nr_L1_inst);

    RC.nb_RU = global_configs.numRUs;
    // printf("RC.nb_RU : %d\n", RC.nb_RU);
  } 

  // Set num of gNBs instances
  // RC.nb_nr_inst = GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt;
  //   printf("RC.nb_nr_inst: %d\n", RC.nb_nr_inst);
  // AssertFatal(RC.nb_nr_inst == NUMBER_OF_gNB_MAX,
  //             "Configuration error: RC.nb_nr_inst (%d) must equal NUMBER_OF_gNB_MAX (%d).\n"
  //             "Currently, only one instance of each layer (L1, L2, L3) is supported.\n"
  //             "Ensure that nb_nr_inst matches the maximum allowed gNB instances in this configuration.",
  //             RC.nb_nr_inst, NUMBER_OF_gNB_MAX);

  // // Set num MACRLC instances
  // paramlist_def_t MACRLCParamList = {CONFIG_STRING_MACRLC_LIST, NULL, 0};
  // config_getlist(config_get_if(), &MACRLCParamList, NULL, 0, NULL);
  // RC.nb_nr_macrlc_inst = MACRLCParamList.numelt;
  // printf("RC.nb_nr_macrlc_inst: %d\n", RC.nb_nr_macrlc_inst);

  // // Set num L1 instances
  // paramlist_def_t L1ParamList = {CONFIG_STRING_L1_LIST, NULL, 0};
  // config_getlist(config_get_if(), &L1ParamList, NULL, 0, NULL);
  // RC.nb_nr_L1_inst = L1ParamList.numelt;
  // printf("RC.nb_nr_L1_inst: %d\n", RC.nb_nr_L1_inst);

  // // Set num RU instances
  // paramlist_def_t RUParamList = {CONFIG_STRING_RU_LIST, NULL, 0};
  // config_getlist(config_get_if(), &RUParamList, NULL, 0, NULL);
  // RC.nb_RU = RUParamList.numelt;
  // printf("RC.nb_RU: %d\n", RC.nb_RU);

  // // Set num component carriers
  // RC.nb_nr_CC = calloc_or_fail(1, sizeof(*RC.nb_nr_CC));
  // *RC.nb_nr_CC = RC.nb_nr_L1_inst;
  // printf("RC.nb_nr_CC: %d\n", *RC.nb_nr_CC);
  // AssertFatal(*RC.nb_nr_CC <= MAX_NUM_CCs, "Configured number of CCs (%d) not supported\n", *RC.nb_nr_CC);

  // // LOG_I(GNB_APP,
  // //       "Initialized RAN Context: RC.nb_nr_inst = %d, RC.nb_nr_macrlc_inst = %d, RC.nb_nr_L1_inst = %d, RC.nb_RU = %d, "
  // //       "RC.nb_nr_CC[0] = %d\n",
  // //       RC.nb_nr_inst,
  // //       RC.nb_nr_macrlc_inst,
  // //       RC.nb_nr_L1_inst,
  // //       RC.nb_RU,
  // //       *RC.nb_nr_CC);

  rc_done = true;
  pthread_mutex_unlock(&rc_mutex);
}

int RCconfig_NR_X2(MessageDef *msg_p, uint32_t i) {
  int   J, l;
  char *address = NULL;
  char *cidr    = NULL;
  //int                    num_gnbs                                                      = 0;
  //int                    num_component_carriers                                        = 0;
  int                    j,k                                                           = 0;
  int32_t                gnb_id                                                        = 0;

  paramdef_t GNBSParams[] = GNBSPARAMS_DESC;
  ////////// Identification parameters
  paramdef_t GNBParams[]  = GNBPARAMS_DESC;
  paramlist_def_t GNBParamList = {GNB_CONFIG_STRING_GNB_LIST,NULL,0};
  /* get global parameters, defined outside any section in the config file */
  config_get(config_get_if(), GNBSParams, sizeofArray(GNBSParams), NULL);
  NR_ServingCellConfigCommon_t *scc = calloc_or_fail(1, sizeof(*scc));
  uint64_t ssb_bitmap=0xff;
  memset((void*)scc,0,sizeof(NR_ServingCellConfigCommon_t));
  prepare_scc(scc);
  paramdef_t SCCsParams[] = SCCPARAMS_DESC(scc);
  paramlist_def_t SCCsParamList = {GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON, NULL, 0};

  // AssertFatal(i < GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt,
  //             "Failed to parse config file %s, %uth attribute %s \n",
  //             RC.config_file_name, i, GNB_CONFIG_STRING_ACTIVE_GNBS);

  if (GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt > 0) {
    // Output a list of all gNBs.
    config_getlist(config_get_if(), &GNBParamList, GNBParams, sizeofArray(GNBParams), NULL);

    if (GNBParamList.numelt > 0) {
      for (k = 0; k < GNBParamList.numelt; k++) {
        if (GNBParamList.paramarray[k][GNB_GNB_ID_IDX].uptr == NULL) {
          // Calculate a default eNB ID
          if (IS_SA_MODE(get_softmodem_params())) {
            uint32_t hash;
            hash = ngap_generate_gNB_id ();
            gnb_id = k + (hash & 0xFFFFFF8);
          } else {
            gnb_id = k;
          }
        } else {
          gnb_id = *(GNBParamList.paramarray[k][GNB_GNB_ID_IDX].uptr);
        }

        // search if in active list
        for (j = 0; j < GNBSParams[GNB_ACTIVE_GNBS_IDX].numelt; j++) {
          if (strcmp(GNBSParams[GNB_ACTIVE_GNBS_IDX].strlistptr[j], *(GNBParamList.paramarray[k][GNB_GNB_NAME_IDX].strptr)) == 0) {
            paramdef_t PLMNParams[] = GNBPLMNPARAMS_DESC;
            paramlist_def_t PLMNParamList = {GNB_CONFIG_STRING_PLMN_LIST, NULL, 0};
            /* map parameter checking array instances to parameter definition array instances */
            checkedparam_t config_check_PLMNParams [] = PLMNPARAMS_CHECK;

            for (int I = 0; I < sizeofArray(PLMNParams); ++I)
              PLMNParams[I].chkPptr = &(config_check_PLMNParams[I]);

            paramdef_t X2Params[]  = X2PARAMS_DESC;
            paramlist_def_t X2ParamList = {ENB_CONFIG_STRING_TARGET_ENB_X2_IP_ADDRESS,NULL,0};
            paramdef_t SCTPParams[]  = GNBSCTPPARAMS_DESC;
	    char*             gnb_ipv4_address_for_NGU      = NULL;
	    uint32_t          gnb_port_for_NGU              = 0;
	    char*             gnb_ipv4_address_for_S1U      = NULL;
	    uint32_t          gnb_port_for_S1U              = 0;

            paramdef_t NETParams[]  =  GNBNETPARAMS_DESC;
            /* TODO: fix the size - if set lower we have a crash (MAX_OPTNAME_SIZE was 64 when this code was written) */
            /* this is most probably a problem with the config module */
            char aprefix[MAX_OPTNAME_SIZE*80 + 8];
            sprintf(aprefix,"%s.[%i]",GNB_CONFIG_STRING_GNB_LIST,k);
            /* Some default/random parameters */
            X2AP_REGISTER_ENB_REQ (msg_p).eNB_id = gnb_id;

            if (strcmp(*(GNBParamList.paramarray[k][GNB_CELL_TYPE_IDX].strptr), "CELL_MACRO_GNB") == 0) {
              X2AP_REGISTER_ENB_REQ (msg_p).cell_type = CELL_MACRO_GNB;
            }else {
              // AssertFatal (0,
              //              "Failed to parse eNB configuration file %s, enb %u unknown value \"%s\" for cell_type choice: CELL_MACRO_ENB or CELL_HOME_ENB !\n",
              //              RC.config_file_name, i, *(GNBParamList.paramarray[k][GNB_CELL_TYPE_IDX].strptr));
            }

            X2AP_REGISTER_ENB_REQ (msg_p).eNB_name         = strdup(*(GNBParamList.paramarray[k][GNB_GNB_NAME_IDX].strptr));
            X2AP_REGISTER_ENB_REQ (msg_p).tac              = *GNBParamList.paramarray[k][GNB_TRACKING_AREA_CODE_IDX].uptr;
            config_getlist(config_get_if(), &PLMNParamList, PLMNParams, sizeofArray(PLMNParams), aprefix);

            if (PLMNParamList.numelt < 1 || PLMNParamList.numelt > 6){}
              // AssertFatal(0, "The number of PLMN IDs must be in [1,6], but is %d\n",
              //             PLMNParamList.numelt);

            if (PLMNParamList.numelt > 1){}
              // LOG_W(X2AP, "X2AP currently handles only one PLMN, ignoring the others!\n");

            X2AP_REGISTER_ENB_REQ (msg_p).mcc = *PLMNParamList.paramarray[0][GNB_MOBILE_COUNTRY_CODE_IDX].uptr;
            X2AP_REGISTER_ENB_REQ (msg_p).mnc = *PLMNParamList.paramarray[0][GNB_MOBILE_NETWORK_CODE_IDX].uptr;
            X2AP_REGISTER_ENB_REQ (msg_p).mnc_digit_length = *PLMNParamList.paramarray[0][GNB_MNC_DIGIT_LENGTH].u8ptr;
            // AssertFatal(X2AP_REGISTER_ENB_REQ(msg_p).mnc_digit_length == 3
            //             || X2AP_REGISTER_ENB_REQ(msg_p).mnc < 100,
            //             "MNC %d cannot be encoded in two digits as requested (change mnc_digit_length to 3)\n",
            //             X2AP_REGISTER_ENB_REQ(msg_p).mnc);

            sprintf(aprefix, "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0);

            config_getlist(config_get_if(), &SCCsParamList, NULL, 0, aprefix);
            if (SCCsParamList.numelt > 0) {
              sprintf(aprefix, "%s.[%i].%s.[%i]", GNB_CONFIG_STRING_GNB_LIST,0,GNB_CONFIG_STRING_SERVINGCELLCONFIGCOMMON, 0);
              config_get(config_get_if(), SCCsParams, sizeofArray(SCCsParams), aprefix);
              fix_scc(scc,ssb_bitmap);
            }
            X2AP_REGISTER_ENB_REQ (msg_p).num_cc = SCCsParamList.numelt;
            for (J = 0; J < SCCsParamList.numelt ; J++) {
              struct NR_FrequencyInfoDL *frequencyInfoDL = scc->downlinkConfigCommon->frequencyInfoDL;
              X2AP_REGISTER_ENB_REQ(msg_p).nr_band[J] = *frequencyInfoDL->frequencyBandList.list.array[0]; // nr_band; //78
              X2AP_REGISTER_ENB_REQ(msg_p).nrARFCN[J] = frequencyInfoDL->absoluteFrequencyPointA;
              X2AP_REGISTER_ENB_REQ (msg_p).uplink_frequency_offset[J] = scc->uplinkConfigCommon->frequencyInfoUL->scs_SpecificCarrierList.list.array[0]->offsetToCarrier; //0
              X2AP_REGISTER_ENB_REQ (msg_p).Nid_cell[J]= *scc->physCellId; //0
              X2AP_REGISTER_ENB_REQ(msg_p).N_RB_DL[J] =
                  frequencyInfoDL->scs_SpecificCarrierList.list.array[0]->carrierBandwidth; // 106
              X2AP_REGISTER_ENB_REQ (msg_p).frame_type[J] = TDD;
              // LOG_I(X2AP, "gNB configuration parameters: nr_band: %d, nr_ARFCN: %d, DL_RBs: %d, num_cc: %d \n",
              //     X2AP_REGISTER_ENB_REQ (msg_p).nr_band[J],
              //     X2AP_REGISTER_ENB_REQ (msg_p).nrARFCN[J],
              //     X2AP_REGISTER_ENB_REQ (msg_p).N_RB_DL[J],
              //     X2AP_REGISTER_ENB_REQ (msg_p).num_cc);
            }

            sprintf(aprefix,"%s.[%i]",GNB_CONFIG_STRING_GNB_LIST,k);
            config_getlist(config_get_if(), &X2ParamList, X2Params, sizeofArray(X2Params), aprefix);
            // AssertFatal(X2ParamList.numelt <= X2AP_MAX_NB_ENB_IP_ADDRESS,
            //             "value of X2ParamList.numelt %d must be lower than X2AP_MAX_NB_ENB_IP_ADDRESS %d value: reconsider to increase X2AP_MAX_NB_ENB_IP_ADDRESS\n",
            //             X2ParamList.numelt,X2AP_MAX_NB_ENB_IP_ADDRESS);
            X2AP_REGISTER_ENB_REQ (msg_p).nb_x2 = 0;

            for (l = 0; l < X2ParamList.numelt; l++) {
              X2AP_REGISTER_ENB_REQ (msg_p).nb_x2 += 1;
              strcpy(X2AP_REGISTER_ENB_REQ (msg_p).target_enb_x2_ip_address[l].ipv4_address,*(X2ParamList.paramarray[l][ENB_X2_IPV4_ADDRESS_IDX].strptr));
              X2AP_REGISTER_ENB_REQ (msg_p).target_enb_x2_ip_address[l].ipv4 = 1;
              X2AP_REGISTER_ENB_REQ (msg_p).target_enb_x2_ip_address[l].ipv6 = 0;
            }

            // timers
            {
              int t_reloc_prep = 0;
              int tx2_reloc_overall = 0;
              int t_dc_prep = 0;
              int t_dc_overall = 0;
              paramdef_t p[] = {
                { "t_reloc_prep", "t_reloc_prep", 0, .iptr=&t_reloc_prep, .defintval=0, TYPE_INT, 0 },
                { "tx2_reloc_overall", "tx2_reloc_overall", 0, .iptr=&tx2_reloc_overall, .defintval=0, TYPE_INT, 0 },
                { "t_dc_prep", "t_dc_prep", 0, .iptr=&t_dc_prep, .defintval=0, TYPE_INT, 0 },
                { "t_dc_overall", "t_dc_overall", 0, .iptr=&t_dc_overall, .defintval=0, TYPE_INT, 0 }
              };
              config_get(config_get_if(), p, sizeofArray(p), aprefix);

              if (t_reloc_prep <= 0 || t_reloc_prep > 10000 ||
                  tx2_reloc_overall <= 0 || tx2_reloc_overall > 20000 ||
                  t_dc_prep <= 0 || t_dc_prep > 10000 ||
                  t_dc_overall <= 0 || t_dc_overall > 20000) {
                // LOG_E(X2AP, "timers in configuration file have wrong values. We must have [0 < t_reloc_prep <= 10000] and [0 < tx2_reloc_overall <= 20000] and [0 < t_dc_prep <= 10000] and [0 < t_dc_overall <= 20000]\n");
                exit(1);
              }

              X2AP_REGISTER_ENB_REQ (msg_p).t_reloc_prep = t_reloc_prep;
              X2AP_REGISTER_ENB_REQ (msg_p).tx2_reloc_overall = tx2_reloc_overall;
              X2AP_REGISTER_ENB_REQ (msg_p).t_dc_prep = t_dc_prep;
              X2AP_REGISTER_ENB_REQ (msg_p).t_dc_overall = t_dc_overall;
            }
            // SCTP SETTING
            X2AP_REGISTER_ENB_REQ (msg_p).sctp_out_streams = SCTP_OUT_STREAMS;
            X2AP_REGISTER_ENB_REQ (msg_p).sctp_in_streams  = SCTP_IN_STREAMS;

            if (IS_SA_MODE(get_softmodem_params())) {
              sprintf(aprefix,"%s.[%i].%s",GNB_CONFIG_STRING_GNB_LIST,k,GNB_CONFIG_STRING_SCTP_CONFIG);
              config_get(config_get_if(), SCTPParams, sizeofArray(SCTPParams), aprefix);
              X2AP_REGISTER_ENB_REQ (msg_p).sctp_in_streams = (uint16_t)*(SCTPParams[GNB_SCTP_INSTREAMS_IDX].uptr);
              X2AP_REGISTER_ENB_REQ (msg_p).sctp_out_streams = (uint16_t)*(SCTPParams[GNB_SCTP_OUTSTREAMS_IDX].uptr);
            }

            sprintf(aprefix,"%s.[%i].%s",GNB_CONFIG_STRING_GNB_LIST,k,GNB_CONFIG_STRING_NETWORK_INTERFACES_CONFIG);
            // NETWORK_INTERFACES
            config_get(config_get_if(), NETParams, sizeofArray(NETParams), aprefix);
            X2AP_REGISTER_ENB_REQ (msg_p).enb_port_for_X2C = (uint32_t)*(NETParams[GNB_PORT_FOR_X2C_IDX].uptr);

            //temp out
            if ((NETParams[GNB_IPV4_ADDR_FOR_X2C_IDX].strptr == NULL) || (X2AP_REGISTER_ENB_REQ (msg_p).enb_port_for_X2C == 0)) {
              // LOG_E(RRC,"Add eNB IPv4 address and/or port for X2C in the CONF file!\n");
              exit(1);
            }

            cidr = *(NETParams[ENB_IPV4_ADDR_FOR_X2C_IDX].strptr);
            char *save = NULL;
            address = strtok_r(cidr, "/", &save);
            X2AP_REGISTER_ENB_REQ (msg_p).enb_x2_ip_address.ipv6 = 0;
            X2AP_REGISTER_ENB_REQ (msg_p).enb_x2_ip_address.ipv4 = 1;
            strcpy(X2AP_REGISTER_ENB_REQ (msg_p).enb_x2_ip_address.ipv4_address, address);
          }
        }
      }
    }
  }

  return 0;
}

void wait_f1_setup_response(void)
{
  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  if (mac->f1_config.setup_resp != NULL) {
    NR_SCHED_UNLOCK(&mac->sched_lock);
    return;
  }

  // LOG_W(GNB_APP, "waiting for F1 Setup Response before activating radio\n");

  /* for the moment, we keep it simple and just sleep to periodically check.
   * The actual check is protected by a mutex */
  while (mac->f1_config.setup_resp == NULL) {
    NR_SCHED_UNLOCK(&mac->sched_lock);
    sleep(1);
    NR_SCHED_LOCK(&mac->sched_lock);
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);
}
static bool check_plmn_identity(const f1ap_plmn_t *check_plmn, const f1ap_plmn_t *plmn)
{
  return plmn->mcc == check_plmn->mcc && plmn->mnc_digit_length == check_plmn->mnc_digit_length && plmn->mnc == check_plmn->mnc;
}

int gNB_app_handle_f1ap_gnb_cu_configuration_update(f1ap_gnb_cu_configuration_update_t *gnb_cu_cfg_update) {
  int i, j, ret=0;
  // LOG_I(GNB_APP, "cells_to_activate %d, RRC instances %d\n",
  //       gnb_cu_cfg_update->num_cells_to_activate, RC.nb_nr_inst);

  // AssertFatal(gnb_cu_cfg_update->num_cells_to_activate == 1, "only one cell supported at the moment\n");
  gNB_MAC_INST *mac = RC.nrmac[0];
  NR_SCHED_LOCK(&mac->sched_lock);
  for (j = 0; j < gnb_cu_cfg_update->num_cells_to_activate; j++) {
    for (i = 0; i < RC.nb_nr_inst; i++) {
      f1ap_setup_req_t *setup_req = RC.nrmac[i]->f1_config.setup_req;
      // identify local index of cell j by nr_cellid, plmn identity and physical cell ID

      if (setup_req->cell[0].info.nr_cellid == gnb_cu_cfg_update->cells_to_activate[j].nr_cellid
          && check_plmn_identity(&setup_req->cell[0].info.plmn, &gnb_cu_cfg_update->cells_to_activate[j].plmn) > 0
          && setup_req->cell[0].info.nr_pci == gnb_cu_cfg_update->cells_to_activate[j].nrpci) {
        // copy system information and decode it
        // AssertFatal(gnb_cu_cfg_update->cells_to_activate[j].num_SI == 0,
        //             "gNB-CU Configuration Update: handling of additional SIs not implemend\n");
        ret++;
        mac->f1_config.setup_resp = malloc(sizeof(*mac->f1_config.setup_resp));
        // AssertFatal(mac->f1_config.setup_resp != NULL, "out of memory\n");
        mac->f1_config.setup_resp->num_cells_to_activate = gnb_cu_cfg_update->num_cells_to_activate;
        mac->f1_config.setup_resp->cells_to_activate[0] = gnb_cu_cfg_update->cells_to_activate[0];
      } else {
        // LOG_E(GNB_APP, "GNB_CU_CONFIGURATION_UPDATE not matching\n");
      }
    }
  }
  NR_SCHED_UNLOCK(&mac->sched_lock);
  /* Free F1AP struct after use */
  free_f1ap_cu_configuration_update(gnb_cu_cfg_update);
  MessageDef *msg_ack_p = NULL;
  if (ret > 0) {
    // generate gNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE
    msg_ack_p = itti_alloc_new_message (TASK_GNB_APP, 0, F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE);
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).num_cells_failed_to_be_activated = 0;
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).have_criticality = 0; 
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).noofTNLAssociations_to_setup =0;
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).noofTNLAssociations_failed = 0;
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).noofDedicatedSIDeliveryNeededUEs = 0;
    F1AP_GNB_CU_CONFIGURATION_UPDATE_ACKNOWLEDGE(msg_ack_p).transaction_id = F1AP_get_next_transaction_identifier(0, 0);
    itti_send_msg_to_task (TASK_DU_F1, INSTANCE_DEFAULT, msg_ack_p);

  }
  else {
    // generate gNB_CU_CONFIGURATION_UPDATE_FAILURE
    msg_ack_p = itti_alloc_new_message (TASK_GNB_APP, 0, F1AP_GNB_CU_CONFIGURATION_UPDATE_FAILURE);
    F1AP_GNB_CU_CONFIGURATION_UPDATE_FAILURE(msg_ack_p).cause = F1AP_CauseRadioNetwork_cell_not_available;

    itti_send_msg_to_task (TASK_DU_F1, INSTANCE_DEFAULT, msg_ack_p);

  }

  return(ret);
}

ngran_node_t get_node_type(void)
{
  // printf("testing--> inside get_node_type\n");
  paramdef_t        MacRLC_Params[] = MACRLCPARAMS_DESC;
  paramlist_def_t   MacRLC_ParamList = {CONFIG_STRING_MACRLC_LIST,NULL,0};
  paramdef_t        GNBParams[]  = GNBPARAMS_DESC;
  paramlist_def_t   GNBParamList = {GNB_CONFIG_STRING_GNB_LIST,NULL,0};
  paramdef_t        GNBE1Params[] = GNBE1PARAMS_DESC;
  paramlist_def_t   GNBE1ParamList = {GNB_CONFIG_STRING_E1_PARAMETERS, NULL, 0};

  // config_getlist(config_get_if(), &GNBParamList, GNBParams, sizeofArray(GNBParams), NULL);

  if (global_vnf.numactiveGNBs == 0) {// We have no valid configuration, let's return a default 
    // printf("No valid GNBParamList configuration found. Returning ngran_gNB.\n");
    return ngran_gNB;
  }
  // printf("testing--> inside get_node_type 1\n");
  // config_getlist(config_get_if(), &MacRLC_ParamList, MacRLC_Params, sizeofArray(MacRLC_Params), NULL);
  char aprefix[MAX_OPTNAME_SIZE*2 + 8];
  sprintf(aprefix, "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0);
  // config_getlist(config_get_if(), &GNBE1ParamList, GNBE1Params, sizeofArray(GNBE1Params), aprefix);
  for (int j = 0; j < global_vnf.numMACRLCs; j++) {
    if (strcmp(global_vnf.macrlcs->tr_n_preference, "f1") == 0) {
      // printf("Found 'f1' in MacRLC_ParamList. Returning ngran_gNB_DU.\n");
      return ngran_gNB_DU; // MACRLCs present in config: it must be a DU
    }
  }
  // printf("testing--> inside get_node_type 2\n");
  if (strcmp(GlobC_ARIAConfs.y_gNBs->RIF_Choice, "f1") == 0) {
    // printf("'f1' found in GNBParamList. Checking GNBE1ParamList...\n");
    if ( GNBE1ParamList.paramarray == NULL || GNBE1ParamList.numelt == 0 ){
      // printf("GNBE1ParamList is NULL or empty. Returning ngran_gNB_CU.\n");
      return ngran_gNB_CU;
    }
    else if (strcmp(*(GNBE1ParamList.paramarray[0][GNB_CONFIG_E1_CU_TYPE_IDX].strptr), "cp") == 0)
      return ngran_gNB_CUCP;
    else if (strcmp(*(GNBE1ParamList.paramarray[0][GNB_CONFIG_E1_CU_TYPE_IDX].strptr), "up") == 0)
      return ngran_gNB_CUUP;
    else
      return ngran_gNB_CU;
  } else {
    return ngran_gNB;
  }
}
