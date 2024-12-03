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

#include "gtest/gtest.h"
extern "C" {
#include "openair2/LAYER2/NR_MAC_UE/mac_proto.h"
#include "executables/softmodem-common.h"
uint64_t get_softmodem_optmask(void)
{
  return 0;
}
static softmodem_params_t softmodem_params;
softmodem_params_t *get_softmodem_params(void)
{
  return &softmodem_params;
}
void nr_mac_rrc_ra_ind(const module_id_t mod_id, int frame, bool success)
{
}
int nr_write_ce_ulsch_pdu(uint8_t *mac_ce,
                          NR_UE_MAC_INST_t *mac,
                          NR_SINGLE_ENTRY_PHR_MAC_CE *power_headroom,
                          const type_bsr_t *bsr,
                          uint8_t *mac_ce_end)
{
  return 0;
}
int nr_write_ce_msg3_pdu(uint8_t *mac_ce, NR_UE_MAC_INST_t *mac, rnti_t crnti, uint8_t *mac_ce_end)
{
  return 0;
}
tbs_size_t mac_rlc_data_req(const module_id_t module_idP,
                            const rnti_t rntiP,
                            const eNB_index_t eNB_index,
                            const frame_t frameP,
                            const eNB_flag_t enb_flagP,
                            const MBMS_flag_t MBMS_flagP,
                            const logical_chan_id_t channel_idP,
                            const tb_size_t tb_sizeP,
                            char *buffer_pP,
                            const uint32_t sourceL2Id,
                            const uint32_t destinationL2Id)
{
  return 0;
}
fapi_nr_ul_config_request_pdu_t *lockGet_ul_config(NR_UE_MAC_INST_t *mac, frame_t frame_tx, int slot_tx, uint8_t pdu_type)
{
  return nullptr;
}
void release_ul_config(fapi_nr_ul_config_request_pdu_t *configPerSlot, bool clearIt)
{
}
void remove_ul_config_last_item(fapi_nr_ul_config_request_pdu_t *pdu)
{
}
int nr_ue_configure_pucch(NR_UE_MAC_INST_t *mac,
                          int slot,
                          frame_t frame,
                          uint16_t rnti,
                          PUCCH_sched_t *pucch,
                          fapi_nr_ul_config_pucch_pdu *pucch_pdu)
{
  return 0;
}
}
#include <cstdio>
#include "common/utils/LOG/log.h"

TEST(test_init_ra, four_step_cbra)
{
  NR_UE_MAC_INST_t mac = {0};
  NR_PRACH_RESOURCES_t prach_resources = {0};
  NR_RACH_ConfigCommon_t nr_rach_ConfigCommon = {0};
  NR_RACH_ConfigGeneric_t rach_ConfigGeneric = {0};
  NR_RACH_ConfigDedicated_t *rach_ConfigDedicated = nullptr;
  NR_UE_UL_BWP_t current_bwp;
  mac.current_UL_BWP = &current_bwp;
  long scs = 1;
  current_bwp.scs = scs;
  current_bwp.channel_bandwidth = 40;
  nr_rach_ConfigCommon.msg1_SubcarrierSpacing = &scs;
  mac.p_Max = 23;
  mac.nr_band = 78;
  mac.frame_type = TDD;
  mac.frequency_range = FR1;

  init_RA(&mac, &prach_resources, &nr_rach_ConfigCommon, &rach_ConfigGeneric, rach_ConfigDedicated);

  EXPECT_EQ(mac.ra.ra_type, RA_4_STEP);
  EXPECT_EQ(mac.state, UE_PERFORMING_RA);
  EXPECT_EQ(mac.ra.RA_active, true);
  EXPECT_EQ(mac.ra.cfra, 0);
}

TEST(test_init_ra, four_step_cfra)
{
  NR_UE_MAC_INST_t mac = {0};
  NR_PRACH_RESOURCES_t prach_resources = {0};
  NR_RACH_ConfigCommon_t nr_rach_ConfigCommon = {0};
  NR_RACH_ConfigGeneric_t rach_ConfigGeneric = {0};
  NR_UE_UL_BWP_t current_bwp;
  mac.current_UL_BWP = &current_bwp;
  long scs = 1;
  current_bwp.scs = scs;
  current_bwp.channel_bandwidth = 40;
  nr_rach_ConfigCommon.msg1_SubcarrierSpacing = &scs;
  mac.p_Max = 23;
  mac.nr_band = 78;
  mac.frame_type = TDD;
  mac.frequency_range = FR1;

  NR_RACH_ConfigDedicated_t rach_ConfigDedicated = {0};
  struct NR_CFRA cfra;
  rach_ConfigDedicated.cfra = &cfra;

  init_RA(&mac, &prach_resources, &nr_rach_ConfigCommon, &rach_ConfigGeneric, &rach_ConfigDedicated);

  EXPECT_EQ(mac.ra.ra_type, RA_4_STEP);
  EXPECT_EQ(mac.state, UE_PERFORMING_RA);
  EXPECT_EQ(mac.ra.RA_active, true);
  EXPECT_EQ(mac.ra.cfra, 1);
}

int main(int argc, char **argv)
{
  logInit();
  uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY);
  g_log->log_component[MAC].level = OAILOG_DEBUG;
  g_log->log_component[NR_MAC].level = OAILOG_DEBUG;
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
