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


#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>


#include "T.h"

#undef MALLOC //there are two conflicting definitions, so we better make sure we don't use it at all
#include <common/utils/assertions.h>
#include "common/oai_version.h"

#include "PHY/types.h"
#include "common/ran_context.h"

#include "PHY/defs_gNB.h"
#include "PHY/defs_common.h"
#include "common/config/config_userapi.h"
#include "common/utils/load_module_shlib.h"
#undef MALLOC //there are two conflicting definitions, so we better make sure we don't use it at all
//#undef FRAME_LENGTH_COMPLEX_SAMPLES //there are two conflicting definitions, so we better make sure we don't use it at all

#include "radio/COMMON/common_lib.h"
#include "radio/ETHERNET/if_defs.h"

//#undef FRAME_LENGTH_COMPLEX_SAMPLES //there are two conflicting definitions, so we better make sure we don't use it at all

#include "PHY/phy_vars.h"
#include "RRC/LTE/rrc_vars.h"
#include "PHY_INTERFACE/phy_interface_vars.h"
#include "gnb_config.h"
#include "SIMULATION/TOOLS/sim.h"

#ifdef SMBV
#include "PHY/TOOLS/smbv.h"
unsigned short config_frames[4] = {2,9,11,13};
#endif

#include "common/utils/LOG/log.h"
#include "common/utils/LOG/vcd_signal_dumper.h"
#include "UTIL/OPT/opt.h"
#include "LAYER2/nr_pdcp/nr_pdcp_oai_api.h"

#include "intertask_interface.h"

#include "PHY/INIT/nr_phy_init.h"

#include "system.h"
#include <openair2/GNB_APP/gnb_app.h>
#include "PHY/TOOLS/phy_scope_interface.h"
#include "PHY/TOOLS/nr_phy_scope.h"
#include "aria.h"
#include "executables/softmodem-common.h"
#include "executables/thread-common.h"
#include "NB_IoT_interface.h"
#include "x2ap_eNB.h"
#include "ngap_gNB.h"
#include "gnb_paramdef.h"
#include <openair3/ocp-gtpu/gtp_itf.h>
#include "nfapi/oai_integration/vendor_ext.h"
#include "gnb_config.h"
#include "openair2/E1AP/e1ap_common.h"
#include "LAYER2/NR_MAC_gNB/nr_mac_gNB.h"
#include <stdatomic.h>

#ifdef ENABLE_AERIAL
#include "nfapi/oai_integration/aerial/fapi_nvIPC.h"
#endif
#ifdef E2_AGENT
#include "openair2/E2AP/flexric/src/agent/e2_agent_api.h"
#include "openair2/E2AP/RAN_FUNCTION/init_ran_func.h"
#endif

// IMPORTANT

pthread_cond_t nfapi_sync_cond;
pthread_mutex_t nfapi_sync_mutex;
int nfapi_sync_var=-1; //!< protected by mutex \ref nfapi_sync_mutex
THREAD_STRUCT thread_struct;
pthread_cond_t sync_cond;
pthread_mutex_t sync_mutex;
int sync_var=-1; //!< protected by mutex \ref sync_mutex.
int config_sync_var=-1;
int oai_exit = 0;

unsigned int mmapped_dma=0;

uint64_t downlink_frequency[MAX_NUM_CCs][4];
int32_t uplink_frequency_offset[MAX_NUM_CCs][4];
char *uecap_file;

runmode_t mode = normal_txrx;

#if MAX_NUM_CCs == 1
rx_gain_t rx_gain_mode[MAX_NUM_CCs][4] = {{max_gain,max_gain,max_gain,max_gain}};
double tx_gain[MAX_NUM_CCs][4] = {{20,0,0,0}};
double rx_gain[MAX_NUM_CCs][4] = {{110,0,0,0}};
#else
rx_gain_t rx_gain_mode[MAX_NUM_CCs][4] = {{max_gain,max_gain,max_gain,max_gain},{max_gain,max_gain,max_gain,max_gain}};
double tx_gain[MAX_NUM_CCs][4] = {{20,0,0,0},{20,0,0,0}};
double rx_gain[MAX_NUM_CCs][4] = {{110,0,0,0},{20,0,0,0}};
#endif

double rx_gain_off = 0.0;

static int tx_max_power[MAX_NUM_CCs]; /* =  {0,0}*/;
int chain_offset=0;
extern void *udp_eNB_task(void *args_p);
int emulate_rf = 0;
int numerology = 0;
double cpuf;

/* hack: pdcp_run() is required by 4G scheduler which is compiled into
 * nr-softmodem because of linker issues */
void pdcp_run(const protocol_ctxt_t *const ctxt_pP)
{
  abort();
}

/*---------------------BMC: timespec helpers -----------------------------*/

struct timespec min_diff_time = { .tv_sec = 0, .tv_nsec = 0 };
struct timespec max_diff_time = { .tv_sec = 0, .tv_nsec = 0 };

struct timespec clock_difftime(struct timespec start, struct timespec end) {
  struct timespec temp;

  if ((end.tv_nsec-start.tv_nsec)<0) {
    temp.tv_sec = end.tv_sec-start.tv_sec-1;
    temp.tv_nsec = 1000000000+end.tv_nsec-start.tv_nsec;
  } else {
    temp.tv_sec = end.tv_sec-start.tv_sec;
    temp.tv_nsec = end.tv_nsec-start.tv_nsec;
  }

  return temp;
}

void print_difftimes(void)
{
  // LOG_I(HW, "difftimes min = %lu ns ; max = %lu ns\n", min_diff_time.tv_nsec, max_diff_time.tv_nsec);
}

