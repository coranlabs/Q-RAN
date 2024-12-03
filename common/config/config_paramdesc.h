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

/*! \file common/config/config_paramdesc.h
 * \brief configuration module, include file describing parameters, common to all implementations
 * \author Francois TABURET
 * \date 2017
 * \version 0.1
 * \company NOKIA BellLabs France
 * \email: francois.taburet@nokia-bell-labs.com
 * \note
 * \warning
 */
#include <stdint.h>
#include "common/utils/LOG/log.h"

#ifndef INCLUDE_CONFIG_PARAMDESC_H
#define INCLUDE_CONFIG_PARAMDESC_H

#define MAX_OPTNAME_SIZE 64
#define CONFIG_MAXOPTLENGTH 512 /* max full option length, full option name exemple: (prefix1.[<index>].prefix2.optname */

#define A_SET_OF(type)                   \
    struct {                             \
        type **array;                    \
        int count; /* Meaningful size */ \
        int size;  /* Allocated size */  \
        void (*free)(type *);    \
    }

#define	A_SEQUENCE_OF(type)	A_SET_OF(type)


/* parameter flags definitions */
/*   Flags to be used by calling modules in their parameters definitions to modify  config module behavior*/
#define PARAMFLAG_MANDATORY               (1 << 0)         // parameter must be explicitely set, default value ignored
#define PARAMFLAG_DISABLECMDLINE          (1 << 1)         // parameter cannot bet set from comand line
#define PARAMFLAG_DONOTREAD               (1 << 2)         // parameter must be ignored in get function
#define PARAMFLAG_NOFREE                  (1 << 3)         // don't free parameter in end function
#define PARAMFLAG_BOOL                    (1 << 4)         // integer param can be 0 or 1
#define PARAMFLAG_CMDLINE_NOPREFIXENABLED (1 << 5)         // on the command line, allow a parameter to be specified without the prefix
#define PARAMFLAG_CMDLINEONLY (1 << 6) // this parameter cannot be specified in config file

/*   Flags used by config modules to return info to calling modules and/or to  for internal usage*/
#define PARAMFLAG_MALLOCINCONFIG          (1 << 15)        // parameter allocated in config module
#define PARAMFLAG_PARAMSET                (1 << 16)        // parameter has been explicitely set in get functions
#define PARAMFLAG_PARAMSETDEF             (1 << 17)        // parameter has been set to default value in get functions

// #include "asn_SEQUENCE_OF.h"
// #include "asn_SET_OF.h"

// #include <asn_SEQUENCE_OF.h>
// #include <asn_SET_OF.h>

//#include "openair1/PHY/impl_defs_lte_NB_IoT.h"

/* checkedparam_t is possibly used in paramdef_t for specific parameter value validation */
#define CONFIG_MAX_NUMCHECKVAL            20
typedef struct paramdef paramdef_t;
typedef struct configmodule_interface configmodule_interface_t;
typedef union checkedparam {
  struct  {
    int (*f1)(configmodule_interface_t *cfg, paramdef_t *param); /* check an integer against a list of authorized values */
    int okintval[CONFIG_MAX_NUMCHECKVAL];                        /* integer array, store possible values  */
    int num_okintval;                                            /* number of valid values in the checkingval array */
  } s1;
  struct  {
    int (*f1a)(configmodule_interface_t *cfg,
               paramdef_t *param); /* check an integer against a list of authorized values and set param value */
    /* to the corresponding item in setintval array (mainly for RRC params)     */
    int okintval[CONFIG_MAX_NUMCHECKVAL];                        /* integer array, store possible values in config file */
    int setintval[CONFIG_MAX_NUMCHECKVAL];                        /* integer array, values set in the paramdef structure */
    int num_okintval;                                            /* number of valid values in the checkingval array */
  } s1a;
  struct {
    int (*f2)(configmodule_interface_t *cfg,
              paramdef_t *param); /* check an integer against an authorized range, defined by its min and max value */
    int okintrange[CONFIG_MAX_NUMCHECKVAL];  /* integer array, store  min and max values  */

  } s2;
  struct {
    int (*f3)(configmodule_interface_t *cfg, paramdef_t *param); /* check a string against a list of authorized values */
    char *okstrval[CONFIG_MAX_NUMCHECKVAL];                      /* string array, store possible values  */
    int  num_okstrval;                                           /* number of valid values in the checkingval array */
  } s3;
  struct {
    int (*f3a)(configmodule_interface_t *cfg,
               paramdef_t *param); /* check a string against a list of authorized values and set param value */
    /* to the corresponding item in setintval array (mainly for RRC params) */
    char *okstrval[CONFIG_MAX_NUMCHECKVAL];                      /* string array, store possible values  */
    int  setintval[CONFIG_MAX_NUMCHECKVAL];                      /* integer array, values set in the paramdef structure */
    int  num_okstrval;                                           /* number of valid values in the checkingval array */
  } s3a;
  struct {
    int (*f4)(configmodule_interface_t *cfg,
              paramdef_t *param); /* generic check function, no arguments but the param description */

  } s4;
  struct {
    void (*checkfunc)(configmodule_interface_t *cfg);
  } s5;
} checkedparam_t;

/* paramdef is used to describe a parameter, array of paramdef_t strustures is used as the main parameter in */
/* config apis used to retrieve parameters values  */
#define MAX_LIST_SIZE 32
#define DEFAULT_EXTRA_SZ 256
typedef struct paramdef {
  char         optname[MAX_OPTNAME_SIZE]; /* parameter name, can be used as long command line option */
  char         *helpstr;                  /* help string */
  unsigned int paramflags;                /* value is a "ored" combination of above PARAMFLAG_XXXX values */
  union { /* pointer to the parameter value, completed by the config module */
    char      **strptr;
    char      **strlistptr;
    uint8_t   *u8ptr;
    int8_t    *i8ptr;
    uint16_t  *u16ptr;
    int16_t   *i16ptr;
    uint32_t  *uptr;
    int32_t   *iptr;
    uint64_t  *u64ptr;
    int64_t   *i64ptr;
    double    *dblptr;
    void      *voidptr;
  } ;
  union {                                /* default parameter value, to be used when PARAMFLAG_MANDATORY is not specified */
    char      *defstrval;
    char      **defstrlistval;
    uint32_t  defuintval;
    int       defintval;
    uint64_t  defint64val;
    int       *defintarrayval;
    double    defdblval;
  } ;
  char type;                              /* parameter value type, as listed below as TYPE_XXXX macro */
  int numelt;                             /* number of elements in a list or array parameters or max size of string value */
  checkedparam_t   *chkPptr;              /* possible pointer to the structure containing the info used to check parameter values */
  int *processedvalue;                    /* used to store integer values computed from string original value */
} paramdef_t;

#define TYPE_INT        TYPE_INT32
#define TYPE_UINT       TYPE_UINT32
#define TYPE_STRING     1
#define TYPE_INT8       2
#define TYPE_UINT8      3
#define TYPE_INT16      4
#define TYPE_UINT16     5
#define TYPE_INT32      6
#define TYPE_UINT32     7
#define TYPE_INT64      8
#define TYPE_UINT64     9
#define TYPE_MASK       10
#define TYPE_DOUBLE     16
#define TYPE_IPV4ADDR   20
#define TYPE_LASTSCALAR 25

#define PARAM_ISLISTORARRAY(P)  (P->type >  TYPE_LASTSCALAR )
#define PARAM_ISSCALAR(P)       (P->type <  TYPE_LASTSCALAR )

