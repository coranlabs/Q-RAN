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

/*! \file nr_nas_msg.h
 * \brief simulator for nr nas message
 * \author Yoshio INOUE, Masayuki HARADA
 * \email yoshio.inoue@fujitsu.com,masayuki.harada@fujitsu.com
 * \date 2020
 * \version 0.1
 */

#ifndef __NR_NAS_MSG_SIM_H__
#define __NR_NAS_MSG_SIM_H__

#include "RegistrationRequest.h"
#include "FGSIdentityResponse.h"
#include "FGSAuthenticationResponse.h"
#include "FGSNASSecurityModeComplete.h"
#include "FGSDeregistrationRequestUEOriginating.h"
#include "RegistrationComplete.h"
#include "as_message.h"
#include "FGSUplinkNasTransport.h"
#include <openair3/UICC/usim_interface.h>
#include "secu_defs.h"

#define INITIAL_REGISTRATION 0b001

#define PLAIN_5GS_NAS_MESSAGE_HEADER_LENGTH 3
#define SECURITY_PROTECTED_5GS_NAS_MESSAGE_HEADER_LENGTH 7
#define PAYLOAD_CONTAINER_LENGTH_MIN 3
#define PAYLOAD_CONTAINER_LENGTH_MAX 65537

/* List of allowed NSSAI from NAS messaging. */
typedef struct {
  int sst;
  int hplmn_sst;
  int sd;
  int hplmn_sd;
} nr_nas_msg_snssai_t;

/* Security Key for SA UE */
typedef struct {
  uint8_t kausf[32];
  uint8_t kseaf[32];
  uint8_t kamf[32];
  uint8_t knas_int[16];
  uint8_t knas_enc[16];
  uint8_t res[16];
  uint8_t rand[16];
  uint8_t kgnb[32];
  uint32_t nas_count_ul;
  uint32_t nas_count_dl;
} ue_sa_security_key_t;

typedef struct {
  uicc_t *uicc;
  ue_sa_security_key_t security;
  stream_security_container_t *security_container;
  Guti5GSMobileIdentity_t *guti;
  bool termination_procedure;
  uint8_t *registration_request_buf;
  uint32_t registration_request_len;
  instance_t UE_id;
} nr_ue_nas_t;

typedef enum fgs_protocol_discriminator_e {
  /* Protocol discriminator identifier for 5GS Mobility Management */
  FGS_MOBILITY_MANAGEMENT_MESSAGE = 0x7E,

  /* Protocol discriminator identifier for 5GS Session Management */
  FGS_SESSION_MANAGEMENT_MESSAGE = 0x2E,
} fgs_protocol_discriminator_t;

typedef struct {
  uint8_t ex_protocol_discriminator;
  uint8_t security_header_type;
  uint8_t message_type;
} mm_msg_header_t;

/* Structure of security protected header */
typedef struct {
  fgs_protocol_discriminator_t protocol_discriminator;
  uint8_t security_header_type;
  uint32_t message_authentication_code;
  uint8_t sequence_number;
} fgs_nas_message_security_header_t;

typedef union {
  mm_msg_header_t header;
  registration_request_msg registration_request;
  fgs_identiy_response_msg fgs_identity_response;
  fgs_authentication_response_msg fgs_auth_response;
  fgs_deregistration_request_ue_originating_msg fgs_deregistration_request_ue_originating;
  fgs_security_mode_complete_msg fgs_security_mode_complete;
  registration_complete_msg registration_complete;
  fgs_uplink_nas_transport_msg uplink_nas_transport;
} MM_msg;

typedef struct {
  MM_msg mm_msg; /* 5GS Mobility Management messages */
} fgs_nas_message_plain_t;

typedef struct {
  fgs_nas_message_security_header_t header;
  fgs_nas_message_plain_t plain;
} fgs_nas_message_security_protected_t;

typedef union {
  fgs_nas_message_security_header_t header;
  fgs_nas_message_security_protected_t security_protected;
  fgs_nas_message_plain_t plain;
} fgs_nas_message_t;

typedef struct {
  union {
    mm_msg_header_t plain_nas_msg_header;
    struct security_protected_nas_msg_header_s {
      uint8_t ex_protocol_discriminator;
      uint8_t security_header_type;
      uint16_t message_authentication_code1;
      uint16_t message_authentication_code2;
      uint8_t sequence_number;
    } security_protected_nas_msg_header_t;
  } choice;
} nas_msg_header_t;

typedef struct {
  uint8_t ex_protocol_discriminator;
  uint8_t pdu_session_id;
  uint8_t PTI;
  uint8_t message_type;
} fgs_sm_nas_msg_header_t;

typedef struct {
  mm_msg_header_t plain_nas_msg_header;
  uint8_t payload_container_type;
  uint16_t payload_container_length;
  fgs_sm_nas_msg_header_t sm_nas_msg_header;
} dl_nas_transport_t;

nr_ue_nas_t *get_ue_nas_info(module_id_t module_id);
void generateRegistrationRequest(as_nas_info_t *initialNasMsg, nr_ue_nas_t *nas);
void *nas_nrue_task(void *args_p);
void *nas_nrue(void *args_p);
void nas_init_nrue(int num_ues);

#endif /* __NR_NAS_MSG_SIM_H__*/