void update_difftimes(struct timespec start, struct timespec end) {
  struct timespec diff_time = { .tv_sec = 0, .tv_nsec = 0 };
  int             changed = 0;
  diff_time = clock_difftime(start, end);

  if ((min_diff_time.tv_nsec == 0) || (diff_time.tv_nsec < min_diff_time.tv_nsec)) {
    min_diff_time.tv_nsec = diff_time.tv_nsec;
    changed = 1;
  }

  if ((max_diff_time.tv_nsec == 0) || (diff_time.tv_nsec > max_diff_time.tv_nsec)) {
    max_diff_time.tv_nsec = diff_time.tv_nsec;
    changed = 1;
  }

#if 1

  if (changed) print_difftimes();

#endif
}

/*------------------------------------------------------------------------*/

unsigned int build_rflocal(int txi, int txq, int rxi, int rxq) {
  return (txi + (txq<<6) + (rxi<<12) + (rxq<<18));
}
unsigned int build_rfdc(int dcoff_i_rxfe, int dcoff_q_rxfe) {
  return (dcoff_i_rxfe + (dcoff_q_rxfe<<8));
}


#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KBLU  "\x1B[34m"
#define RESET "\033[0m"

void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  int ru_id;

  if (s != NULL) {
    // printf("%s:%d %s() Exiting OAI softmodem: %s\n",file,line, function, s);
  }

  oai_exit = 1;

  if (RC.ru == NULL)
    exit(-1); // likely init not completed, prevent crash or hang, exit now...

  for (ru_id=0; ru_id<RC.nb_RU; ru_id++) {
    if (RC.ru[ru_id] && RC.ru[ru_id]->rfdevice.trx_end_func) {
      RC.ru[ru_id]->rfdevice.trx_end_func(&RC.ru[ru_id]->rfdevice);
      RC.ru[ru_id]->rfdevice.trx_end_func = NULL;
    }

    if (RC.ru[ru_id] && RC.ru[ru_id]->ifdevice.trx_end_func) {
      RC.ru[ru_id]->ifdevice.trx_end_func(&RC.ru[ru_id]->ifdevice);
      RC.ru[ru_id]->ifdevice.trx_end_func = NULL;
    }
  }

  if (assert) {
    abort();
  } else {
    sleep(1); // allow nr-softmodem threads to exit first
    exit(EXIT_SUCCESS);
  }
}

static int create_gNB_tasks(ngran_node_t node_type, configmodule_interface_t *cfg)
{
  uint32_t                        gnb_nb = RC.nb_nr_inst; 
  uint32_t                        gnb_id_start = 0;
  uint32_t                        gnb_id_end = gnb_id_start + gnb_nb;
  // LOG_D(GNB_APP, "%s(gnb_nb:%d)\n", __FUNCTION__, gnb_nb);
  itti_wait_ready(1);
  // LOG_D(PHY, "%s() Task ready initialize structures\n", __FUNCTION__);

  SM_Logs(LOG_INFO, _PHY_, "Task: \033[1;33mInitializing structures....\033[0m\n");

  SM_Logs(LOG_INFO, _PHY_, "\033[1;32mStructure Initialization Successful\033[0m");

#ifdef ENABLE_AERIAL
  // AssertFatal(NFAPI_MODE == NFAPI_MODE_AERIAL,"Can only be run with '--nfapi AERIAL' when compiled with AERIAL support, if you want to run other (n)FAPI modes, please run ./build_oai without -w AERIAL");
#endif

  RCconfig_verify(cfg, node_type);
  // printf("Hi i am after rcss: \n");
  if (RC.nb_nr_macrlc_inst > 0)
    RCconfig_nr_macrlc(cfg);
  // printf("HI i am after macrlc: \n");

  if (RC.nb_nr_L1_inst>0) AssertFatal(l1_north_init_gNB()==0,"could not initialize L1 north interface\n");

  // AssertFatal (gnb_nb <= RC.nb_nr_inst,
  //              "Number of gNB is greater than gNB defined in configuration file (%d/%d)!",
  //              gnb_nb, RC.nb_nr_inst);

  // LOG_D(GNB_APP, "Allocating gNB_RRC_INST\n");
  RC.nrrrc = calloc(1, sizeof(*RC.nrrrc));
  // printf("HI i am after macrlc: 1 \n");
  RC.nrrrc[0] = RCconfig_NRRRC();

  if (!get_softmodem_params()->nsa && !(node_type == ngran_gNB_DU)) {
    // we start pdcp in both cuup (for drb) and cucp (for srb)
    init_pdcp();
  }
  // printf("Hi i am after init pdcp: \n");
  if (get_softmodem_params()->nsa) { //&& !NODE_IS_DU(node_type)
    // LOG_I(X2AP, "X2AP enabled \n");
    __attribute__((unused)) uint32_t x2_register_gnb_pending = gNB_app_register_x2 (gnb_id_start, gnb_id_end);
  }
  //  printf("Hi i am after init pdcp: 1\n");
  /* For the CU case the gNB registration with the AMF might have to take place after the F1 setup, as the PLMN info
     * can originate from the DU. Add check on whether x2ap is enabled to account for ENDC NSA scenario.*/
  if (IS_SA_MODE(get_softmodem_params()) && !NODE_IS_DU(node_type)) {
    /* Try to register each gNB */
    //registered_gnb = 0;
    // printf("Hi i am after init pdcp: 2\n");
    __attribute__((unused)) uint32_t register_gnb_pending = gNB_app_register (gnb_id_start, gnb_id_end);
  }
   
  if (gnb_nb > 0) {
    if(itti_create_task(TASK_SCTP, sctp_eNB_task, NULL) < 0) {
      // LOG_E(SCTP, "Create task for SCTP failed\n");
      return -1;
    }
    //  printf("Hi i am after init pdcp: 3\n");
    if (get_softmodem_params()->nsa) {
      if(itti_create_task(TASK_X2AP, x2ap_task, NULL) < 0) {
        // LOG_E(X2AP, "Create task for X2AP failed\n");
      }
    } else {
      //  printf("Hi i am after init pdcp: 4\n");
      // LOG_I(X2AP, "X2AP is disabled.\n");
       SM_Logs(LOG_INFO, _X2AP_, "Initiating Check for X2AP, Status: \033[0;31mDisabled\033[0m ");
    }
  }
    // printf("Hi i am after init pdcp: 5\n");
  if (IS_SA_MODE(get_softmodem_params()) && !NODE_IS_DU(node_type)) {

    char*             gnb_ipv4_address_for_NGU      = NULL;
    uint32_t          gnb_port_for_NGU              = 0;
    char*             gnb_ipv4_address_for_S1U      = NULL;
    uint32_t          gnb_port_for_S1U              = 0;
    paramdef_t NETParams[]  =  GNBNETPARAMS_DESC;
    char aprefix[MAX_OPTNAME_SIZE*2 + 8];
    sprintf(aprefix,"%s.[%i].%s",GNB_CONFIG_STRING_GNB_LIST,0,GNB_CONFIG_STRING_NETWORK_INTERFACES_CONFIG);
    // printf("Just going into config get: \n");
    //config_get(cfg, NETParams, sizeofArray(NETParams), aprefix);

    if (gnb_nb > 0) {
      if (itti_create_task (TASK_NGAP, ngap_gNB_task, NULL) < 0) {
        // LOG_E(NGAP, "Create task for NGAP failed\n");
        return -1;
      }
    }
  }
  // printf("hi i am going in gnb_nb: \n");
  if (gnb_nb > 0) {
    if (itti_create_task (TASK_GNB_APP, gNB_app_task, NULL) < 0) {
      // LOG_E(GNB_APP, "Create task for gNB APP failed\n");
      return -1;
    }

    if (!NODE_IS_DU(node_type)) {
      if (itti_create_task (TASK_RRC_GNB, rrc_gnb_task, NULL) < 0) {
        // LOG_E(NR_RRC, "Create task for NR RRC gNB failed\n");
        return -1;
      }
    }
    // printf("hi i am going in gnb_nb: 1\n");
    // E1AP initialisation, whether the node is a CU or has integrated CU
    if (node_type == ngran_gNB_CU || node_type == ngran_gNB) {
      // printf("hi i am going in gnb_nb: 2\n");
      MessageDef *msg = RCconfig_NR_CU_E1(NULL);
      instance_t inst = 0;
      createE1inst(UPtype, inst, E1AP_REGISTER_REQ(msg).gnb_id, &E1AP_REGISTER_REQ(msg).net_config, NULL);
      cuup_init_n3(inst);
      RC.nrrrc[gnb_id_start]->e1_inst = inst; // stupid instance !!!*/

      /* send E1 Setup Request to RRC */
      MessageDef *new_msg = itti_alloc_new_message(TASK_GNB_APP, 0, E1AP_SETUP_REQ);
      E1AP_SETUP_REQ(new_msg) = E1AP_REGISTER_REQ(msg).setup_req;
      new_msg->ittiMsgHeader.originInstance = -1; /* meaning, it is local */
      itti_send_msg_to_task(TASK_RRC_GNB, 0 /*unused by callee*/, new_msg);
      itti_free(TASK_UNKNOWN, msg);
    }
    // printf("hi i am going in gnb_nb: 3\n");
    //Use check on x2ap to consider the NSA scenario 
    if((is_x2ap_enabled() || IS_SA_MODE(get_softmodem_params())) && (node_type != ngran_gNB_CUCP)) {
      if (itti_create_task (TASK_GTPV1_U, &gtpv1uTask, NULL) < 0) {
        // LOG_E(GTPU, "Create task for GTPV1U failed\n");
        return -1;
      }
    }
  }
  // printf("I'm about to return: \n");

  return 0;
}

