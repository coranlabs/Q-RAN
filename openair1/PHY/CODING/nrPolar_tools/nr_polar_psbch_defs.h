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

/*! \file /PHY/CODING/nrPolar_tools/nr_polar_psbch_defs.h
 \brief Polar definitions required for Sidelink PSBCH
 \author
 \date
 \version
 \company: Fraunhofer
 \email:
 \note
 \warning
*/

#ifndef __NR_POLAR_PSBCH_DEFS__H__
#define __NR_POLAR_PSBCH_DEFS__H__

// PSBCH related polar parameters.

// PSBCH symbols sent in 11RBS, 9 symbols. 11*9*(12-3(for DMRS))*2bits = 1782 bits
#define SL_NR_POLAR_PSBCH_E_NORMAL_CP 1782
// PSBCH symbols sent in 11RBS, 7 symbols. 11*7*(12-3(for DMRS))*2bits = 1386 bits
#define SL_NR_POLAR_PSBCH_E_EXT_CP 1386
// SL_NR_POLAR_PSBCH_E_NORMAL_CP/32
#define SL_NR_POLAR_PSBCH_E_DWORD 56

#define SL_NR_POLAR_PSBCH_MESSAGE_TYPE (NR_POLAR_UCI_PUCCH_MESSAGE_TYPE + 1)
#define SL_NR_POLAR_PSBCH_PAYLOAD_BITS 32
#define SL_NR_POLAR_PSBCH_AGGREGATION_LEVEL 0
#define SL_NR_POLAR_PSBCH_N_MAX 9
#define SL_NR_POLAR_PSBCH_I_IL 1
#define SL_NR_POLAR_PSBCH_I_SEG 0
#define SL_NR_POLAR_PSBCH_N_PC 0
#define SL_NR_POLAR_PSBCH_N_PC_WM 0
#define SL_NR_POLAR_PSBCH_I_BIL 0
#define SL_NR_POLAR_PSBCH_CRC_PARITY_BITS 24
#define SL_NR_POLAR_PSBCH_CRC_ERROR_CORRECTION_BITS 3

#endif