#define TYPE_STRINGLIST 50
#define TYPE_INTARRAY   51
#define TYPE_UINTARRAY  52
#define TYPE_LIST       55

#define ANY_IPV4ADDR_STRING "0.0.0.0"





//-------------------------------------------------------------------------------------------------


// Define structs for the nested structures

#define MAX_ALGO_COUNT 4
#define DEFAULT_LOG_LEVEL "info"
#define MAX_CORES 128

#include <stdio.h>
#include <yaml.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int L1_THREAD_POOL_SIZ;

    int16_t L1_TX_AMP_backoff_db;
    bool L1_PHASE_comp;
    int L1_SRS_DTX_THRES;
    int numPRSconfigs;
    int pdsch_AntennaPorts_N1;
    int pdsch_AntennaPorts_N2;
    int pdsch_AntennaPorts_XP;
    int pusch_AntennaPorts_idx;

    int PUSCH_target_snrx10;
    int PUCCH_target_snrx10;
    int UL_PRBblack_SNR_thres;
    int PUCCH_fail_thres;
    int PUSCH_fail_thres;

    uint16_t GNB_Remote_S_PORTD;
    char *GNB_local_S_ADDR;
    uint16_t GNB_local_S_PORTD;
    uint64_t GNB_cuup_id;

    uint32_t ULSCH_max_frame;
    double dloption_upper;
    double dloption_lower;
    uint8_t dloption_maxmcs;
    uint8_t dloption_harqmaxround;

    double uloption_upper;
    double uloption_lower;
    uint8_t uloption_maxmcs;
    uint8_t uloption_harqmaxround;
    uint8_t min_grant_PRB;
    uint8_t min_grant_MCS;
    bool identity_PM;
    int num_ulprbbl;
    char *gnb_tr_s_pref;
    char *sdapflag;
    int UMONDEFAULTDRB;
    int D_DRBs;
    char *x2ap_state;
    char *Cell_typ;
    int slice_d;
   
    int minRXTXTIME;
    int sib1_tda;
    int do_CSIRS;
    int do_SRS;
    bool force_256qam_dis;

    bool force_UL256qam_off;
    bool use_deltaMCS;
    int maxMIMO_layers;
    bool disable_harq;
    int num_dlharq;
    int num_ulharq;

    int sr_ProhibitTimer;
    int sr_TransMax;
    int sr_ProhibitTimer_v1700;
    int t300;
    int t301;
    int t310;
    int n310;
    int t311;
    int n311;
    int t319;
    int GNB_BEAMWEIGH_IDX;
    bool GNBwtsidx;

    uint32_t OFDM_offset_divisor;
    int OPT_typ;
    int numGLBoptions;
    uint32_t LOGparamlogFILE;
    uint32_t LOGparamDUMP;
    uint32_t LOGparamDEBUG;
    uint8_t NFAPI_mde;
    int NFAPI_idx;
} Globaldefvals_t;

typedef struct {
    char *log_levels[MAX_LOG_PREDEF_COMPONENTS];
    char *global_log_level;
    char *hw_log_level;
    char *phy_log_level;
} LogConfig_t;

#include <stdbool.h> // For using `bool` type

typedef struct {
    // Horizontal levels for different protocol layers
    struct {
        bool PDCP;
        bool SCTP;
        bool F1AP;
        bool RRC;
        bool NGAP;
        bool GTPU;
        bool E1AP;
        bool RLC;
        bool CU_APP;
        bool MAC;
        bool PHY;
        bool ARIA;
        bool X2AP;
        bool P5_INFO_LEVEL;
        bool P7_INFO_LEVEL;
        bool SDAP;
        bool GNB_APP;
        bool DTLS;
    } horizontal_level;

    int vertical_level;

    // Other configurations
    bool print_datetime;        // Whether to print timestamps in logs
    bool print_configurations;  // Whether to print logging configurations
    bool generate_log_file;     // Whether to generate a log file
} LogConfig;

typedef struct {
    // Horizontal levels for different protocol layers
    struct {
        bool PDCP;
        bool SCTP;
        bool F1AP;
        bool RRC;
        bool NGAP;
        bool GTPU;
        bool E1AP;
        bool RLC;
        bool CU_APP;
        bool MAC;
        bool PHY;
        bool ARIA;
        bool X2AP;
        bool P5_INFO_LEVEL;
        bool P7_INFO_LEVEL;
        bool SDAP;
        bool GNB_APP;
        bool DTLS;
    } horizontal_level;

    // Vertical logging level: LOG_NONE (0), LOG_INFO (1), LOG_DEBUG (2)
    int vertical_level;

    // Other configurations
    bool print_datetime;        // Whether to print timestamps in logs
    bool print_configurations;  // Whether to print logging configurations
    bool generate_log_file;     // Whether to generate a log file
} LogFileConfig;


typedef struct {
    int num_cc;
    char *tr_n_preference;
    int prach_dtx_threshold;
    int pucch0_dtx_threshold;
    int pusch_dtx_threshold;
    int max_ldpc_iterations;
    int tx_amp_backoff_dB;
    int L1_rx_thread_core;
    int L1_tx_thread_core;
    int phase_compensation;
    char *local_n_if_name;
    char *remote_n_address;
    char *local_n_address;
    int local_n_portc;
    int remote_n_portc;
    int local_n_portd;
    int remote_n_portd;
} L1Config_t;

typedef struct {
    char *local_rf;
    int sl_ahead;
    int nb_tx;
    int nb_rx;
    int att_tx;
    int att_rx;
    int numbands;
    int bands;
    int max_pdschReferenceSignalPower;
    int max_rxgain;
    int sf_extension;
    int eNB_instances;
    int ru_thread_core;
    unsigned int bf_weights[10];
    int num_bfwts;
    int tpc_cores[10];  // Array to store TPCore values
    char *tr_preference;
    int do_precoding;
    int numeNB_instances;
    char *RUs_GPIO;
    char *RUs_CLKSRC;
    char *RUs_TIMESRC;

    int set_RUsGPIO;
    int set_RUsCLKSRC;
    int set_RUsTIMESRC;
    int set_bfwts;
    int set_txsubdev;
    int set_rxsubdev;
    int set_RuSDRaddr;

    char *LOCALifname;
    char *LOCALaddr;
    char *REMOTEaddr;
    
    uint16_t LOCALportC;
    uint16_t REMOTEportC;
    uint16_t LOCALportD;
    uint16_t REMOTEportD;

    uint64_t RU_IFfreq;
    int RU_if_freqoffset;

    int RU_nrflag;
    int RU_nr_scsRaster;
    int RU_rxfh_cores;
    int RU_txfh_cores;
    int RU_numtpcores;
    int RU_halfslot_parallelization;
    int num_tpcores_thrdpl;

} RUConfig_t;

typedef struct {
    uint64_t pkt_proc_core;
    uint32_t nEthLinePerPort;
    uint32_t nEthLineSpeed;
    int32_t timingcores;

    char *fileprefix;

    int num_dpdk_devs;
    int num_ruAddr;
    int num_duAddr;
    char *dpdk_devices[10];
    int system_core;
    int io_core;
    int worker_cores[10];
    char **du_addr[10];
    char **ru_addr[10];
    int mtu;
    uint32_t cp_vlan_tag;
    uint32_t up_vlan_tag;
    int num_fh_config;
    struct {
        int Tadv_cp_dl;
        struct {
            int min;
            int max;
        } T2a_cp_dl;
        struct {
            int min;
            int max;
        } T2a_cp_ul;
        struct {
            int min;
            int max;
        } T2a_up;
        struct {
            int min;
            int max;
        } Ta3;
        struct {
            int min;
            int max;
        } T1a_cp_dl;
        struct {
            int min;
            int max;
        } T1a_cp_ul;
        struct {
            int min;
            int max;
        } T1a_up;
        struct {
            int min;
            int max;
        } Ta4;
    } fh_config;
    struct {
        int iq_width;
        int iq_width_prach;
        int fft_size;
    } ru_config;
    struct {
        int eAxC_offset;
        int kbar;
    } prach_config;
} FHIConfig_t;