// static void get_options(configmodule_interface_t *cfg)
// {
//   paramdef_t cmdline_params[] = CMDLINE_PARAMS_DESC_GNB ;
//   CONFIG_SETRTFLAG(CONFIG_NOEXITONHELP);
//   //get_common_options(cfg, SOFTMODEM_GNB_BIT);
//   //config_process_cmdline(cfg, cmdline_params, sizeofArray(cmdline_params), NULL);
//   CONFIG_CLEARRTFLAG(CONFIG_NOEXITONHELP);
// }

void wait_RUs(void) {
  // LOG_D(PHY, "Waiting for RUs to be configured ... RC.ru_mask:%02lx\n", RC.ru_mask);
  // wait for all RUs to be configured over fronthaul
  pthread_mutex_lock(&RC.ru_mutex);

  while (RC.ru_mask>0) {
    pthread_cond_wait(&RC.ru_cond,&RC.ru_mutex);
  }

  pthread_mutex_unlock(&RC.ru_mutex);
  //LOG_D(PHY, "RUs configured\n");
}

void wait_gNBs(void) {
  int i;
  int waiting=1;

  while (waiting==1) {
    // LOG_D(GNB_APP, "Waiting for gNB L1 instances to all get configured ... sleeping 50ms (nb_nr_sL1_inst %d)\n", RC.nb_nr_L1_inst);
    SM_Logs(LOG_INFO, _PHY_,"Awaiting full configuration of all Layer 1 instances. Operational status: \033[0;33mIdle \033[0m \n \n");
    usleep(50*1000);
    waiting=0;

    for (i=0; i<RC.nb_nr_L1_inst; i++) {
      if (RC.gNB[i]->configured==0) {
        waiting=1;
        break;
      }
    }
  }

  // LOG_D(GNB_APP, "gNB L1 are configured\n");
  SM_Logs(LOG_INFO, _PHY_,"Layer 1 (Physical Layer) configuration sequence completed. Operational status: \033[0;32mACTIVE\033[0m\n");
}

/*
 * helper function to terminate a certain ITTI task
 */
void terminate_task(task_id_t task_id, module_id_t mod_id) {
  // LOG_I(GNB_APP, "sending TERMINATE_MESSAGE to task %s (%d)\n", itti_get_task_name(task_id), task_id);
  MessageDef *msg;
  msg = itti_alloc_new_message (TASK_ENB_APP, 0, TERMINATE_MESSAGE);
  itti_send_msg_to_task (task_id, ENB_MODULE_ID_TO_INSTANCE(mod_id), msg);
}

