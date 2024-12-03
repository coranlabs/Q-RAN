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

#include <string.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include "common/utils/LOG/log.h"
#include "common/utils/nr/nr_common.h"
#include "assertions.h"
#include "common/utils/ocp_itti/intertask_interface.h"
#include "openair2/GNB_APP/gnb_paramdef.h"
#include "openair3/ocp-gtpu/gtp_itf.h"

static void get_NGU_S1U_addr(char **addr, uint16_t *port)
{
  int num_gnbs = 0;
  char *gnb_ipv4_address_for_NGU = NULL;
  uint32_t gnb_port_for_NGU = 0;
  char *gnb_ipv4_address_for_S1U = NULL;
  uint32_t gnb_port_for_S1U = 0;
  char gtpupath[MAX_OPTNAME_SIZE * 2 + 8];

  paramdef_t GNBSParams[] = GNBSPARAMS_DESC;
  paramdef_t NETParams[] = GNBNETPARAMS_DESC;
  //printf("Hi i am in get_NGU_S1U_addr: \n");
  
    SM_Logs(LOG_INFO, _GTPU_, "Initiating GTP-U configuration: Setting up GTP-U interface parameters for tunneling protocol.");


  /* get number of active eNodeBs */
  //config_get(config_get_if(), GNBSParams, sizeofArray(GNBSParams), NULL);
  num_gnbs = GlobC_ARIAConfs.numGNBparmlist;
  //printf("Hi i am in get_NGU_S1U_addr: 1\n");
  if (num_gnbs <= 0) {
    // SM_Logs(LOG_ERROR, _E1AP_, 
    //         "Configuration parsing failed: No active gNodeBs found in %s.\n", 
    //         GNB_CONFIG_STRING_ACTIVE_GNBS);
    return; // Replace with appropriate error handling logic if needed
}
//printf("Hi i am in get_NGU_S1U_addr: 2\n");


  //sprintf(gtpupath, "%s.[%i].%s", GNB_CONFIG_STRING_GNB_LIST, 0, GNB_CONFIG_STRING_NETWORK_INTERFACES_CONFIG);
  //config_get(config_get_if(), NETParams, sizeofArray(NETParams), gtpupath);
  char *address;
  if (GlobC_ARIAConfs.y_gNBs->n_net_if.NgAmfDevIPv4 != NULL) {
    // LOG_I(GTPU, "SA mode \n");
    //     SM_Logs(LOG_INFO, _GTPU_, "Currently Running in the mode: \033[0;32mStand Alone\033[0m");

    //AssertFatal(gnb_ipv4_address_for_NGU != NULL, "NG-U IPv4 address is NULL: could not read IPv4 address\n");
    address = strdup(GlobC_ARIAConfs.y_gNBs->n_net_if.NgAmfDevIPv4);
    *port = 2152;
  } else {
    //LOG_I(GTPU, "NSA mode \n");
    // AssertFatal(gnb_ipv4_address_for_S1U != NULL, "S1U IPv4 address is NULL: could not read IPv4 address\n");
    address = strdup(GlobC_ARIAConfs.y_gNBs->n_net_if.NgAmfDevIPv4);
    *port = 2152;
  }
  if (strchr(address, '/'))
    *strchr(address, '/') = 0;
  *addr = address;
  return;
}