typedef struct {
    unsigned int U_SF_record;
    unsigned int U_SF_replay;
    unsigned int use_mmap;
    uint32_t maxshlibs;
    char *loddt_shlibpath;

    int numPl;
    LogConfig_t log_config;
    bool PNFactive;
    L1Config_t L1s;
    int numRUs;
    int numTHREADlist;
    Globaldefvals_t defvals;
    RUConfig_t RUs;
    FHIConfig_t fhi_72;
    int numfhi_72;

    char *thread_pool_cores[MAX_CORES];
    int num_thread_pool_cores;
    bool L1DUactive;
} GlobalPNFConfig_t;


// typedef struct {
//     int S_sst;
// } snssai_t;

// typedef struct {
//     int p_mcc;
//     int p_mnc;
//     int p_mnc_length;
//     snssai_t s_snssai;
//     int s_snssai_count;
// } plmn_list_t;

// typedef struct {
//     int C_SCTP_INSTREAMS;
//     int C_SCTP_OUTSTREAMS;
// } sctp_t;

// typedef struct {
//     char *a_ipv4;
// } amf_ip_address_t;

// typedef struct {
//     char *n_GNB_IPV4_ADDRESS_FOR_NG_AMF;
//     char *n_GNB_IPV4_ADDRESS_FOR_NGU;
//     int n_GNB_PORT_FOR_S1U;
// } network_interfaces_t;

// typedef struct {
//     int g_gNB_ID;
//     char *g_gNB_name;
//     int g_tracking_area_code;
//     plmn_list_t p_plmn;
//     uint64_t g_nr_cellid;
//     char *g_tr_s_preference;
//     char *g_local_s_address;
//     char *g_remote_s_address;
//     int g_local_s_portc;
//     int g_local_s_portd;
//     int g_remote_s_portc;
//     int g_remote_s_portd;
//     sctp_t C_SCTP;
//     amf_ip_address_t a_amf_ip;
//     network_interfaces_t n_net_if;
// } gnb_t;

// typedef struct {
//     char *s_ciphering_algorithms[MAX_ALGO_COUNT];
//     char *s_integrity_algorithms[MAX_ALGO_COUNT];
//     char *s_drb_ciphering;
//     char *s_drb_integrity;
// } security_t;

typedef struct {

  char *enable_O_flag;
  char *enable_rfsim_flag;

} config_flags_t;

typedef struct {

  char *log_levels[MAX_LOG_PREDEF_COMPONENTS];
  char *l_global_log_level;
  char *l_hw_log_level;
  char *l_phy_log_level;
  char *l_mac_log_level;
  char *l_rlc_log_level;
  char *l_pdcp_log_level;
  char *l_rrc_log_level;
  char *l_f1ap_log_level;
    
  char *l_global_log_online;
  char *l_global_log_options;


} log_config_t;

// typedef struct {

//   uint8_t NFAPI_mde;
//   int NFAPI_idx;
//   char *x2ap_state;
//   int OPT_TYP;
//   uint32_t LOGparamlogFILE;
//   uint32_t LOGparamDUMP;
//   uint32_t LOGparamDEBUG;
//   uint64_t GNBcuupID;
//   int64_t A2EventEnable;
//   int64_t A2thresRSRP;
//   int64_t A2TimetoTrigger;  
//   int64_t MeasEventEnable;
//   int64_t D_BeamMeasurements;  // New field for beam measurements
//   int64_t D_MAXrs_IdxToReport;  // New field for max number of RS indexes
//   char *Cell_typ;
//   int slice_d;
//   int D_DRBs;
//   int UMONDEFAULTDRD;
//   char *sdapflag;
//   int REM_S_PORTC;
//   int LOC_S_PORTD;
// } def_vals_t;

typedef enum new_NR_SubcarrierSpacing {
	new_NR_SubcarrierSpacing_kHz15	= 0,
	new_NR_SubcarrierSpacing_kHz30	= 1,
	new_NR_SubcarrierSpacing_kHz60	= 2,
	new_NR_SubcarrierSpacing_kHz120	= 3,
	new_NR_SubcarrierSpacing_kHz240	= 4,
	new_NR_SubcarrierSpacing_kHz480_v1700	= 5,
	new_NR_SubcarrierSpacing_kHz960_v1700	= 6,
	new_NR_SubcarrierSpacing_spare1	= 7
} new_e_NR_SubcarrierSpacing;

typedef long	 new_NR_SubcarrierSpacing_t;
typedef long	 new_NR_ControlResourceSetZero_t;


typedef ssize_t new_ber_tlv_len_t;

typedef struct new_asn_struct_ctx_s {
	short phase;		/* Decoding phase */
	short step;		/* Elementary step of a phase */
	int context;		/* Other context information */
	void *ptr;		/* Decoder-specific stuff (stack elements) */
	new_ber_tlv_len_t left;	/* Number of bytes left, -1 for indefinite */
} new_asn_struct_ctx_t;


// typedef struct {
//     int offsetToCarrier;
//     int subcarrierSpacing;
//     int carrierBandwidth;
// } SCS_SpecificCarrier_t;

// typedef struct {
    
//     struct {
//         SCS_SpecificCarrier_t *array[10]; // Assuming max 10 carriers
//         int count;                        // Number of carriers
//     } scs_SpecificCarrierList;
// } FrequencyInfoDL_t;