//extern void  free_transport(PHY_VARS_gNB *);
extern void  nr_phy_free_RU(RU_t *);

static  void wait_nfapi_init(char *thread_name)
{
  pthread_mutex_lock( &nfapi_sync_mutex );

  while (nfapi_sync_var<0)
    pthread_cond_wait( &nfapi_sync_cond, &nfapi_sync_mutex );

  pthread_mutex_unlock(&nfapi_sync_mutex);
}

void init_pdcp(void) {
  uint32_t pdcp_initmask = IS_SOFTMODEM_NOS1 ? ENB_NAS_USE_TUN_BIT : LINK_ENB_PDCP_TO_GTPV1U_BIT;

  if (!NODE_IS_DU(get_node_type())) {
    nr_pdcp_layer_init();
    nr_pdcp_module_init(pdcp_initmask, 0);
  }
}

#ifdef E2_AGENT
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h" // need to get info from MAC
static void initialize_agent(ngran_node_t node_type, e2_agent_args_t oai_args)
{
  // AssertFatal(oai_args.sm_dir != NULL , "Please, specify the directory where the SMs are located in the config file, i.e., add in config file the next line: e2_agent = {near_ric_ip_addr = \"127.0.0.1\"; sm_dir = \"/usr/local/lib/flexric/\");} ");
  // AssertFatal(oai_args.ip != NULL , "Please, specify the IP address of the nearRT-RIC in the config file, i.e., e2_agent = {near_ric_ip_addr = \"127.0.0.1\"; sm_dir = \"/usr/local/lib/flexric/\"");

  // printf("After RCconfig_NR_E2agent %s %s \n",oai_args.sm_dir, oai_args.ip  );

  fr_args_t args = { .ip = oai_args.ip }; // init_fr_args(0, NULL);
  memcpy(args.libs_dir, oai_args.sm_dir, 128);

  sleep(1);
  const gNB_RRC_INST* rrc = RC.nrrrc[0];
  // assert(rrc != NULL && "rrc cannot be NULL");

  const int mcc = rrc->configuration.mcc[0];
  const int mnc = rrc->configuration.mnc[0];
  const int mnc_digit_len = rrc->configuration.mnc_digit_length[0];
  // const ngran_node_t node_type = rrc->node_type;
  int nb_id = 0;
  int cu_du_id = 0;
  if (node_type == ngran_gNB) {
    nb_id = rrc->node_id;
  } else if (node_type == ngran_gNB_DU) {
    const gNB_MAC_INST* mac = RC.nrmac[0];
    // AssertFatal(mac, "MAC not initialized\n");
    cu_du_id = mac->f1_config.gnb_id;
    nb_id = mac->f1_config.setup_req->gNB_DU_id;
  } else if (node_type == ngran_gNB_CU || node_type == ngran_gNB_CUCP) {
    // agent buggy: the CU has no second ID, it is the CU-UP ID
    // however, that is not a problem her for us, so put the same ID twice
    nb_id = rrc->node_id;
    cu_du_id = rrc->node_id;
  } else {
    // LOG_E(NR_RRC, "not supported ran type detect\n");
  }

  // printf("[E2 NODE]: mcc = %d mnc = %d mnc_digit = %d nb_id = %d \n", mcc, mnc, mnc_digit_len, nb_id);

  // printf("[E2 NODE]: Args %s %s \n", args.ip, args.libs_dir);

  sm_io_ag_ran_t io = init_ran_func_ag();
  init_agent_api(mcc, mnc, mnc_digit_len, nb_id, cu_du_id, node_type, io, &args);
}
#endif

void init_eNB_afterRU(void);
configmodule_interface_t *uniqCfg = NULL;

time_t app_start_time;

// Initialize the uptime tracker
void initialize_uptime_tracker() {
    app_start_time = time(NULL);
}


double calculate_PER(unsigned long total_packets, unsigned long error_packets) {
    if (total_packets == 0) {
        // Avoid division by zero
        // printf("Total packets cannot be zero.\n");
        return -1.0; // Indicating an error
    }
    return ((double)error_packets / (double)total_packets) * 100.0;
}