MessageDef *RCconfig_NR_CU_E1(const E1_t *entity)
{
  //printf("Hi i am in RCconfig_NR_CU_E1 \n");
  MessageDef *msgConfig = itti_alloc_new_message(TASK_GNB_APP, 0, E1AP_REGISTER_REQ);
  if (!msgConfig)
    return NULL;

  paramdef_t GNBSParams[] = GNBSPARAMS_DESC;
  paramdef_t GNBParams[] = GNBPARAMS_DESC;
  paramlist_def_t GNBParamList = {GNB_CONFIG_STRING_GNB_LIST, NULL, 0};
  //config_get(config_get_if(), GNBSParams, sizeofArray(GNBSParams), NULL);
  char aprefix[MAX_OPTNAME_SIZE * 2 + 8];
  sprintf(aprefix, "%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0);
  int num_gnbs = GlobC_ARIAConfs.numGNBparmlist;
  //AssertFatal(num_gnbs == 1, "Support only one gNB per process\n");
  //config_getlist(config_get_if(), &GNBParamList, GNBParams, sizeofArray(GNBParams), NULL);
  paramdef_t *gnbParms = GNBParamList.paramarray[0];
  e1ap_setup_req_t *e1Setup = &E1AP_REGISTER_REQ(msgConfig).setup_req;

  if (num_gnbs > 0) {
    msgConfig->ittiMsgHeader.destinationInstance = 0;
    if (GlobC_ARIAConfs.FunctionalNodes)
      e1Setup->gNB_cu_up_name = GlobC_ARIAConfs.FunctionalNodes;

    paramdef_t PLMNParams[] = GNBPLMNPARAMS_DESC;
    paramlist_def_t PLMNParamList = {GNB_CONFIG_STRING_PLMN_LIST, NULL, 0};
    /* map parameter checking array instances to parameter definition array instances */
    checkedparam_t config_check_PLMNParams[] = PLMNPARAMS_CHECK;

    for (int I = 0; I < sizeofArray(PLMNParams); ++I)
      PLMNParams[I].chkPptr = &(config_check_PLMNParams[I]);

   // config_getlist(config_get_if(), &PLMNParamList, PLMNParams, sizeofArray(PLMNParams), aprefix);
    int numPLMNs = GlobC_ARIAConfs.numplmnlist;
    e1Setup->supported_plmns = numPLMNs;

    for (int I = 0; I < numPLMNs; I++) {
      e1Setup->plmn[I].id.mcc = GlobC_ARIAConfs.y_gNBs->p_plmn.MCC;
      e1Setup->plmn[I].id.mnc = GlobC_ARIAConfs.y_gNBs->p_plmn.MNC;
      e1Setup->plmn[I].id.mnc_digit_length = GlobC_ARIAConfs.y_gNBs->p_plmn.MNC_len ;
     // printf("PLMN[%d]: MCC = %u, MNC = %u, MNC Digit Length = %u\n", 
      //  I, 
      //  e1Setup->plmn[I].id.mcc, 
      //  e1Setup->plmn[I].id.mnc, 
      //  e1Setup->plmn[I].id.mnc_digit_length);


      char snssaistr[MAX_OPTNAME_SIZE*2 + 8];
      sprintf(snssaistr, "%s.[%i].%s.[%i]", GNB_CONFIG_STRING_GNB_LIST, 0, GNB_CONFIG_STRING_PLMN_LIST, I);
      paramlist_def_t SNSSAIParamList = {GNB_CONFIG_STRING_SNSSAI_LIST, NULL, 0};
      paramdef_t SNSSAIParams[] = GNBSNSSAIPARAMS_DESC;
      //config_getlist(config_get_if(), &SNSSAIParamList, SNSSAIParams, sizeof(SNSSAIParams) / sizeof(paramdef_t), snssaistr);
      e1Setup->plmn[I].supported_slices = GlobC_ARIAConfs.numSNSSAIlist;
      e1Setup->plmn[I].slice = calloc(GlobC_ARIAConfs.numSNSSAIlist, sizeof(*e1Setup->plmn[I].slice));
      //AssertFatal(e1Setup->plmn[I].slice != NULL, "out of memory\n");
      for (int s = 0; s < GlobC_ARIAConfs.numSNSSAIlist; ++s) {
        e1ap_nssai_t *slice = &e1Setup->plmn[I].slice[s];
        slice->sst = GlobC_ARIAConfs.y_gNBs->p_plmn.s_snssai.SvcTypeID;
        slice->sd = GlobC_ARIAConfs.y_gNBs->p_plmn.s_snssai.SliceDiff;
      }
    }
     //printf("Hi i am in RCconfig_NR_CU_E1 1\n");

    e1ap_net_config_t *e1ap_nc = &E1AP_REGISTER_REQ(msgConfig).net_config;
    e1ap_nc->remotePortF1U = GlobC_ARIAConfs.y_gNBs->ExtSdtPort;
    e1ap_nc->localAddressF1U = strdup(GlobC_ARIAConfs.y_gNBs->HostServIP);
    e1ap_nc->localPortF1U = GlobC_ARIAConfs.y_gNBs->HostSCrlPort;
    get_NGU_S1U_addr(&e1ap_nc->localAddressN3, &e1ap_nc->localPortN3);
    e1ap_nc->remotePortN3 = e1ap_nc->localPortN3 ;
    // printf("Hi i am in RCconfig_NR_CU_E1 2\n");
    //AssertFatal(config_isparamset(gnbParms, GNB_GNB_ID_IDX), "%s is not defined in configuration file\n", GNB_CONFIG_STRING_GNB_ID);
    uint32_t gnb_id = GlobC_ARIAConfs.y_gNBs->NodeKey;
    E1AP_REGISTER_REQ(msgConfig).gnb_id = gnb_id;

    if (entity != NULL) {
       //printf("Hi i am in RCconfig_NR_CU_E1 3\n");
      paramlist_def_t GNBE1ParamList = {GNB_CONFIG_STRING_E1_PARAMETERS, NULL, 0};
      paramdef_t GNBE1Params[] = GNBE1PARAMS_DESC;
      //config_getlist(config_get_if(), &GNBE1ParamList, GNBE1Params, sizeofArray(GNBE1Params), aprefix);
      paramdef_t *e1Parms = GNBE1ParamList.paramarray[0];
      strcpy(e1ap_nc->CUCP_e1_ip_address.ipv4_address, GlobC_ARIAConfs.y_defvalues.cucp_ipv4);
      e1ap_nc->CUCP_e1_ip_address.ipv4 = 1;
      strcpy(e1ap_nc->CUUP_e1_ip_address.ipv4_address, GlobC_ARIAConfs.y_defvalues.cuup_ipv4);
      e1ap_nc->CUUP_e1_ip_address.ipv4 = 1;
      // printf("Hi i am in RCconfig_NR_CU_E1 4\n");
      if (*entity == CPtype) {
        // CP needs gNB_ID (although not used, but other parts check it as
        // well), and gNB-CU-UP ID should NOT be present as it comes through E1!
       // AssertFatal(!config_isparamset(gnbParms, GNB_GNB_CU_UP_ID_IDX), "%s must not be defined in configuration file\n", GNB_CONFIG_STRING_GNB_CU_UP_ID);
      } else { // UPtype
        //AssertFatal(config_isparamset(gnbParms, GNB_GNB_CU_UP_ID_IDX), "%s is not be defined in configuration file\n", GNB_CONFIG_STRING_GNB_CU_UP_ID);
        e1Setup->gNB_cu_up_id = GlobC_ARIAConfs.y_defvalues.GNBcuupID;
      }
       //printf("Hi i am in RCconfig_NR_CU_E1 5\n");
    } else {
      // integrated CU-CP/UP. We don't care about the gNB-CU-UP ID that much,
      // but if it's there, check it's the same as gNB ID
      // printf("Hi i am in RCconfig_NR_CU_E1 6\n");
      uint64_t *gnb_cu_up_id = GlobC_ARIAConfs.y_defvalues.GNBcuupID;
      // AssertFatal(!config_isparamset(gnbParms, GNB_GNB_CU_UP_ID_IDX) || *gnb_cu_up_id == gnb_id,
      //             "%s is different of %s: they need to match or remove %s from config\n",
      //             GNB_CONFIG_STRING_GNB_CU_UP_ID,
      //             GNB_CONFIG_STRING_GNB_ID,
      //             GNB_CONFIG_STRING_GNB_CU_UP_ID);
      e1Setup->gNB_cu_up_id = gnb_id;
    }
  }
   //printf("Hi i am in RCconfig_NR_CU_E1 7\n");
  return msgConfig;
}