typedef struct new_NR_SCS_SpecificCarrier {
	long	 offsetToCarrier;
	new_NR_SubcarrierSpacing_t	 subcarrierSpacing;
	long	 carrierBandwidth;

	// struct NR_SCS_SpecificCarrier__ext1 {
	// 	long	*txDirectCurrentLocation;	/* OPTIONAL */
		
	// 	/* Context for parsing across buffer boundaries */
	// 	new_asn_struct_ctx_t _asn_ctx;
	// } *ext1;
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_SCS_SpecificCarrier_t;

typedef long     new_NR_ARFCN_ValueNR_t;
typedef long     new_NR_FreqBandIndicatorNR_t;


typedef struct new_NR_MultiFrequencyBandListNR {
    A_SEQUENCE_OF(new_NR_FreqBandIndicatorNR_t) list;    //doubt** (see line 58 and 588)
    new_asn_struct_ctx_t _asn_ctx;

}new_NR_MultiFrequencyBandListNR_t;

typedef struct new_NR_FrequencyInfoDL {
    new_NR_ARFCN_ValueNR_t	*absoluteFrequencySSB;
    new_NR_MultiFrequencyBandListNR_t	 *frequencyBandList;
    new_NR_ARFCN_ValueNR_t	 *absoluteFrequencyPointA;
    	struct new_NR_FrequencyInfoDL__scs_SpecificCarrierList {
		A_SEQUENCE_OF(struct new_NR_SCS_SpecificCarrier) list;
		
		/* Context for parsing across buffer boundaries */
		new_asn_struct_ctx_t _asn_ctx;
	} new_scs_SpecificCarrierList;
	// NR_ARFCN_ValueNR_t	*absoluteFrequencySSB;	/* OPTIONAL */
	// NR_MultiFrequencyBandListNR_t	 frequencyBandList;
	// NR_ARFCN_ValueNR_t	 absoluteFrequencyPointA;
	// struct NR_FrequencyInfoDL__scs_SpecificCarrierList {
	// 	A_SEQUENCE_OF(struct new_NR_SCS_SpecificCarrier) list;
		
	// 	/* Context for parsing across buffer boundaries */
	// 	new_asn_struct_ctx_t _asn_ctx;
	// } scs_SpecificCarrierList;
	// /*
	//  * This type is extensible,
	//  * possible extensions are below.
	//  */
	
	// /* Context for parsing across buffer boundaries */
	// asn_struct_ctx_t _asn_ctx;
} new_NR_FrequencyInfoDL_t;

// typedef enum NR_SetupRelease_RACH_ConfigCommon_PR {
// 	NR_SetupRelease_RACH_ConfigCommon_PR_NOTHING,	/* No components present */
// 	NR_SetupRelease_RACH_ConfigCommon_PR_release,
// 	NR_SetupRelease_RACH_ConfigCommon_PR_setup
// } NR_SetupRelease_RACH_ConfigCommon_PR;

typedef struct new_NR_RACH_ConfigGeneric {
	long	 prach_ConfigurationIndex;
	long	 msg1_FDM;
	long	 msg1_FrequencyStart;
	long	 zeroCorrelationZoneConfig;
	long	 preambleReceivedTargetPower;
	long	 preambleTransMax;
	long	 powerRampingStep;
	long	 ra_ResponseWindow;
	/*
	 * This type is extensible,
	 * possible extensions are below.
	 */
	// struct new_NR_RACH_ConfigGeneric__ext1 {
	// 	long	*prach_ConfigurationPeriodScaling_IAB_r16;	/* OPTIONAL */
	// 	long	*prach_ConfigurationFrameOffset_IAB_r16;	/* OPTIONAL */
	// 	long	*prach_ConfigurationSOffset_IAB_r16;	/* OPTIONAL */
	// 	long	*ra_ResponseWindow_v1610;	/* OPTIONAL */
	// 	long	*prach_ConfigurationIndex_v1610;	/* OPTIONAL */
		
	// 	/* Context for parsing across buffer boundaries */
	// 	// asn_struct_ctx_t _asn_ctx;
	// } *ext1;
	// struct new_NR_RACH_ConfigGeneric__ext2 {
	// 	long	*ra_ResponseWindow_v1700;	/* OPTIONAL */
		
	// 	/* Context for parsing across buffer boundaries */
	// 	// asn_struct_ctx_t _asn_ctx;
	// } *ext2;
	
	/* Context for parsing across buffer boundaries */
	// asn_struct_ctx_t _asn_ctx;
} new_NR_RACH_ConfigGeneric_t;

typedef enum new_NR_PUSCH_Config__transformPrecoder {
	new_NR_PUSCH_Config__transformPrecoder_enabled	= 0,
	new_NR_PUSCH_Config__transformPrecoder_disabled	= 1
} new_e_NR_PUSCH_Config__transformPrecoder;

typedef enum new_NR_RACH_ConfigGeneric__msg1_FDM {
	new_NR_RACH_ConfigGeneric__msg1_FDM_one	= 0,
	new_NR_RACH_ConfigGeneric__msg1_FDM_two	= 1,
	new_NR_RACH_ConfigGeneric__msg1_FDM_four	= 2,
	new_NR_RACH_ConfigGeneric__msg1_FDM_eight	= 3
} new_e_NR_RACH_ConfigGeneric__msg1_FDM;

typedef enum new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR {
	new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_NOTHING,	/* No components present */
	new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_oneEighth,
	new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_oneFourth,
	new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_oneHalf,
	new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_one,
	new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_two,
	new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_four,
	new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_eight,
	new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR_sixteen
} new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR;

typedef enum new_NR_RACH_ConfigCommon__prach_RootSequenceIndex_PR {
	new_NR_RACH_ConfigCommon__prach_RootSequenceIndex_PR_NOTHING,	/* No components present */
	new_NR_RACH_ConfigCommon__prach_RootSequenceIndex_PR_l839,
	new_NR_RACH_ConfigCommon__prach_RootSequenceIndex_PR_l139
} new_NR_RACH_ConfigCommon__prach_RootSequenceIndex_PR;

typedef enum new_NR_ServingCellConfigCommon__ssb_periodicityServingCell {
	new_NR_ServingCellConfigCommon__ssb_periodicityServingCell_ms5	= 0,
	new_NR_ServingCellConfigCommon__ssb_periodicityServingCell_ms10	= 1,
	new_NR_ServingCellConfigCommon__ssb_periodicityServingCell_ms20	= 2,
	new_NR_ServingCellConfigCommon__ssb_periodicityServingCell_ms40	= 3,
	new_NR_ServingCellConfigCommon__ssb_periodicityServingCell_ms80	= 4,
	new_NR_ServingCellConfigCommon__ssb_periodicityServingCell_ms160	= 5,
	new_NR_ServingCellConfigCommon__ssb_periodicityServingCell_spare2	= 6,
	new_NR_ServingCellConfigCommon__ssb_periodicityServingCell_spare1	= 7
} new_e_NR_ServingCellConfigCommon__ssb_periodicityServingCell;

typedef enum new_NR_RACH_ConfigCommon__restrictedSetConfig {
	new_NR_RACH_ConfigCommon__restrictedSetConfig_unrestrictedSet	= 0,
	new_NR_RACH_ConfigCommon__restrictedSetConfig_restrictedSetTypeA	= 1,
	new_NR_RACH_ConfigCommon__restrictedSetConfig_restrictedSetTypeB	= 2
} new_NR_RACH_ConfigCommon__restrictedSetConfig;

typedef enum new_NR_SetupRelease_PUSCH_ConfigCommon_PR {
	new_NR_SetupRelease_PUSCH_ConfigCommon_PR_NOTHING,	/* No components present */
	new_NR_SetupRelease_PUSCH_ConfigCommon_PR_release,
	new_NR_SetupRelease_PUSCH_ConfigCommon_PR_setup
} new_NR_SetupRelease_PUSCH_ConfigCommon_PR;

typedef enum new_NR_SetupRelease_PUCCH_ConfigCommon_PR {
	new_NR_SetupRelease_PUCCH_ConfigCommon_PR_NOTHING,	/* No components present */
	new_NR_SetupRelease_PUCCH_ConfigCommon_PR_release,
	new_NR_SetupRelease_PUCCH_ConfigCommon_PR_setup
} new_NR_SetupRelease_PUCCH_ConfigCommon_PR;

typedef enum new_NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR {
	new_NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_NOTHING,	/* No components present */
	new_NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_shortBitmap,
	new_NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_mediumBitmap,
	new_NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_longBitmap
} new_NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR;

typedef enum new_NR_ServingCellConfigCommon__dmrs_TypeA_Position {
	new_NR_ServingCellConfigCommon__dmrs_TypeA_Position_pos2	= 0,
	new_NR_ServingCellConfigCommon__dmrs_TypeA_Position_pos3	= 1
} new_e_NR_ServingCellConfigCommon__dmrs_TypeA_Position;

// typedef long	 new_NR_SubcarrierSpacing_t;
typedef long	 new_NR_RSRP_Range_t;
typedef long	 new_NR_PhysCellId_t;
// typedef long	 new_NR_SubcarrierSpacing_t;
// typedef long     new_NR_ARFCN_ValueNR_t;
// typedef long     new_NR_FreqBandIndicatorNR_t;
typedef long	 new_NR_SearchSpaceZero_t;
// typedef long	 new_NR_SubcarrierSpacing_t;
typedef long	 new_NR_P_Max_t;
// typedef long	 new_NR_SubcarrierSpacing_t;
typedef int      new_NULL_t;


typedef struct new_BIT_STRING_s {
	uint8_t *buf;	/* BIT STRING body */
	size_t size;	/* Size of the above buffer */

	int bits_unused;/* Unused trailing bits in the last octet (0..7) */

	new_asn_struct_ctx_t _asn_ctx;	/* Parsing across buffer boundaries */
} new_BIT_STRING_t;

typedef struct new_NR_PUSCH_TimeDomainResourceAllocation {
	long	*k2;	/* OPTIONAL */
	long	 mappingType;
	long	 startSymbolAndLength;
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_PUSCH_TimeDomainResourceAllocation_t;

typedef struct new_NR_PUSCH_TimeDomainResourceAllocationList {
	A_SEQUENCE_OF(struct new_NR_PUSCH_TimeDomainResourceAllocation) list;
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_PUSCH_TimeDomainResourceAllocationList_t;



typedef struct new_NR_RACH_ConfigCommon {
	new_NR_RACH_ConfigGeneric_t	 *rach_ConfigGeneric;
	long	*totalNumberOfRA_Preambles;	/* OPTIONAL */
	struct new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB {
		new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR present;
		union new_NR_RACH_ConfigCommon__NR_ssb_perRACH_OccasionAndCB_PreamblesPerSSB_u {
			long	 oneEighth;
			long	 oneFourth;
			long	 oneHalf;
			long	 one;
			long	 two;
			long	 four;
			long	 eight;
			long	 sixteen;
		} choice;
		
		/* Context for parsing across buffer boundaries */
		// asn_struct_ctx_t _asn_ctx;
	} *ssb_perRACH_OccasionAndCB_PreamblesPerSSB;
	struct new_NR_RACH_ConfigCommon__groupBconfigured {
		long	 ra_Msg3SizeGroupA;
		long	 messagePowerOffsetGroupB;
		long	 numberOfRA_PreamblesGroupA;
		
		/* Context for parsing across buffer boundaries */
		// asn_struct_ctx_t _asn_ctx;
	} *groupBconfigured;
	long	 ra_ContentionResolutionTimer;
	new_NR_RSRP_Range_t	*rsrp_ThresholdSSB;	/* OPTIONAL */
	new_NR_RSRP_Range_t	*rsrp_ThresholdSSB_SUL;	/* OPTIONAL */
	struct new_NR_RACH_ConfigCommon__prach_RootSequenceIndex {
		new_NR_RACH_ConfigCommon__prach_RootSequenceIndex_PR present;
		union new_NR_RACH_ConfigCommon__NR_prach_RootSequenceIndex_u {
			long	 l839;
			long	 l139;
		} choice;
		
		/* Context for parsing across buffer boundaries */
		// asn_struct_ctx_t _asn_ctx;
	} prach_RootSequenceIndex;
	new_NR_SubcarrierSpacing_t	*msg1_SubcarrierSpacing;	/* OPTIONAL */
	long	 restrictedSetConfig;
	long	*msg3_transformPrecoder;	/* OPTIONAL */


} new_NR_RACH_ConfigCommon_t;

typedef enum new_NR_SetupRelease_RACH_ConfigCommon_PR {
	new_NR_SetupRelease_RACH_ConfigCommon_PR_NOTHING,	/* No components present */
	new_NR_SetupRelease_RACH_ConfigCommon_PR_release,
	new_NR_SetupRelease_RACH_ConfigCommon_PR_setup
} new_NR_SetupRelease_RACH_ConfigCommon_PR;

typedef struct new_NR_SetupRelease_RACH_ConfigCommon {
	new_NR_SetupRelease_RACH_ConfigCommon_PR present;
	union new_NR_SetupRelease_RACH_ConfigCommon_u {
		new_NULL_t	 release;
		struct new_NR_RACH_ConfigCommon	*setup;
	} choice;
	
	// /* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_SetupRel_RACH_ConfigCommon_t;

typedef struct {
    new_NR_SetupRel_RACH_ConfigCommon_t  *rach_ConfigCommon;

} new_BWP_UplinkCommon_t;



struct new_NR_SCS_SpecificCarrier;


typedef struct new_NR_FrequencyInfoUL{
    new_NR_MultiFrequencyBandListNR_t	*frequencyBandList;
    new_NR_ARFCN_ValueNR_t	*absoluteFrequencyPointA;
    struct new_NR_FrequencyInfoUL__scs_SpecificCarrierList {
		A_SEQUENCE_OF(struct new_NR_SCS_SpecificCarrier) list;
		
		/* Context for parsing across buffer boundaries */
		new_asn_struct_ctx_t _asn_ctx;
	} new_scs_SpecificCarrierList;
    new_NR_P_Max_t	*p_Max;
    new_asn_struct_ctx_t _asn_ctx;
}new_NR_FrequencyInfoUL_t;

typedef struct new_NR_PUSCH_ConfigCommon {
	long	*groupHoppingEnabledTransformPrecoding;	/* OPTIONAL */
	struct new_NR_PUSCH_TimeDomainResourceAllocationList	*pusch_TimeDomainAllocationList;	/* OPTIONAL */
	long	*msg3_DeltaPreamble;	/* OPTIONAL */
	long	*p0_NominalWithGrant;	/* OPTIONAL */
	/*
	 * This type is extensible,
	 * possible extensions are below.
	 */
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_PUSCH_ConfigCommon_t;


typedef enum new_NR_PUCCH_ConfigCommon__pucch_GroupHopping {
	new_NR_PUCCH_ConfigCommon__pucch_GroupHopping_neither	= 0,
	new_NR_PUCCH_ConfigCommon__pucch_GroupHopping_enable	= 1,
	new_NR_PUCCH_ConfigCommon__pucch_GroupHopping_disable	= 2
} new_e_NR_PUCCH_ConfigCommon__pucch_GroupHopping;


typedef struct new_NR_SetupRelease_PUSCH_ConfigCommon {
	new_NR_SetupRelease_PUSCH_ConfigCommon_PR present;
	union new_NR_SetupRelease_PUSCH_ConfigCommon_u {
		new_NULL_t	 release;
		struct new_NR_PUSCH_ConfigCommon	*setup;
	} choice;
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_SetupRelease_PUSCH_ConfigCommon_t;

typedef struct new_NR_PUCCH_ConfigCommon {
	long	*pucch_ResourceCommon;	/* OPTIONAL */
	long	 pucch_GroupHopping;
	long	*hoppingId;	/* OPTIONAL */
	long	*p0_nominal;	/* OPTIONAL */
	/*
	 * This type is extensible,
	 * possible extensions are below.
	 */
	struct new_NR_PUCCH_ConfigCommon__ext1 {
		long	*nrofPRBs;	/* OPTIONAL */
		long	*intra_SlotFH_r17;	/* OPTIONAL */
		long	*pucch_ResourceCommonRedCap_r17;	/* OPTIONAL */
		long	*additionalPRBOffset_r17;	/* OPTIONAL */
		
		/* Context for parsing across buffer boundaries */
		new_asn_struct_ctx_t _asn_ctx;
	} *ext1;
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_PUCCH_ConfigCommon_t;

typedef struct new_NR_SetupRelease_PUCCH_ConfigCommon {
	new_NR_SetupRelease_PUCCH_ConfigCommon_PR present;
	union new_NR_SetupRelease_PUCCH_ConfigCommon_u {
		new_NULL_t	 release;
		struct new_NR_PUCCH_ConfigCommon	*setup;
	} choice;
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_SetupRelease_PUCCH_ConfigCommon_t;

typedef struct new_NR_BWP_UplinkCommon{
    struct new_NR_BWP	 *genericParameters;
    struct new_NR_SetupRelease_RACH_ConfigCommon	*rach_ConfigCommon;
    struct new_NR_SetupRelease_PUSCH_ConfigCommon	*pusch_ConfigCommon;
    struct new_NR_SetupRelease_PUCCH_ConfigCommon	*pucch_ConfigCommon;

}new_NR_BWP_UplinkCommon_t;

struct new_NR_BWP_UplinkCommon;

typedef struct {
	struct new_NR_FrequencyInfoUL	*frequencyInfoUL;	/* OPTIONAL */
    
	new_NR_BWP_UplinkCommon_t	*initialUplinkBWP;	/* OPTIONAL */
	// NR_TimeAlignmentTimer_t	 dummy;
	
	// /* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_UplinkConfigCommon_t;

typedef struct new_NR_BWP {
	long	 locationAndBandwidth;
	// NR_SubcarrierSpacing_t	 subcarrierSpacing;
    long	 subcarrierSpacing;
	long	*cyclicPrefix;	/* OPTIONAL */
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_BWP_t;

typedef struct new_NR_PDCCH_ConfigCommon{
    new_NR_SearchSpaceZero_t	*searchSpaceZero;
    new_NR_ControlResourceSetZero_t	*controlResourceSetZero;

}new_NR_PDCCH_ConfigCommon_t; 

typedef struct new_NR_SetupRelease_PDCCH_ConfigCommon {
	// NR_SetupRelease_PDCCH_ConfigCommon_PR present;
	union new_NR_SetupRelease_PDCCH_ConfigCommon_u {
		new_NULL_t	 release;
		struct new_NR_PDCCH_ConfigCommon	*setup;
	} choice;
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_SetupRelease_PDCCH_ConfigCommon_t;

typedef struct new_NR_RACH_ConfigGenericTwoStepRA_r16 {
    long	*msgB_ResponseWindow_r16;
} new_NR_RACH_ConfigGenericTwoStepRA_r16_t;

typedef struct new_NR_RACH_ConfigCommonTwoStepRA_r16 {
    new_NR_RACH_ConfigGenericTwoStepRA_r16_t	 rach_ConfigGenericTwoStepRA_r16;
    new_NR_RSRP_Range_t	*msgA_RSRP_Threshold_r16;
    long	*msgA_CB_PreamblesPerSSB_PerSharedRO_r16;
} new_NR_RACH_ConfigCommonTwoStepRA_r16_t;

typedef struct new_NR_MsgA_DMRS_Config_r16 {
	long	*msgA_DMRS_AdditionalPosition_r16;	/* OPTIONAL */
	long	*msgA_MaxLength_r16;	/* OPTIONAL */
	long	*msgA_PUSCH_DMRS_CDM_Group_r16;	/* OPTIONAL */
	long	*msgA_PUSCH_NrofPorts_r16;	/* OPTIONAL */
	long	*msgA_ScramblingID0_r16;	/* OPTIONAL */
	long	*msgA_ScramblingID1_r16;	/* OPTIONAL */
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_MsgA_DMRS_Config_r16_t;

typedef struct new_NR_MsgA_PUSCH_Resource_r16 {
	long	 msgA_MCS_r16;
	long	 nrofSlotsMsgA_PUSCH_r16;
	long	 nrofMsgA_PO_PerSlot_r16;
	long	 msgA_PUSCH_TimeDomainOffset_r16;
	long	*msgA_PUSCH_TimeDomainAllocation_r16;	/* OPTIONAL */
	long	*startSymbolAndLengthMsgA_PO_r16;	/* OPTIONAL */
	long	*mappingTypeMsgA_PUSCH_r16;	/* OPTIONAL */
	long	*guardPeriodMsgA_PUSCH_r16;	/* OPTIONAL */
	long	 guardBandMsgA_PUSCH_r16;
	long	 frequencyStartMsgA_PUSCH_r16;
	long	 nrofPRBs_PerMsgA_PO_r16;
	long	 nrofMsgA_PO_FDM_r16;
	long	*msgA_IntraSlotFrequencyHopping_r16;	/* OPTIONAL */
	
	new_NR_MsgA_DMRS_Config_r16_t	 msgA_DMRS_Config_r16;
	long	 nrofDMRS_Sequences_r16;
	long	*msgA_Alpha_r16;	/* OPTIONAL */
	long	*interlaceIndexFirstPO_MsgA_PUSCH_r16;	/* OPTIONAL */
	long	*nrofInterlacesPerMsgA_PO_r16;	/* OPTIONAL */
	/*
	 * This type is extensible,
	 * possible extensions are below.
	 */
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_MsgA_PUSCH_Resource_r16_t;

typedef struct new_NR_MsgA_PUSCH_Config_r16 {
	struct new_NR_MsgA_PUSCH_Resource_r16	*msgA_PUSCH_ResourceGroupA_r16;	/* OPTIONAL */
	
	long	*msgA_TransformPrecoder_r16;	/* OPTIONAL */
	long	*msgA_DataScramblingIndex_r16;	/* OPTIONAL */
	long	*msgA_DeltaPreamble_r16;	/* OPTIONAL */
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_MsgA_PUSCH_Config_r16_t;

typedef struct new_NR_MsgA_ConfigCommon_r16 {
	new_NR_RACH_ConfigCommonTwoStepRA_r16_t	 rach_ConfigCommonTwoStepRA_r16;
	struct new_NR_MsgA_PUSCH_Config_r16	*msgA_PUSCH_Config_r16;	/* OPTIONAL */
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_MsgA_ConfigCommon_r16_t;

typedef struct new_NR_SetupRelease_MsgA_ConfigCommon_r16 {
	
	union new_NR_SetupRelease_MsgA_ConfigCommon_r16_u {
		
		struct new_NR_MsgA_ConfigCommon_r16	*setup;
	} choice;
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_SetupRelease_MsgA_ConfigCommon_r16_t;

typedef struct {
	struct new_NR_BWP	 *genericParameters;
	struct new_NR_SetupRelease_PDCCH_ConfigCommon	*pdcch_ConfigCommon;	/* OPTIONAL */
	struct NR_SetupRelease_PDSCH_ConfigCommon	*pdsch_ConfigCommon;	/* OPTIONAL */
    struct new_NR_BWP_UplinkCommon__ext1 {
		
		struct new_NR_SetupRelease_MsgA_ConfigCommon_r16	*msgA_ConfigCommon_r16;	/* OPTIONAL */
		
		/* Context for parsing across buffer boundaries */
		new_asn_struct_ctx_t _asn_ctx;
	} *ext1;

	new_asn_struct_ctx_t _asn_ctx;
} new_NR_BWP_DownlinkCommon_t;

typedef struct {
    new_NR_FrequencyInfoDL_t *frequencyInfoDL;
    new_NR_BWP_DownlinkCommon_t	*initialDownlinkBWP;
    // struct new_NR_BWP_DownlinkCommon	*initialDownlinkBWP;
} new_DownlinkConfigCommon_t;


typedef struct new_NR_TDD_UL_DL_Pattern {
	long	 dl_UL_TransmissionPeriodicity;
	long	 nrofDownlinkSlots;
	long	 nrofDownlinkSymbols;
	long	 nrofUplinkSlots;
	long	 nrofUplinkSymbols;
	/*
	 * This type is extensible,
	 * possible extensions are below.
	 */
	struct new_NR_TDD_UL_DL_Pattern__ext1 {
		long	*dl_UL_TransmissionPeriodicity_v1530;	/* OPTIONAL */
		
		/* Context for parsing across buffer boundaries */
		new_asn_struct_ctx_t _asn_ctx;
	} *ext1;
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_TDD_UL_DL_Pattern_t;


typedef struct new_NR_TDD_UL_DL_ConfigCommon {
	new_NR_SubcarrierSpacing_t	 referenceSubcarrierSpacing;
	new_NR_TDD_UL_DL_Pattern_t	 *pattern1;   //shivam changed here 
	struct new_NR_TDD_UL_DL_Pattern	*pattern2;	/* OPTIONAL */
	/*
	 * This type is extensible,
	 * possible extensions are below.
	 */
	
	/* Context for parsing across buffer boundaries */
	new_asn_struct_ctx_t _asn_ctx;
} new_NR_TDD_UL_DL_ConfigCommon_t;


typedef struct {
    new_NR_PhysCellId_t	*physCellId;
    long	*n_TimingAdvanceOffset;

    new_DownlinkConfigCommon_t	*downlinkConfigCommon;	/* OPTIONAL */
	new_UplinkConfigCommon_t	    *uplinkConfigCommon;	/* OPTIONAL */
	// UplinkConfigCommon_t	    supplementaryUplinkConfig;	/* OPTIONAL */
    struct new_NR_TDD_UL_DL_ConfigCommon	*tdd_UL_DL_ConfigurationCommon;	

    struct new_NR_ServingCellConfigCommon__ssb_PositionsInBurst {
		new_NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR present;
		union new_NR_ServingCellConfigCommon__NR_ssb_PositionsInBurst_u {
			new_BIT_STRING_t	 shortBitmap;
			new_BIT_STRING_t	 mediumBitmap;
			new_BIT_STRING_t	 longBitmap;
		} choice;
		
		/* Context for parsing across buffer boundaries */
		new_asn_struct_ctx_t _asn_ctx;
	} *ssb_PositionsInBurst;

    new_NR_SubcarrierSpacing_t	*ssbSubcarrierSpacing;
    long	*ssb_periodicityServingCell;	/* OPTIONAL */
	long	 dmrs_TypeA_Position;
    long	 ss_PBCH_BlockPower;




    new_asn_struct_ctx_t _asn_ctx;
} SCC_t;

// typedef struct _config_yaml_t{

//     int numTHREADParamList;
//     int numPRSParamList;
//     int numNeighbourlist;
//     int numA3event;
//     int numSECURITY;
//     int numCIPHERalgo;
//     int numINTEGalgo;
//     int numAMFaddr;
//     int numSNSSAIlist;
//     int numplmnlist;
//     int numGNBparmlist;
//     int numGLBoptions;
//     int numPlmn;
//     int active_gnb_count;
//     char *y_Active_gNBs;
//     char *y_Asn1_verbosity;

//     config_flags_t Flags;
//     gnb_t y_gNBs[10];
//     security_t y_security;
    // log_config_t y_log_config;
//     def_vals_t y_defvalues;
// } config_yaml_t;

typedef struct {

    int64_t msgB_ResponseWindow_r16;
    int64_t msgA_RSRP_Threshold_r16;
    int64_t msgA_MCS_r16;
    int64_t nrofSlotsMsgA_PUSCH_r16;
    int64_t nrofMsgA_PO_PerSlot_r16;
    int64_t msgA_PUSCH_TimeDomainOffset_r16;
    int64_t startSymbolAndLengthMsgA_PO_r16;
    int64_t mappingTypeMsgA_PUSCH_r16;
    int64_t guardBandMsgA_PUSCH_r16;
    int64_t frequencyStartMsgA_PUSCH_r16;
    int64_t nrofPRBs_PerMsgA_PO_r16;
    int64_t nrofMsgA_PO_FDM_r16;
    int64_t msgA_PUSCH_NrofPorts_r16;
    int64_t nrofDMRS_Sequences_r16;
    int64_t msgA_TransformPrecoder_r16;
    int64_t msgA_CB_PreamblesPerSSB_PerSharedRO_r16;    

} MsgASCCConfs_t;

#define MAX_GNBS 10
#define MAX_SIB1_CONFIGS 10
#define MAX_SNSSAIS 10
#define MAX_ALGORITHMS 5
#define MAX_AMF_IPS 5

typedef struct {
    int controlResourceSetZero;
    int searchSpaceZero;
} PDCCHConfigSIB1_t;

typedef struct {
    int referenceSubcarrierSpacing;
    int dl_UL_TransmissionPeriodicity;
    int nrofDownlinkSlots;
    int nrofDownlinkSymbols;
    int nrofUplinkSlots;
    int nrofUplinkSymbols;
    int ssPBCH_BlockPower;
} TDD_UL_DL_ConfigurationCommon_t;

typedef struct {
    int prach_ConfigurationIndex;
    int prach_msg1_FDM;
    int prach_msg1_FrequencyStart;
    int zeroCorrelationZoneConfig;
    int preambleReceivedTargetPower;
    int preambleTransMax;
    int powerRampingStep;
    int ra_ResponseWindow;
    int ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR;
    int ssb_perRACH_OccasionAndCB_PreamblesPerSSB;
    int ra_ContentionResolutionTimer;
    int rsrp_ThresholdSSB;
    int prach_RootSequenceIndex_PR;
    int prach_RootSequenceIndex;
    int msg1_SubcarrierSpacing;
    int restrictedSetConfig;
    int msg3_DeltaPreamble;
    int p0_NominalWithGrant;
} RACHConfigCommon_t;

typedef struct {
    int pucchGroupHopping;
    int hoppingId;
    int p0_nominal;
    int ssb_PositionsInBurst_PR;
    int ssb_PositionsInBurst_Bitmap;
    int ssb_periodicityServingCell;
    int dmrs_TypeA_Position;
    int subcarrierSpacing;
} PUCCHConfigCommon_t;

typedef struct {
    int ul_frequencyBand;
    int ul_offstToCarrier;
    int ul_subcarrierSpacing;
    int ul_carrierBandwidth;
    int pMax;
    struct {
        int initialULBWPlocationAndBandwidth;
        int initialULBWPsubcarrierSpacing;
    } initialUplinkBWP;
    RACHConfigCommon_t rachConfigCommon;
    PUCCHConfigCommon_t pucchConfigCommon;
} UplinkConfigCommon_t;

typedef struct {
    int absoluteFrequencySSB;
    int dl_frequencyBand;
    int dl_absoluteFrequencyPointA;
    int dl_offstToCarrier;
    int dl_subcarrierSpacing;
    int dl_carrierBandwidth;
    struct {
        int initialDLBWPlocationAndBandwidth;
        int initialDLBWPsubcarrierSpacing;
        int initialDLBWPcontrolResourceSetZero;
        int initialDLBWPsearchSpaceZero;
    } initialDownlinkBWP;
} DownlinkConfigCommon_t;

typedef struct {
    int physCellId;
    DownlinkConfigCommon_t downlinkConfigCommon;
    UplinkConfigCommon_t uplinkConfigCommon;
} ServingCellConfigCommon_t;

typedef struct {
    char *gNB_name;
    unsigned int gNB_ID;
    unsigned int gNB_DU_ID;
    int tracking_area_code;
    unsigned long nr_cellid;
    int pusch_AntennaPorts;
    int pdsch_AntennaPorts_XP;
    int sib1_tda;
    int do_CSIRS;
    int do_SRS;
    PDCCHConfigSIB1_t pdcchConfigSIB1[MAX_SIB1_CONFIGS];
    ServingCellConfigCommon_t servingCellConfigCommon;
} GNBConfig_t;

typedef struct {
    int mcc;
    int mnc;
    int mnc_length;
    struct {
        int sst;
    } snssaiList[MAX_SNSSAIS];
} PLMN_t;

typedef struct {
    int SCTP_INSTREAMS;
    int SCTP_OUTSTREAMS;
} SCTPConfig_t;

typedef struct {
    char *ipv4;
    char *ipv6;
    char *active;
    char *preference;
} AMFIPAddress_t;

typedef struct {
    char *GNB_INTERFACE_NAME_FOR_NG_AMF;
    char *GNB_IPV4_ADDRESS_FOR_NG_AMF;
    char *GNB_INTERFACE_NAME_FOR_NGU;
    char *GNB_IPV4_ADDRESS_FOR_NGU;
    int GNB_PORT_FOR_S1U;
} NetworkInterfaces_t;

typedef struct {
    char *F1uaddr;
    int num_cc;
    char *local_s_if_name;
    char *remote_s_address;
    char *local_s_address;
    int local_s_portc;
    int remote_s_portc;
    int local_s_portd;
    int remote_s_portd;
    char *tr_s_preference;
    char *tr_n_preference;
    int pusch_failure_thres;
} MACRLCConfig_t;

typedef struct {
    char *Ciphering_algorithms[MAX_ALGO_COUNT];
    char *Integrity_algorithms[MAX_ALGO_COUNT];
    char *Drb_ciphering;
    char *Drb_integrity;
} SecurityConfig_t;

typedef struct {
    char *log_levels[MAX_LOG_PREDEF_COMPONENTS];
    char *global_log_level;
    char *sdap_log_level;
} LogVNFConfig_t;

typedef struct {
    int numCIPHERalgo;
    int numINTEGalgo;
    char *active_gNBs;
    char *asn1_verbosity;
    int numMACRLCs;
    int numactiveGNBs;
    bool VNFactive;
    int numServingcellcmn;
    int numMsgASCCsParam;
    int numSecurity;
    int numSCDconfigs;
    int bwdidx;
    int numPlmnlist;
    int numSNSSAIlist;
    int numNGParams;
    int numL1;
    char* ReleaseVersion;
    int L1_stats_update_interval;
    int GenerateLogFile;
    GNBConfig_t gNBs[MAX_GNBS];
    TDD_UL_DL_ConfigurationCommon_t tddConfigCommon;
    PLMN_t plmn_list[MAX_GNBS];
    SCTPConfig_t SCTP;
    AMFIPAddress_t amf_ip_addresses;
    NetworkInterfaces_t network_interfaces;
    MACRLCConfig_t macrlcs[MAX_GNBS];
    SecurityConfig_t security;
    LogVNFConfig_t log_config;
    LogConfig logConfigs;
    LogFileConfig logFileConfigs;
    SCC_t *GSCC;
    char *ULPRBBlacklist;

    MsgASCCConfs_t mSCC;
    uint8_t ANALOG_BEAMFORMG_IDX;

    int64_t ephemerisPositionX;
    int64_t ephemerisPositionY;
    int64_t ephemerisPositionZ;
    int64_t ephemerisVelocityVX;
    int64_t ephemerisVelocityVY;
    int64_t ephemerisVelocityVZ;
    int64_t taCommon;

    int64_t dloffsetToCarrier;
    int64_t dlsubcarrierSpacing;
    int64_t dlcarrierBandwidth;
    int64_t uloffsetToCarrier;
    int64_t ulsubcarrierSpacing;
    int64_t ulcarrierBandwidth;

} GlobalL1DUConfig_t;





//for YAML-CONFIG Structs

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Define structs for the nested structures

#define MAX_ALGO_COUNT 4

typedef struct {
    int SvcTypeID;
    uint32_t SliceDiff;
} snssai_t;

typedef struct {
    int MCC;
    int MNC;
    int MNC_len;
    snssai_t s_snssai;
    int s_snssai_count;
} plmn_list_t;

typedef struct {
    int FlexReceptionStreamSet;
    int FlexTransmissionStreamSet;
} sctp_t;

typedef struct {
    char *AMFDevIPv4;
} amf_ip_address_t;

typedef struct {
    char *NgAmfDevIPv4;
    char *NguDevIPv4;
    int S1uPort;
} network_interfaces_t;

typedef struct {
    int NodeKey;
    char *EntityLabel;
    int TAC;
    plmn_list_t p_plmn;
    uint64_t SegmentID;
    char *NodeClass;
    char *RIF_Choice;
    char *HostServIP;
    char *ExtPointIP;
    int HostSCrlPort;
    int HostSdtPort;
    int ExtScrlPort;
    int ExtSdtPort;
    sctp_t C_SCTP;
    amf_ip_address_t a_amf_ip;
    network_interfaces_t n_net_if;
} gnb_t;

typedef struct {
    char *CipherModes[MAX_ALGO_COUNT];
    char *AuthModes[MAX_ALGO_COUNT];
    char *BchannelEncryp;
    char *Bchannelauthentication;
} security_t;



typedef struct {

  uint8_t NFAPI_mde;
  int NFAPI_idx;
  char *x2ap_state;
  int OPT_TYP;
  uint32_t LOGparamlogFILE;
  uint32_t LOGparamDUMP;
  uint32_t LOGparamDEBUG;
  uint64_t GNBcuupID;
  char * cucp_ipv4;
  char * cuup_ipv4;
  int64_t A2EventEnable;
  int64_t A2thresRSRP;
  int64_t A2TimetoTrigger;  
  int64_t MeasEventEnable;
  int64_t D_BeamMeasurements;  // New field for beam measurements
  int64_t D_MAXrs_IdxToReport;  // New field for max number of RS indexes
  char *Cell_typ;
  int slice_d;
  int D_DRBs;
  int Default_DRBs;
  int UMONDEFAULTDRD;
  char *sdapflag;
  int REM_S_PORTC;
  int LOC_S_PORTD;
} def_vals_t;

typedef struct _config_yaml_t{

    int numTHREADParamList;
    int numPRSParamList;
    int numNeighbourlist;
    int numA3event;
    int numSECURITY;
    int numCIPHERalgo;
    int numINTEGalgo;
    int numAMFaddr;
    int numSNSSAIlist;
    int numplmnlist;
    int numNGparamlist;
    int numGNBparmlist;
    int numGLBoptions;
    int numPlmn;
    int active_gnb_count;
    char *FunctionalNodes;
    char *Asn1Granularity;
    char* ReleaseVersion;
    char* SDAPMode;
    gnb_t y_gNBs[10];
    security_t y_security;
    
    def_vals_t y_defvalues;
    log_config_t y_log_config;
    bool CUActive;
} config_yaml_t;



//-------------------------------------------------------------------------------------------





typedef struct paramlist_def {
  char listname[MAX_OPTNAME_SIZE];
  paramdef_t **paramarray;
  int numelt ;
} paramlist_def_t;

#endif  /* INCLUDE_CONFIG_PARAMDESC_H */