void print_parameters_table() {
    char buffer[8192]; // Buffer to hold the entire table as a single string
    size_t offset = 0; // Offset to track the current position in the buffer

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, YELLOW"RUNNING STATS:\n"RESET);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|---------------------|--------------|\n");

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| " GREEN "Rx_On_Time          " RESET "| %12ld |\n", aria_ue_stats.counters_stats.Rx_on_time);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| "YELLOW "Rx_Early            " RESET "| %12ld |        ___________________       \n", aria_ue_stats.counters_stats.Rx_early);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| " LIGHT_RED "Rx_Late             " RESET "| %12ld |       |" RED "%12s " RESET "      |      \n", aria_ue_stats.counters_stats.Rx_late, ARIA_NAME);
          offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| " RED "Rx_Corrupt          " RESET "| %12ld |       | VF Addrs :        |      \n", aria_ue_stats.counters_stats.Rx_corrupt);
                          offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Rx_Pkt_Dupl         | %12ld |       |  %-15s  |      \n", aria_ue_stats.counters_stats.Rx_pkt_dupl,AriaGlobalConfigs.vfs_addr[0]);
                          offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Tx-bytesCount       | %12ld |       |  %-15s  |      \n", aria_ue_stats.counters_stats.tx_bytes_counter,AriaGlobalConfigs.vfs_addr[0]);
                          offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| DL Throughput       | %12ld |kbps   |                   |      \n", aria_ue_stats.counters_stats.tx_bytes_per_sec * 8 / 1000L);
                          offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Tx-Packets_PerSec   | %12ld |       |                   |      \n", aria_ue_stats.counters_stats.tx_counter);
                          offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Rx-bytesCount       | %12ld |       |___________________|      \n", aria_ue_stats.counters_stats.rx_bytes_counter);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| UL Throughput       | %12ld |kbps                             \n", aria_ue_stats.counters_stats.rx_bytes_per_sec * 8 / 1000L);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Rx-Packets_PerSec   | %12ld |                                 \n", aria_ue_stats.counters_stats.rx_counter);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| PER                 | %12ld |                                 \n", aria_ue_stats.counters_stats.per_value);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|---------------------+--------------|\n");
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Total_Rx-Packets    | %12ld |\n", aria_ue_stats.counters_stats.Total_msgs_rcvd);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Num-Rx_OnTime       | %12ld |\n", aria_ue_stats.counters_stats.Total_msgs_rcvd - aria_ue_stats.counters_stats.Rx_early - aria_ue_stats.counters_stats.Rx_late);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|---------------------|--------------|\n");

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n"YELLOW "PUSCH / PRACH [PACKETS]\n" RESET "");
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|--------------------------------------------------------------------------\n");

    for (int rxant = 0; rxant < aria_ue_stats.counters_stats.nb_rx / aria_ue_stats.counters_stats.xran_ports; rxant++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Num-Pusch[ANT %d]    | %12ld | Num-Prach[ANT %d]    | %12ld |\n",
                           rxant, aria_ue_stats.counters_stats.rx_pusch_packets[rxant],
                           rxant, aria_ue_stats.counters_stats.rx_prach_packets[rxant]);
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|--------------------------------------------------------------------------\n");
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Num-SRS_Packets     | %12ld |                                    |\n", aria_ue_stats.counters_stats.rx_srs_packets);
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|--------------------------------------------------------------------------\n");
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, YELLOW"\nNum of UEs   :  %ld \n"RESET, aria_ue_stats.num_of_ue);

    if (aria_ue_stats.num_of_ue > 0) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "\n|---------------------|--------------|-------------------------------------\n");
    }

    for (size_t i = 0; i < aria_ue_stats.num_of_ue; i++) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|" YELLOW " UE ID (RNTI)" RESET "%8ld| %-12s  | Signal Quality :                   |\n", aria_ue_stats.stats[i].ue_rnti, aria_ue_stats.stats[i].sync_status);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|---------------------|--------------|                                    |\n");
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| UE Index            | %12d | CQI                 | %12d |\n", aria_ue_stats.stats[i].cu_ue_id,aria_ue_stats.stats[i].cqi);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| PMI                 | %10ld.%ld | Average RSRP        | %12d |\n", aria_ue_stats.stats[i].pmi[0], aria_ue_stats.stats[i].pmi[1], aria_ue_stats.stats[i].avg_rsrp);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|                     |              | Pathloss            | %12d |\n", aria_ue_stats.stats[i].pmi[0], aria_ue_stats.stats[i].pmi[1], aria_ue_stats.stats[i].ph);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|---------------------+----------------------------------------------------\n");
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| " YELLOW "DL " RESET "                                | " YELLOW "UL    " RESET "                             |\n");
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|---------------------+----------------------------------------------------\n");
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| RBs Retx            | %12ld | RBs Retx            | %12ld |\n", aria_ue_stats.stats[i].dl_total_rbs_retx, aria_ue_stats.stats[i].ul_total_rbs_retx);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Total RBs           | %12ld | Total RBs           | %12ld |\n", aria_ue_stats.stats[i].dl_total_rbs, aria_ue_stats.stats[i].ul_total_rbs);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Bytes               | %12ld | Bytes               | %12ld |\n", aria_ue_stats.stats[i].dl_current_bytes, aria_ue_stats.stats[i].ul_current_bytes);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| HARQ Errors         | %12ld | HARQ Errors         | %12ld |\n", aria_ue_stats.stats[i].dlsch_errors, aria_ue_stats.stats[i].ulsch_errors);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| MCS                 | %12ld | MCS                 | %12ld |\n", aria_ue_stats.stats[i].mcs_dl, aria_ue_stats.stats[i].mcs_ul);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| BLER                | %12.5f | BLER                | %12.5f |\n", aria_ue_stats.stats[i].bler_dlsch, aria_ue_stats.stats[i].bler_ulsch);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|---------------------+--------------+-------------------------------------\n");
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "| Total Bytes         | %12ld | Total Bytes         | %12ld |\n", aria_ue_stats.stats[i].mac_tx_bytes, aria_ue_stats.stats[i].mac_rx_bytes);
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "|---------------------+----------------------------------------------------\n");
    }

    // Print the entire buffer at once
    printf("%s", buffer);
}

// Termination flag for the thread
volatile atomic_int terminate_thread = 0;

// Signal handler to set the termination flag
void handle_signal(int signal) {
    if (signal == SIGINT) {  // Handle Ctrl+C (SIGINT)
        terminate_thread = 1;
    }
}

// Thread function to print the table at regular intervals
void *table_printer_thread(void *arg) {
    int interval = *(int *)arg; // Fetch the interval passed as argument
    // int interval = 10;

    while (!terminate_thread) {
        if (aria_ue_stats.counters_stats.stats_available == 1)
        {
          print_parameters_table();
        }
        // printf("\nTESTING parallel thread\n");
        
        sleep(interval); // Sleep for the specified interval
    }

    return NULL;
}




int main( int argc, char **argv ) {
  #if defined ARIA_DU

    printf( "\n\n\n\
      \033[31m █████╗ \033[0m ██████╗  ██╗ █████╗               ██╗     ██╗      ██╗     ██████╗ \n\
      \033[31m██╔══██╗\033[0m ██╔══██╗ ██║██╔══██╗              ██║    ███║      ██║     ╚════██╗\n\
      \033[31m███████║\033[0m ██████╔  ██║███████║    █████     ██║    ╚██║█████ ██║      █████╔╝\n\
      \033[31m██╔══██║\033[0m ██╔══██╗ ██║██╔══██║              ██║     ██║      ██║     ██╔═══╝ \n\
      \033[31m██║  ██║\033[0m ██║  ██║ ██║██║  ██║              ███████╗██║      ███████╗███████╗\n\
      \033[31m╚═╝  ╚═╝\033[0m ╚═╝  ╚═╝ ╚═╝╚═╝  ╚═╝              ╚══════╝╚═╝      ╚══════╝╚══════╝ \n\
    ARIA STARTED\n");
  #elif defined ARIA_CU
    printf( "\n\n\n\
      \033[31m █████╗ \033[0m ██████╗  ██╗ █████╗              ██╗     ██████╗      \n\
      \033[31m██╔══██╗\033[0m ██╔══██╗ ██║██╔══██╗             ██║     ╚════██╗     \n\
      \033[31m███████║\033[0m ██████╔  ██║███████║    █████    ██║      █████╔╝     \n\
      \033[31m██╔══██║\033[0m ██╔══██╗ ██║██╔══██║             ██║      ╚═══██╗     \n\
      \033[31m██║  ██║\033[0m ██║  ██║ ██║██║  ██║             ███████╗██████╔╝     \n\
      \033[31m╚═╝  ╚═╝\033[0m ╚═╝  ╚═╝ ╚═╝╚═╝  ╚═╝             ╚══════╝╚═════╝      \n\
    ARIA STARTED\n");
  #else
    printf( "\n\n\n\
      \033[31m █████╗ \033[0m ██████╗  ██╗ █████╗  \n\
      \033[31m██╔══██╗\033[0m ██╔══██╗ ██║██╔══██╗ \n\
      \033[31m███████║\033[0m ██████╔  ██║███████║ \n\
      \033[31m██╔══██║\033[0m ██╔══██╗ ██║██╔══██║ \n\
      \033[31m██║  ██║\033[0m ██║  ██║ ██║██║  ██║ \n\
      \033[31m╚═╝  ╚═╝\033[0m ╚═╝  ╚═╝ ╚═╝╚═╝  ╚═╝ \n\
    ARIA STARTED\n");
  #endif

  sleep(0.5);


  int ru_id, CC_id = 0;
  start_background_system();

  ///static configuration for NR at the moment
  if ((uniqCfg = load_configmodule(argc, argv, CONFIG_ENABLECMDLINEONLY)) == NULL) {
    // exit_fun("[SOFTMODEM] Error, configuration module init failed\n");
  }

  set_softmodem_sighandler();
#ifdef DEBUG_CONSOLE
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
#endif

  AriaGlobalConfigs.logs_configurations.horizontal_level.PDCP = global_vnf.logConfigs.horizontal_level.PDCP;
  AriaGlobalConfigs.logs_configurations.horizontal_level.SCTP = global_vnf.logConfigs.horizontal_level.SCTP;
  AriaGlobalConfigs.logs_configurations.horizontal_level.F1AP = global_vnf.logConfigs.horizontal_level.F1AP;
  AriaGlobalConfigs.logs_configurations.horizontal_level.RRC = global_vnf.logConfigs.horizontal_level.RRC;
  AriaGlobalConfigs.logs_configurations.horizontal_level.NGAP = global_vnf.logConfigs.horizontal_level.NGAP;
  AriaGlobalConfigs.logs_configurations.horizontal_level.GTPU = global_vnf.logConfigs.horizontal_level.GTPU;
  AriaGlobalConfigs.logs_configurations.horizontal_level.E1AP = global_vnf.logConfigs.horizontal_level.E1AP;
  AriaGlobalConfigs.logs_configurations.vertical_level = global_vnf.logConfigs.vertical_level;
  AriaGlobalConfigs.logs_configurations.horizontal_level.RLC = global_vnf.logConfigs.horizontal_level.RLC;
  AriaGlobalConfigs.logs_configurations.horizontal_level.CU_APP = global_vnf.logConfigs.horizontal_level.CU_APP;
  AriaGlobalConfigs.logs_configurations.horizontal_level.MAC = global_vnf.logConfigs.horizontal_level.MAC;
  AriaGlobalConfigs.logs_configurations.horizontal_level.PHY = global_vnf.logConfigs.horizontal_level.PHY;
  AriaGlobalConfigs.logs_configurations.horizontal_level.ARIA = global_vnf.logConfigs.horizontal_level.ARIA;
  AriaGlobalConfigs.logs_configurations.horizontal_level.X2AP = global_vnf.logConfigs.horizontal_level.X2AP;
  AriaGlobalConfigs.logs_configurations.horizontal_level.P5_INFO_LEVEL = global_vnf.logConfigs.horizontal_level.P5_INFO_LEVEL;
  AriaGlobalConfigs.logs_configurations.horizontal_level.P7_INFO_LEVEL = global_vnf.logConfigs.horizontal_level.P7_INFO_LEVEL;
  AriaGlobalConfigs.logs_configurations.horizontal_level.SDAP = global_vnf.logConfigs.horizontal_level.SDAP;
  AriaGlobalConfigs.logs_configurations.horizontal_level.GNB_APP = global_vnf.logConfigs.horizontal_level.GNB_APP;
  AriaGlobalConfigs.logs_configurations.horizontal_level.DTLS = global_vnf.logConfigs.horizontal_level.DTLS;
  AriaGlobalConfigs.logs_configurations.print_datetime = global_vnf.logConfigs.print_datetime;
  AriaGlobalConfigs.logs_configurations.print_configurations = false;


  AriaGlobalConfigs.logs_file_configurations.horizontal_level.PDCP = global_vnf.logConfigs.horizontal_level.PDCP;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.SCTP = global_vnf.logConfigs.horizontal_level.SCTP;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.F1AP = global_vnf.logConfigs.horizontal_level.F1AP;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.RRC = global_vnf.logConfigs.horizontal_level.RRC;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.NGAP = global_vnf.logConfigs.horizontal_level.NGAP;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.GTPU = global_vnf.logConfigs.horizontal_level.GTPU;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.E1AP = global_vnf.logConfigs.horizontal_level.E1AP;
  AriaGlobalConfigs.logs_file_configurations.vertical_level = global_vnf.logFileConfigs.vertical_level;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.RLC = global_vnf.logConfigs.horizontal_level.RLC;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.CU_APP = global_vnf.logConfigs.horizontal_level.CU_APP;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.MAC = global_vnf.logConfigs.horizontal_level.MAC;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.PHY = global_vnf.logConfigs.horizontal_level.PHY;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.ARIA = global_vnf.logConfigs.horizontal_level.ARIA;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.X2AP = global_vnf.logConfigs.horizontal_level.X2AP;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.P5_INFO_LEVEL = global_vnf.logConfigs.horizontal_level.P5_INFO_LEVEL;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.P7_INFO_LEVEL = global_vnf.logConfigs.horizontal_level.P7_INFO_LEVEL;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.SDAP = global_vnf.logConfigs.horizontal_level.SDAP;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.GNB_APP = global_vnf.logConfigs.horizontal_level.GNB_APP;
  AriaGlobalConfigs.logs_file_configurations.horizontal_level.DTLS = global_vnf.logConfigs.horizontal_level.DTLS;

  AriaGlobalConfigs.logs_file_configurations.print_configurations = false;
  AriaGlobalConfigs.logs_file_configurations.print_datetime = global_vnf.logConfigs.print_datetime;
  AriaGlobalConfigs.logs_file_configurations.generate_log_file = global_vnf.GenerateLogFile;
  AriaGlobalConfigs.l1_stats_update_interval = global_vnf.L1_stats_update_interval;

  aria_ue_stats.counters_stats.stats_available = 0;

  strncpy(AriaGlobalConfigs.vfs_addr[0], "0000:18:01.0", 10);
  strncpy(AriaGlobalConfigs.vfs_addr[1], "0000:18:01.1", 10);
  AriaGlobalConfigs.vfs_addr[0][9] = '\0';
  AriaGlobalConfigs.vfs_addr[1][9] = '\0';



  aria_ue_stats.num_of_ue = 0; // Initially no UEs are tracked
  memset(aria_ue_stats.stats, 0, sizeof(aria_ue_stats.stats)); // Set all stats to zero

  const ngran_node_t node_type = get_node_type();
  mode = normal_txrx;
  memset(tx_max_power,0,sizeof(int)*MAX_NUM_CCs);
  // printf("before calling logInit\n");
  logInit();
  // printf("after calling logInit\n");
  lock_memory_to_ram();
  // printf("before calling get_options\n");
  // get_options(uniqCfg);
  // printf("after calling get_options\n");


  EPC_MODE_ENABLED = !IS_SOFTMODEM_NOS1;

  if (CONFIG_ISFLAGSET(CONFIG_ABORT) ) {
    fprintf(stderr,"Getting configuration failed\n");
    exit(-1);
  }

  if (!has_cap_sys_nice())
  softmodem_verify_mode(get_softmodem_params());

// printf("before calling T_Config_Init\n");
#if T_TRACER
  T_Config_Init();
#endif

  set_taus_seed (0);

  cpuf=get_cpu_freq_GHz();
  itti_init(TASK_MAX, tasks_info);
  init_opt();

  char *pckg = strdup(OAI_PACKAGE_VERSION);
  if (!(CONFIG_ISFLAGSET(CONFIG_ABORT)))
    NRRCConfig();
  if (RC.nb_nr_L1_inst > 0) {
    init_gNB();

    RCconfig_NR_L1();

    if(NFAPI_MODE != NFAPI_MODE_PNF && NFAPI_MODE != NFAPI_MODE_AERIAL)
      RCconfig_nr_prs();
  }

  // printf("just before if (NFAPI_MODE != NFAPI_MODE_PNF) {\n");

  if (NFAPI_MODE != NFAPI_MODE_PNF) {
    int ret = create_gNB_tasks(node_type, uniqCfg);
  }

  pthread_cond_init(&sync_cond,NULL);
  pthread_mutex_init(&sync_mutex, NULL);
  usleep(1000);

  if (NFAPI_MODE && NFAPI_MODE != NFAPI_MODE_AERIAL) {
    pthread_cond_init(&sync_cond,NULL);
    pthread_mutex_init(&sync_mutex, NULL);
  }

  // start the main threads
  number_of_cards = 1;

  if (NODE_IS_DU(node_type) || NODE_IS_MONOLITHIC(node_type)){

  pthread_t l1_stats_printer_thread;

  // signal(SIGINT, handle_signal);
  int print_interval = AriaGlobalConfigs.l1_stats_update_interval;
  if (pthread_create(&l1_stats_printer_thread, NULL, table_printer_thread, &print_interval) != 0) {
      return EXIT_FAILURE;
  }

  }

  wait_gNBs();
  int sl_ahead = NFAPI_MODE == NFAPI_MODE_AERIAL ? 0 : 6;
  if (RC.nb_RU >0) {
    init_NR_RU(uniqCfg, get_softmodem_params()->rf_config_file);

    for (ru_id=0; ru_id<RC.nb_RU; ru_id++) {
      RC.ru[ru_id]->rf_map.card=0;
      RC.ru[ru_id]->rf_map.chain=CC_id+chain_offset;
      if (ru_id==0) sl_ahead = RC.ru[ru_id]->sl_ahead;	
      else{}
      //  AssertFatal(RC.ru[ru_id]->sl_ahead != RC.ru[0]->sl_ahead,"RU %d has different sl_ahead %d than RU 0 %d\n",ru_id,RC.ru[ru_id]->sl_ahead,RC.ru[0]->sl_ahead);
    }
    
  }

  config_sync_var=0;


#ifdef E2_AGENT

//////////////////////////////////
//////////////////////////////////
//// Init the E2 Agent

  // OAI Wrapper 
  e2_agent_args_t oai_args = RCconfig_NR_E2agent();

  if (oai_args.enabled) {
    initialize_agent(node_type, oai_args);
  }

#endif // E2_AGENT


  if (NFAPI_MODE==NFAPI_MODE_PNF) {
    wait_nfapi_init("main?");
  }

  // wait for F1 Setup Response before starting L1 for real
  if (NODE_IS_DU(node_type) || NODE_IS_MONOLITHIC(node_type))
    wait_f1_setup_response();

  if (RC.nb_RU > 0)
    start_NR_RU();
#ifdef ENABLE_AERIAL
  gNB_MAC_INST *nrmac = RC.nrmac[0];
  nvIPC_Init(nrmac->nvipc_params_s);
#endif
  if (RC.nb_nr_L1_inst > 0) {
    wait_RUs();
    // once all RUs are ready initialize the rest of the gNBs ((dependence on final RU parameters after configuration)

    for (int idx=0;idx<RC.nb_nr_L1_inst;idx++) RC.gNB[idx]->if_inst->sl_ahead = sl_ahead;
    if (IS_SOFTMODEM_DOSCOPE || IS_SOFTMODEM_IMSCOPE_ENABLED) {
      sleep(1);
      scopeParms_t p;
      p.argc = &argc;
      p.argv = argv;
      p.gNB = RC.gNB[0];
      p.ru = RC.ru[0];
      if (IS_SOFTMODEM_DOSCOPE) {
        load_softscope("nr", &p);
      }
      if (IS_SOFTMODEM_IMSCOPE_ENABLED) {
        load_softscope("im", &p);
      }
    }

    if (NFAPI_MODE != NFAPI_MODE_PNF && NFAPI_MODE != NFAPI_MODE_VNF && NFAPI_MODE != NFAPI_MODE_AERIAL) {
      init_eNB_afterRU();
    }

    // connect the TX/RX buffers
    pthread_mutex_lock(&sync_mutex);
    sync_var=0;
    pthread_cond_broadcast(&sync_cond);
    pthread_mutex_unlock(&sync_mutex);
  }

  // wait for end of program
  // printf("TYPE <CTRL-C> TO TERMINATE\n");
  itti_wait_tasks_end(NULL);
  // printf("Returned from ITTI signal handler\n");
  SM_Logs(LOG_INFO,_F1AP_,"Current layer initialised and system status is \033[0;32mHealthy\033[0m. Detecting the subsequent layer and initialising connection with the subsequent layer.\n\n");
  oai_exit=1;

  // cleanup
  if (RC.nb_nr_L1_inst > 0)
    stop_gNB(RC.nb_nr_L1_inst);

  if (RC.nb_RU > 0)
    stop_RU(RC.nb_RU);

  /* release memory used by the RU/gNB threads (incomplete), after all
   * threads have been stopped (they partially use the same memory) */
  for (int inst = 0; inst < RC.nb_RU; inst++) {
    nr_phy_free_RU(RC.ru[inst]);
  }

  for (int inst = 0; inst < RC.nb_nr_L1_inst; inst++) {
    phy_free_nr_gNB(RC.gNB[inst]);
  }

  pthread_cond_destroy(&sync_cond);
  pthread_mutex_destroy(&sync_mutex);
  pthread_cond_destroy(&nfapi_sync_cond);
  pthread_mutex_destroy(&nfapi_sync_mutex);

  // *** Handle per CC_id openair0

  for(ru_id = 0; ru_id < RC.nb_RU; ru_id++) {
    if (RC.ru[ru_id]->ifdevice.trx_end_func)
      RC.ru[ru_id]->ifdevice.trx_end_func(&RC.ru[ru_id]->ifdevice);
  }

  free(pckg);
  logClean();
  // printf("Bye.\n");
if (AriaGlobalConfigs.logs_file_configurations.generate_log_file == true){
  if (NODE_IS_DU(node_type)){
      save_logs_to_file("../../../logs/aria_l1_l2_logs_file.txt");
  }
  else if(NODE_IS_CU(node_type)){
      save_logs_to_file("../../../logs/aria_l3_logs_file.txt");
  }
  else if(NODE_IS_MONOLITHIC(node_type)){
      save_logs_to_file("../../logs/aria_logs_file.txt");
  }

  SM_Logs(LOG_INFO, _ARIA_, "THANK YOU FOR CHOOSING ARIA ");
  return 0;
}
}

////////////////////
    // int SCTP = AriaGlobalConfigs.logs_configurations.horizontal_level.SCTP;

    // int F1AP = AriaGlobalConfigs.logs_configurations.horizontal_level.F1AP;

    // int RRC = AriaGlobalConfigs.logs_configurations.horizontal_level.RRC;

    // int NGAP = AriaGlobalConfigs.logs_configurations.horizontal_level.NGAP;

    // int RLC = AriaGlobalConfigs.logs_configurations.horizontal_level.RLC;

    // int GTPU = AriaGlobalConfigs.logs_configurations.horizontal_level.GTPU;

    // int PDCP = AriaGlobalConfigs.logs_configurations.horizontal_level.PDCP;

    // int MAC = AriaGlobalConfigs.logs_configurations.horizontal_level.MAC;

    // int PHY = AriaGlobalConfigs.logs_configurations.horizontal_level.PHY;

    // int ARIA = AriaGlobalConfigs.logs_configurations.horizontal_level.ARIA;

    // int E1AP = AriaGlobalConfigs.logs_configurations.horizontal_level.E1AP;

    // int X2AP = AriaGlobalConfigs.logs_configurations.horizontal_level.X2AP;

    // int GNB_APP = AriaGlobalConfigs.logs_configurations.horizontal_level.GNB_APP;

    // int EXTRA = AriaGlobalConfigs.logs_configurations.horizontal_level.EXTRA;

    // int P5_INFO_LEVEL= AriaGlobalConfigs.logs_configurations.horizontal_level.P5_INFO_LEVEL;

    // int P7_INFO_LEVEL= AriaGlobalConfigs.logs_configurations.horizontal_level.P7_INFO_LEVEL; 
