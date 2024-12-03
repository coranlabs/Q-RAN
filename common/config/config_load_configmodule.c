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

/*! \file common/config/config_load_configmodule.c
 * \brief configuration module, load the shared library implementing the configuration module
 * \author Francois TABURET
 * \date 2017
 * \version 0.1
 * \company NOKIA BellLabs France
 * \email: francois.taburet@nokia-bell-labs.com
 * \note
 * \warning
 */
#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <dlfcn.h>
#include "common/platform_types.h"

#define CONFIG_LOADCONFIG_MAIN
#include "config_load_configmodule.h"
#include "config_userapi.h"
#include "../utils/LOG/log.h"
#define CONFIG_SHAREDLIBFORMAT "libparams_%s.so"
#include "nfapi/oai_integration/vendor_ext.h"
#include "config_common.h"

// clang-format off
static char  config_helpstr [] = "\n lte-softmodem -O [config mode]<:dbgl[debugflags]><:incp[path]>\n \
          debugflags can also be defined in the config section of the config file\n \
          debugflags: mask,    1->print parameters, 2->print memory allocations debug messages\n \
                               4->print command line processing debug messages\n \
          incp parameter can be used to define the include path used for config files (@include directive)\n \
                         defaults is set to the path of the main config file.\n";

static paramdef_t Config_Params[] = {
  /*--------------------------------------------------------------------------------------------------------------------------*/
  /*                                            config parameters for config module                                           */
  /* optname           helpstr             paramflags        XXXptr          defXXXval            type         numelt         */
  /*--------------------------------------------------------------------------------------------------------------------------*/
  {CONFIGP_DEBUGFLAGS, config_helpstr,     0,                .uptr = NULL,   .defintval = 0,      TYPE_MASK,   0},
  {CONFIGP_TMPDIR,     CONFIG_HELP_TMPDIR, PARAMFLAG_NOFREE, .strptr = NULL, .defstrval = "/tmp", TYPE_STRING, 0},
};
// clang-format on

config_yaml_t yaml_config = {0};

GlobalPNFConfig_t global_configs;
GlobalL1DUConfig_t global_vnf;
config_yaml_t GlobC_ARIAConfs;
bool do_ra = false;
bool nokrnMOD = true;
bool SAflag = true;
bool emulate_l1 = true;
bool threadPoolConfig = true;
bool V_nsa = false;
bool V_phy_test = false;
bool V_usim_test = false;


bool continuous_tx = false;
bool P_emulate_l1 = false;
bool P_ldpc_offload_flag = false;
bool P_chest_time = false;
bool p_chest_freq = false;
bool P_tune_offset = false;
bool P_sl_mode = false;
// bool P_emulate_l1 = false;



int load_config_sharedlib(configmodule_interface_t *cfgptr) {
  void *lib_handle;
  char fname[128];
  char libname[FILENAME_MAX];
  int st;
  st=0;
  sprintf(libname,CONFIG_SHAREDLIBFORMAT,cfgptr->cfgmode);
  lib_handle = dlopen(libname, RTLD_LAZY | RTLD_GLOBAL | RTLD_NODELETE);

  if (!lib_handle) {
    fprintf(stderr,"[CONFIG] %s %d Error calling dlopen(%s): %s\n",__FILE__, __LINE__, libname,dlerror());
    st = -1;
  } else {
    sprintf (fname,"config_%s_init",cfgptr->cfgmode);
    cfgptr->init = dlsym(lib_handle,fname);

    if (cfgptr->init == NULL ) {
      // printf("[CONFIG] %s %d no function %s for config mode %s\n",
      //        __FILE__, __LINE__,fname, cfgptr->cfgmode);
    } else {
      st = cfgptr->init(cfgptr);
      // printf("[CONFIG] function %s returned %i\n",
      //        fname, st);
    }

    sprintf (fname,"config_%s_get",cfgptr->cfgmode);
    cfgptr->get = dlsym(lib_handle,fname);

    if (cfgptr->get == NULL ) {
      // printf("[CONFIG] %s %d no function %s for config mode %s\n",
      //        __FILE__, __LINE__,fname, cfgptr->cfgmode);
      st = -1;
    }

    sprintf (fname,"config_%s_getlist",cfgptr->cfgmode);
    cfgptr->getlist = dlsym(lib_handle,fname);

    if (cfgptr->getlist == NULL ) {
      // printf("[CONFIG] %s %d no function %s for config mode %s\n",
      //        __FILE__, __LINE__,fname, cfgptr->cfgmode);
      st = -1;
    }

    if (cfgptr->rtflags & CONFIG_SAVERUNCFG) {
      sprintf(fname, "config_%s_set", cfgptr->cfgmode);
      cfgptr->set = dlsym(lib_handle, fname);

      if (cfgptr->set == NULL) {
        // printf("[CONFIG] %s %d no function %s for config mode %s\n", __FILE__, __LINE__, fname, cfgptr->cfgmode);
        st = -1;
      }
      sprintf(fname, "config_%s_write_parsedcfg", cfgptr->cfgmode);
      cfgptr->write_parsedcfg = dlsym(lib_handle, fname);

      if (cfgptr->write_parsedcfg == NULL) {
        // printf("[CONFIG] %s %d no function %s for config mode %s\n", __FILE__, __LINE__, fname, cfgptr->cfgmode);
      }
    }

    sprintf (fname,"config_%s_end",cfgptr->cfgmode);
    cfgptr->end = dlsym(lib_handle,fname);

    if (cfgptr->end == NULL) {
      // printf("[CONFIG] %s %d no function %s for config mode %s\n",
      //        __FILE__, __LINE__,fname, cfgptr->cfgmode);
    }
  }

  return st;
}


/*-----------------------------------------------------------------------------------*/
/* from here: interface implementtion of the configuration module */
int nooptfunc(void) {
  return 0;
};

int config_cmdlineonly_getlist(configmodule_interface_t *cfg,
                               paramlist_def_t *ParamList,
                               paramdef_t *params,
                               int numparams,
                               char *prefix)
{
  ParamList->numelt = 0;
  return 0;
}

int config_cmdlineonly_get(configmodule_interface_t *cfg, paramdef_t *cfgoptions, int numoptions, char *prefix)
{
  int defval;
  int fatalerror=0;
  int numdefvals=0;

  for(int i=0; i<numoptions; i++) {
    defval=0;

    switch(cfgoptions[i].type) {
      case TYPE_STRING:
        defval = config_setdefault_string(cfg, &cfgoptions[i], prefix);
        break;

      case TYPE_STRINGLIST:
        defval = config_setdefault_stringlist(cfg, &cfgoptions[i], prefix);
        break;

      case TYPE_UINT8:
      case TYPE_INT8:
      case TYPE_UINT16:
      case TYPE_INT16:
      case TYPE_UINT32:
      case TYPE_INT32:
      case TYPE_MASK:
        defval = config_setdefault_int(cfg, &cfgoptions[i], prefix);
        break;

      case TYPE_UINT64:
      case TYPE_INT64:
        defval = config_setdefault_int64(cfg, &cfgoptions[i], prefix);
        break;

      case TYPE_UINTARRAY:
      case TYPE_INTARRAY:
        defval = config_setdefault_intlist(cfg, &cfgoptions[i], prefix);
        break;

      case TYPE_DOUBLE:
        defval = config_setdefault_double(cfg, &cfgoptions[i], prefix);
        break;

      case TYPE_IPV4ADDR:
        defval = config_setdefault_ipv4addr(cfg, &cfgoptions[i], prefix);
        break;

      default:
        fprintf(stderr,"[CONFIG] %s.%s type %i not supported\n",prefix, cfgoptions[i].optname,cfgoptions[i].type);
        fatalerror=1;
        break;
    } /* switch on param type */

    if (defval == 1) {
      numdefvals++;
      cfgoptions[i].paramflags = cfgoptions[i].paramflags |  PARAMFLAG_PARAMSETDEF;
    }
  } /* for loop on options */

  // printf_params(cfg,
  //               "[CONFIG] %s: %i/%i parameters successfully set \n",
  //               prefix == NULL ? "(root)" : prefix,
  //               numdefvals,
  //               numoptions);

  if (fatalerror == 1) {
    fprintf(stderr,"[CONFIG] fatal errors found when assigning %s parameters \n",
            prefix);
  }

  return numdefvals;
}

configmodule_interface_t *load_configmodule(int argc,
					    char **argv,
					    uint32_t initflags)
{
  char *cfgparam=NULL;
  char *modeparams=NULL;
  char *cfgmode=NULL;
  char *strtokctx=NULL;
  char *atoken;
  uint32_t tmpflags=0;
  int i;
  int OoptIdx=-1;
  int OWoptIdx = -1;


  // Iterate over the command-line arguments
    for (i = 0; i < argc; i++) {
        // If the argument looks like a help flag
        if (strstr(argv[i], "help_config") != NULL) {
            config_printhelp(Config_Params, sizeofArray(Config_Params), CONFIG_SECTIONNAME);
            exit(0);
        }

        if ((strcmp(argv[i] + 1, "h") == 0) || (strstr(argv[i] + 1, "help_") != NULL)) {
            tmpflags = CONFIG_HELP;
        }
    }

       // Validate that the configuration file is provided as the first argument after the binary
    if (argc < 2 || argv[1][0] == '-') { // No argument or first argument is a flag
        fprintf(stderr, "Error: A configuration file must be provided immediately after the binary.\n");
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Assign the configuration file path from argv[1]
    cfgparam = argv[1];

        // Check if the configuration file has a valid .yml or .yaml extension
    if (!(strstr(cfgparam, ".yml") == (cfgparam + strlen(cfgparam) - 4) || 
          strstr(cfgparam, ".yaml") == (cfgparam + strlen(cfgparam) - 5))) {
        fprintf(stderr, "Error: The configuration file must have a .yml or .yaml extension.\n");
        exit(EXIT_FAILURE);
    }


      if ( cfgparam == NULL ) {
    cfgparam = getenv("OAI_CONFIGMODULE");
  }

  /* default different for UE and softmodem because UE may run without config file */
  /* and -O option is not mandatory for UE                                    */
  /* phy simulators behave as UE                                              */
  if (cfgparam == NULL) {
    tmpflags = tmpflags | CONFIG_NOOOPT;

    if ( initflags & CONFIG_ENABLECMDLINEONLY) {
      cfgparam = CONFIG_CMDLINEONLY ":dbgl0" ;
    } else {
      cfgparam = CONFIG_CMDLINEONLY ":dbgl0" ;
      cfgparam = CONFIG_LIBCONFIGFILE ":" DEFAULT_CFGFILENAME;
    }
  }

  /* parse the config parameters to set the config source */
  i = sscanf(cfgparam,"%m[^':']:%ms",&cfgmode,&modeparams);

  if (i< 0) {
    //fprintf(stderr,"[CONFIG] %s, %d, sscanf error parsing config source  %s: %s\n", __FILE__, __LINE__,cfgparam, strerror(errno));
    exit(-1) ;
  } else if ( i == 1 ) {
    /* -O argument doesn't contain ":" separator, assume -O <conf file> option, default cfgmode to libconfig
       with one parameter, the path to the configuration file cfgmode must not be NULL */
    modeparams = cfgmode;
    if (strstr(modeparams, ".yaml") != NULL || strstr(modeparams, ".yml") != NULL) {
      cfgmode = strdup("yaml");
    } else {
      cfgmode = strdup(CONFIG_LIBCONFIGFILE);
    }
  }
  static configmodule_interface_t *cfgptr;
  if (cfgptr)
    fprintf(stderr, "ERROR: Call load_configmodule more than one time\n");

  // The macros are not thread safe print_params and similar
  cfgptr = calloc(sizeof(configmodule_interface_t), 1);
  /* argv_info is used to memorize command line options which have been recognized */
  /* and to detect unrecognized command line options which might have been specified */
    cfgptr->argv_info = calloc(sizeof(int32_t), argc+10);
  /* argv[0] is the exec name, always Ok */
    cfgptr->argv_info[0] |= CONFIG_CMDLINEOPT_PROCESSED;

    /* When reuested _(_--OW or rtflag is 5), a file with config parameters, as defined after all processing, will be created */
    if (OWoptIdx >= 0) {
      cfgptr->argv_info[OWoptIdx] |= CONFIG_CMDLINEOPT_PROCESSED;
      cfgptr->rtflags |= CONFIG_SAVERUNCFG;
    }
  /* when OoptIdx is >0, -O option has been detected at position OoptIdx 
   *  we must memorize arv[OoptIdx is Ok                                  */ 
    if (OoptIdx >= 0) {
      cfgptr->argv_info[OoptIdx] |= CONFIG_CMDLINEOPT_PROCESSED;
      cfgptr->argv_info[OoptIdx+1] |= CONFIG_CMDLINEOPT_PROCESSED;
    }

    cfgptr->rtflags = cfgptr->rtflags | tmpflags;
    cfgptr->argc   = argc;
    cfgptr->argv   = argv;
    cfgptr->cfgmode=strdup(cfgmode);
    cfgptr->num_cfgP=0;
    atoken=strtok_r(modeparams,":",&strtokctx);

    while ( cfgptr->num_cfgP< CONFIG_MAX_OOPT_PARAMS && atoken != NULL) {
    /* look for debug level in the config parameters, it is common to all config mode
       and will be removed from the parameter array passed to the shared module */
      char *aptr;
      aptr=strcasestr(atoken,"dbgl");

      if (aptr != NULL) {
        cfgptr->rtflags = cfgptr->rtflags | strtol(aptr+4,NULL,0);
      } else {
        cfgptr->cfgP[cfgptr->num_cfgP] = strdup(atoken);
        cfgptr->num_cfgP++;
      }

      atoken = strtok_r(NULL,":",&strtokctx);
    }

    for (i = 0; i < cfgptr->num_cfgP; i++) {
      /* check if that file actually exists */
      if (access(cfgptr->cfgP[i], F_OK) != 0) {
        fprintf(stderr, "error: file %s does not exist\n", cfgptr->cfgP[i]);
        for (int j = 0; j < cfgptr->num_cfgP; ++j)
            free(cfgptr->cfgP[j]);
            free(modeparams);
            free(cfgptr->cfgmode);
            free(cfgptr->argv_info);
            free(cfgptr);
        if (cfgmode != NULL)
          free(cfgmode);
        return NULL;
      }
    }

    if (cfgptr->rtflags & CONFIG_PRINTPARAMS) {
        cfgptr->status = malloc(sizeof(configmodule_status_t));
    }


#if defined ARIA_DU

        i = YamlVNFGlobal(&cfgptr->yaml_config, cfgptr->cfgP[0]);
        

        if (i == 0) {
            int idx = config_paramidx_fromname(Config_Params, sizeofArray(Config_Params), CONFIGP_DEBUGFLAGS);
            Config_Params[idx].uptr = &(cfgptr->rtflags);
            idx = config_paramidx_fromname(Config_Params, sizeofArray(Config_Params), CONFIGP_TMPDIR);
            Config_Params[idx].strptr = &(cfgptr->tmpdir);
        } else {
            cfgptr->rtflags = cfgptr->rtflags | CONFIG_HELP | CONFIG_ABORT;
        }
#elif defined ARIA_CU
        
        i = GLOBALCUConfs(yaml_config, cfgptr->cfgP[0]);

        if (i == 0) {
            int idx = config_paramidx_fromname(Config_Params, sizeofArray(Config_Params), CONFIGP_DEBUGFLAGS);
            Config_Params[idx].uptr = &(cfgptr->rtflags);
            idx = config_paramidx_fromname(Config_Params, sizeofArray(Config_Params), CONFIGP_TMPDIR);
            Config_Params[idx].strptr = &(cfgptr->tmpdir);
        } else {
            fprintf(stderr, "[CONFIG] %s %d config module for VNF couldn't be loaded\n", __FILE__, __LINE__);
            cfgptr->rtflags = cfgptr->rtflags | CONFIG_HELP | CONFIG_ABORT;
        }
#else
        exit(EXIT_FAILURE);
#endif

  if (modeparams != NULL) free(modeparams);

  if (cfgmode != NULL) free(cfgmode);

  if (CONFIG_ISFLAGSET(CONFIG_ABORT)) {
    config_printhelp(Config_Params, sizeofArray(Config_Params), CONFIG_SECTIONNAME);
    //       exit(-1);
  }

  return cfgptr;
}


//----------------------------------------------------------------------------------------------------------------------


void LoadFhiConfigs(yaml_parser_t *parser, FHIConfig_t *fhi_72) {
    yaml_event_t event, map_event;
    int i = 0;

  //  printf("Parsing FHI Config...\n");

    while (1) {
        yaml_parser_parse(parser, &event);
        if (event.type == YAML_MAPPING_END_EVENT) {
            break;  // End of the mapping
        }

        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            // printf("Parsing key: %s\n", key);

            yaml_parser_parse(parser, &map_event);  // Parse the value or nested structure

            if (strcmp(key, "dpdkDeviceList") == 0) {
                int j = 0;
                if (map_event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break;
                        }
                        if (event.type == YAML_SCALAR_EVENT) {
                            global_configs.fhi_72.dpdk_devices[j] = strdup((char *)event.data.scalar.value);
                            j++;
                            global_configs.fhi_72.num_dpdk_devs++;
                        }
                        yaml_event_delete(&event);
                    }
                }
            } else if (strcmp(key, "dpdkCoreIsolation") == 0) {
                global_configs.fhi_72.system_core = atoi((char *)map_event.data.scalar.value);
               // printf("  system_core: %d\n", global_configs.fhi_72.system_core);
            } else if (strcmp(key, "fhiCoreIsolation") == 0) {
                global_configs.fhi_72.io_core = atoi((char *)map_event.data.scalar.value);
               // printf("  io_core: %d\n", global_configs.fhi_72.io_core);
            } else if (strcmp(key, "fhiWorkerCoreIsolation") == 0) {
                int j = 0;
                if (map_event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break;
                        }
                        if (event.type == YAML_SCALAR_EVENT) {
                            global_configs.fhi_72.worker_cores[j] = atoi((char *)event.data.scalar.value);
                            j++;
                        }
                        yaml_event_delete(&event);
                    }
                }
            } else if (strcmp(key, "duAddressList") == 0) {
                int j = 0;
                if (map_event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break;
                        }
                        if (event.type == YAML_SCALAR_EVENT) {
                            global_configs.fhi_72.du_addr[j] = strdup((char *)event.data.scalar.value);
                            j++;
                            global_configs.fhi_72.num_duAddr++;
                        }
                        yaml_event_delete(&event);
                    }
                }
            }
            else if (strcmp(key, "ruAddressList") == 0) {
                int j = 0;
                if (map_event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break;
                        }
                        if (event.type == YAML_SCALAR_EVENT) {
                            global_configs.fhi_72.ru_addr[j] = strdup((char *)event.data.scalar.value);
                            j++;
                            global_configs.fhi_72.num_ruAddr++;
                        }
                        yaml_event_delete(&event);
                    }
                }
            }
            else if (strcmp(key, "mtuForRu") == 0) {
                global_configs.fhi_72.mtu = atoi((char *)map_event.data.scalar.value);
            }

            if (strcmp(key, "fhTimingConfig") == 0) {
                global_configs.fhi_72.num_fh_config++;
                if (map_event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break;
                        }
                        if (event.type == YAML_MAPPING_START_EVENT) {
                            while (1) {
                                yaml_parser_parse(parser, &map_event);
                                if (map_event.type == YAML_MAPPING_END_EVENT) {
                                    break;
                                }
                                if (map_event.type == YAML_SCALAR_EVENT) {
                                    char *subkey = (char *)map_event.data.scalar.value;
                                    yaml_parser_parse(parser, &map_event);  // Parse the value

                                    if (strcmp(subkey, "Tadv_CP_DL") == 0) {
                                        global_configs.fhi_72.fh_config.Tadv_cp_dl = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T2a_CP_DL_Min") == 0) {
                                        global_configs.fhi_72.fh_config.T2a_cp_dl.min = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T2a_CP_DL_Max") == 0) {
                                        global_configs.fhi_72.fh_config.T2a_cp_dl.max = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T2a_CP_UL_Min") == 0) {
                                        global_configs.fhi_72.fh_config.T2a_cp_ul.min = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T2a_CP_UL_Max") == 0) {
                                        global_configs.fhi_72.fh_config.T2a_cp_ul.max = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T2a_UP_Min") == 0) {
                                        global_configs.fhi_72.fh_config.T2a_up.min = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T2a_UP_Max") == 0) {
                                        global_configs.fhi_72.fh_config.T2a_up.max = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "Ta3_Min") == 0) {
                                        global_configs.fhi_72.fh_config.Ta3.min = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "Ta3_Max") == 0) {
                                        global_configs.fhi_72.fh_config.Ta3.max = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T1a_CP_DL_Min") == 0) {
                                        global_configs.fhi_72.fh_config.T1a_cp_dl.min = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T1a_CP_DL_Max") == 0) {
                                        global_configs.fhi_72.fh_config.T1a_cp_dl.max = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T1a_CP_UL_Min") == 0) {
                                        global_configs.fhi_72.fh_config.T1a_cp_ul.min = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T1a_CP_UL_Max") == 0) {
                                        global_configs.fhi_72.fh_config.T1a_cp_ul.max = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T1a_UP_Min") == 0) {
                                        global_configs.fhi_72.fh_config.T1a_up.min = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "T1a_UP_Max") == 0) {
                                        global_configs.fhi_72.fh_config.T1a_up.max = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "Ta4_Min") == 0) {
                                        global_configs.fhi_72.fh_config.Ta4.min = atoi((char *)map_event.data.scalar.value);
                                    } 
                                    else if (strcmp(subkey, "Ta4_Max") == 0) {
                                        global_configs.fhi_72.fh_config.Ta4.max = atoi((char *)map_event.data.scalar.value);
                                    }


                                    else if (strcmp(subkey, "iqBitWidth") == 0) {
                                        global_configs.fhi_72.ru_config.iq_width = atoi((char *)map_event.data.scalar.value);
                                    }
                                    else if (strcmp(subkey, "prachIqBitWidth") == 0) {
                                        global_configs.fhi_72.ru_config.iq_width_prach = atoi((char *)map_event.data.scalar.value);
                                    }
                                    else if (strcmp(subkey, "fftSize") == 0) {
                                        global_configs.fhi_72.ru_config.fft_size = atoi((char *)map_event.data.scalar.value);
                                    }
                                    else if (strcmp(subkey, "eAxCOffset") == 0) {
                                        global_configs.fhi_72.prach_config.eAxC_offset = atoi((char *)map_event.data.scalar.value);
                                    }
                                    else if (strcmp(subkey, "kBar") == 0) {
                                        global_configs.fhi_72.prach_config.kbar = atoi((char *)map_event.data.scalar.value);
                                    }
                                }
                            }
                        }
                    }
                    yaml_event_delete(&event);
                }
            }
        }
    }
    yaml_event_delete(&event);
}

void HorizontalLogLevel(char* string,int* value){
    
    if (strcmp(string,"true")==0){
        *value = 1;
    }
    else if (strcmp(string,"false")==0){
        *value = 0;
    }
    else {
        *value = 0;
    }
}

void fillLogConfigs(yaml_parser_t *parser, LogConfig *logConfig) {
    yaml_event_t event;
    char key[256] = {0};

    logConfig->vertical_level = 0;
    logConfig->horizontal_level.PDCP = 0;
    logConfig->horizontal_level.SCTP = 0;
    logConfig->horizontal_level.F1AP = 0;
    logConfig->horizontal_level.RRC = 0;
    logConfig->horizontal_level.NGAP = 0;
    logConfig->horizontal_level.GTPU = 0;
    logConfig->horizontal_level.E1AP = 0;
    logConfig->horizontal_level.RLC = 0;
    logConfig->horizontal_level.CU_APP = 0;
    logConfig->horizontal_level.MAC = 0;
    logConfig->horizontal_level.PHY = 0;
    logConfig->horizontal_level.ARIA = 0;
    logConfig->horizontal_level.X2AP = 0;
    logConfig->horizontal_level.P5_INFO_LEVEL = 0;
    logConfig->horizontal_level.P7_INFO_LEVEL = 0;
    logConfig->horizontal_level.SDAP = 0;
    logConfig->horizontal_level.GNB_APP = 0;
    logConfig->print_datetime = 0;

    while (yaml_parser_parse(parser, &event)) {
        if (event.type == YAML_SCALAR_EVENT) {
            strncpy(key, (char *)event.data.scalar.value, sizeof(key) - 1);

            yaml_parser_parse(parser, &event);
            if (event.type == YAML_SCALAR_EVENT) {
                char* level = strdup((char *)event.data.scalar.value);
                int value = 0;

                // Map the key to the appropriate structure field
                if (strcmp(key, "verticalLevel") == 0) {
                    if (strcmp((char *)event.data.scalar.value, "LOG_DEBUG") == 0) {
                        global_vnf.logConfigs.vertical_level = LOG_DEBUG; // LOG_DEBUG = 2
                    } else if (strcmp((char *)event.data.scalar.value, "LOG_INFO") == 0) {
                        global_vnf.logConfigs.vertical_level = LOG_INFO; // LOG_INFO = 1
                    } else if (strcmp((char *)event.data.scalar.value, "LOG_WARN") == 0) {
                        global_vnf.logConfigs.vertical_level = LOG_WARN; // LOG_NONE = 0
                    } else if (strcmp((char *)event.data.scalar.value, "LOG_NONE") == 0) {
                        global_vnf.logConfigs.vertical_level = LOG_NONE; // LOG_NONE = 0
                    }else {
                        printf("Invalid Vertical_Level value: %s\n", (char *)event.data.scalar.value);
                    }
                } else if (strcmp(key, "PDCP") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.PDCP = value;
                } else if (strcmp(key, "SCTP") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.SCTP = value;
                } else if (strcmp(key, "F1AP") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.F1AP = value;
                } else if (strcmp(key, "RRC") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.RRC = value;
                } else if (strcmp(key, "NGAP") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.NGAP = value;
                } else if (strcmp(key, "GTPU") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.GTPU = value;
                } else if (strcmp(key, "E1AP") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.E1AP = value;
                } else if (strcmp(key, "RLC") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.RLC = value;
                } else if (strcmp(key, "CU_APP") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.CU_APP = value;
                } else if (strcmp(key, "MAC") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.MAC = value;
                } else if (strcmp(key, "PHY") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.PHY = value;
                } else if (strcmp(key, "ARIA") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.ARIA = value;
                } else if (strcmp(key, "X2AP") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.X2AP = value;
                } else if (strcmp(key, "P5_INFO_LEVEL") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.P5_INFO_LEVEL = value;
                } else if (strcmp(key, "P7_INFO_LEVEL") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.P7_INFO_LEVEL = value;
                } else if (strcmp(key, "SDAP") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.SDAP = value;
                } else if (strcmp(key, "GNB_APP") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.GNB_APP = value;
                } else if (strcmp(key, "DTLS") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.horizontal_level.DTLS = value;
                } else if (strcmp(key, "printTimeStamp") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.print_datetime = value;
                } else if (strcmp(key, "Print_Configurations") == 0) {
                    HorizontalLogLevel((char *)event.data.scalar.value,&value);
                    global_vnf.logConfigs.print_configurations = value;
                }  else {
                    printf("Unknown key: %s\n", key);
                }
            }
        } else if (event.type == YAML_MAPPING_END_EVENT) {
            break;  // End of the mapping
        }

        yaml_event_delete(&event);
    }
}


void fillLogFileConfigs(yaml_parser_t *parser, LogFileConfig *logFileConfig) {
    yaml_event_t event;
    char key[256] = {0};

    logFileConfig->vertical_level = 4;
    logFileConfig->horizontal_level.PDCP = 0;
    logFileConfig->horizontal_level.SCTP = 0;
    logFileConfig->horizontal_level.F1AP = 0;
    logFileConfig->horizontal_level.RRC = 0;
    logFileConfig->horizontal_level.NGAP = 0;
    logFileConfig->horizontal_level.GTPU = 0;
    logFileConfig->horizontal_level.E1AP = 0;
    logFileConfig->horizontal_level.RLC = 0;
    logFileConfig->horizontal_level.CU_APP = 0;
    logFileConfig->horizontal_level.MAC = 0;
    logFileConfig->horizontal_level.PHY = 0;
    logFileConfig->horizontal_level.ARIA = 0;
    logFileConfig->horizontal_level.X2AP = 0;
    logFileConfig->horizontal_level.P5_INFO_LEVEL = 0;
    logFileConfig->horizontal_level.P7_INFO_LEVEL = 0;
    logFileConfig->horizontal_level.SDAP = 0;
    logFileConfig->horizontal_level.GNB_APP = 0;
    logFileConfig->print_datetime = 0;
    logFileConfig->print_configurations = 0;
    logFileConfig->generate_log_file = 0;

    while (yaml_parser_parse(parser, &event)) {
        if (event.type == YAML_SCALAR_EVENT) {
            // Copy the key name
            strncpy(key, (char *)event.data.scalar.value, sizeof(key) - 1);

            // Parse the value associated with the key
            yaml_parser_parse(parser, &event);
            if (event.type == YAML_SCALAR_EVENT) {
                char* level = strdup((char *)event.data.scalar.value);
                int value = 0;

                if (strcmp(level, "true") == 0) {
                    value = 1;
                } else if (strcmp(level, "false") == 0) {
                    value = 0;
                } else {
                  //  printf("Invalid value for level: %s\n", level);
                }
                // Map the key to the appropriate structure field
                if (strcmp(key, "verticalLevel") == 0) {
                    if (strcmp((char *)event.data.scalar.value, "LOG_DEBUG") == 0) {
                        global_vnf.logFileConfigs.vertical_level = LOG_DEBUG; // LOG_DEBUG = 2
                    } else if (strcmp((char *)event.data.scalar.value, "LOG_INFO") == 0) {
                        global_vnf.logFileConfigs.vertical_level = LOG_INFO; // LOG_INFO = 1
                    } else if (strcmp((char *)event.data.scalar.value, "LOG_WARN") == 0) {
                        global_vnf.logFileConfigs.vertical_level = LOG_WARN; // LOG_NONE = 0
                    } else if (strcmp((char *)event.data.scalar.value, "LOG_NONE") == 0) {
                        global_vnf.logFileConfigs.vertical_level = LOG_NONE; // LOG_NONE = 0
                    }else {
                        printf("Invalid Vertical_Level value: %s\n", (char *)event.data.scalar.value);
                    }
                } else if (strcmp(key, "PDCP") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.PDCP = value;
                } else if (strcmp(key, "SCTP") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.SCTP = value;
                } else if (strcmp(key, "F1AP") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.F1AP = value;
                } else if (strcmp(key, "RRC") == 0) {
                    global_vnf.logConfigs.horizontal_level.RRC = value;
                } else if (strcmp(key, "NGAP") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.NGAP = value;
                } else if (strcmp(key, "GTPU") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.GTPU = value;
                } else if (strcmp(key, "E1AP") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.E1AP = value;
                } else if (strcmp(key, "RLC") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.RLC = value;
                } else if (strcmp(key, "CU_APP") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.CU_APP = value;
                } else if (strcmp(key, "MAC") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.MAC = value;
                } else if (strcmp(key, "PHY") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.PHY = value;
                } else if (strcmp(key, "ARIA") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.ARIA = value;
                } else if (strcmp(key, "X2AP") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.X2AP = value;
                } else if (strcmp(key, "P5_INFO_LEVEL") == 0) {
                    global_vnf.logConfigs.horizontal_level.P5_INFO_LEVEL = value;
                } else if (strcmp(key, "P7_INFO_LEVEL") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.P7_INFO_LEVEL = value;
                } else if (strcmp(key, "SDAP") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.SDAP = value;
                } else if (strcmp(key, "GNB_APP") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.GNB_APP = value;
                } else if (strcmp(key, "DTLS") == 0) {
                    global_vnf.logFileConfigs.horizontal_level.DTLS = value;
                } else if (strcmp(key, "printTimeStamp") == 0) {
                    global_vnf.logFileConfigs.print_datetime = value;
                } else {
                    printf("Unknown key: %s\n", key);
                }
            }
        } else if (event.type == YAML_MAPPING_END_EVENT) {
            break;  // End of the mapping
        }

        yaml_event_delete(&event);
    }
}


void LoadRUConfigs(yaml_parser_t *parser) {
    yaml_event_t event;
    int bf_weights_index = 0;

    //printf("testing--> inside LoadRUConfigs\n");
    int tpc_cores_index = 0;

    while (1) {
        yaml_parser_parse(parser, &event);
        if (event.type == YAML_MAPPING_END_EVENT) {
            break;  // End of the mapping
        }

        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            // printf("Parsing key: %s\n", key);
            yaml_parser_parse(parser, &event);  // Parse the value

           
            if (strcmp(key, "slotAhead") == 0) {
                global_configs.RUs.sl_ahead = atoi((char *)event.data.scalar.value);
                // printf("Parsed sl_ahead: %d\n", global_configs.RUs.sl_ahead);
            } else if (strcmp(key, "numTransmitAntennas") == 0) {
                global_configs.RUs.nb_tx = atoi((char *)event.data.scalar.value);
            //    printf("Parsed nb_tx: %d\n", global_configs.RUs.nb_tx);
            } else if (strcmp(key, "numReceiveAntennas") == 0) {
                global_configs.RUs.nb_rx = atoi((char *)event.data.scalar.value);
            //    printf("Parsed nb_rx: %d\n", global_configs.RUs.nb_rx);
            } else if (strcmp(key, "transmitAttenuation") == 0) {
                global_configs.RUs.att_tx = atoi((char *)event.data.scalar.value);
                // printf("Parsed att_tx: %d\n", global_configs.RUs.att_tx);
            } else if (strcmp(key, "receiveAttenuation") == 0) {
                global_configs.RUs.att_rx = atoi((char *)event.data.scalar.value);
            //    printf("Parsed att_rx: %d\n", global_configs.RUs.att_rx);
            } else if (strcmp(key, "frequencyBands") == 0) {
                global_configs.RUs.bands = atoi((char *)event.data.scalar.value);
            //    printf("Parsed bands: %d\n", global_configs.RUs.bands);
            } else if (strcmp(key, "maxPdschReferenceSignalPower") == 0) {
                global_configs.RUs.max_pdschReferenceSignalPower = atoi((char *)event.data.scalar.value);
            //    printf("Parsed max_pdschReferenceSignalPower: %d\n", global_configs.RUs.max_pdschReferenceSignalPower);
            } else if (strcmp(key, "maxRxGain") == 0) {
                global_configs.RUs.max_rxgain = atoi((char *)event.data.scalar.value);
                // printf("Parsed max_rxgain: %d\n", global_configs.RUs.max_rxgain);
            } else if (strcmp(key, "slotFrameExtension") == 0) {
                global_configs.RUs.sf_extension = atoi((char *)event.data.scalar.value);
                // printf("Parsed sf_extension: %d\n", global_configs.RUs.sf_extension);
            } 
            // else if (strcmp(key, "eNBInstanceCount") == 0) {
            //     global_configs.RUs.eNB_instances = atoi((char *)event.data.scalar.value);
            //     //printf("Parsed eNB_instances: %d\n", global_configs.RUs.eNB_instances);
            // } 
            else if (strcmp(key, "ruThreadCore") == 0) {
                global_configs.RUs.ru_thread_core = atoi((char *)event.data.scalar.value);
            //    printf("Parsed ru_thread_core: %d\n", global_configs.RUs.ru_thread_core);
            } else if (strcmp(key, "precodingEnabled") == 0) {
                global_configs.RUs.do_precoding = atoi((char *)event.data.scalar.value);
            //    printf("Parsed do_precoding: %d\n", global_configs.RUs.do_precoding);
            } else if (strcmp(key, "beamformingWeights") == 0) {
                global_configs.RUs.set_bfwts++;
               // printf("set_bfwts : %d\n",global_configs.RUs.set_bfwts);
                //printf("Parsing bf_weights...\n");
                yaml_event_t inner_event;
                int j = 0;

                // Parse the next event
                yaml_parser_parse(parser, &inner_event);

                // Handle if it's a scalar event (which is being treated as such)
                if (inner_event.type == YAML_SCALAR_EVENT) {
                    // Since it's a scalar, we can directly parse the values as hex
                    char *value_str = (char *)inner_event.data.scalar.value;
                    char *token = strtok(value_str, ",");  // Split by comma

                    // int j = 0;
                    while (token != NULL) {
                        global_configs.RUs.bf_weights[j] = strtoul(token, NULL, 16);
                    //    printf("bf_weights[%d]: 0x%x\n", j, global_configs.RUs.bf_weights[j]);
                        token = strtok(NULL, ",");  // Get next token
                        j++;
                        global_configs.RUs.num_bfwts++;

                    }
                   // printf("num_bfwts : %d\n",global_configs.RUs.num_bfwts);

                    //printf("Finished parsing bf_weights.\n");
                } else {
                    printf("Error: bf_weights is not a scalar (event type: %d).\n", inner_event.type);
                }

                // Clean up
                yaml_event_delete(&inner_event);  
            } 
            
            else if (strcmp(key, "TPCores") == 0) {
               // printf("Parsing TPCores...\n");
                yaml_event_t inner_event;

                // Parse the next event
                yaml_parser_parse(parser, &inner_event);

                // Handle if it's a scalar event (which is being treated as such)
                if (inner_event.type == YAML_SCALAR_EVENT) {
                    // Since it's a scalar, we can directly parse the values
                    char *value_str = (char *)inner_event.data.scalar.value;
                    char *token = strtok(value_str, ",");  // Split by comma

                    int tpc_cores_index = 0;
                    while (token != NULL) {
                        global_configs.RUs.tpc_cores[tpc_cores_index] = atoi(token);
                        printf("Parsed TPCores[%d]: %d\n", tpc_cores_index, global_configs.RUs.tpc_cores[tpc_cores_index]);
                        token = strtok(NULL, ",");  // Get next token
                        tpc_cores_index++;
                    }

                   // printf("Finished parsing TPCores.\n");
                } else {
                    printf("Error: TPCores is not a scalar (event type: %d).\n", inner_event.type);
                }

                // Clean up
                yaml_event_delete(&inner_event);  
            }
        }
        yaml_event_delete(&event);  // Clean up after every event
    }
    yaml_event_delete(&event);
}



void LoadL1Configs(yaml_parser_t *parser) {
    yaml_event_t event;

   // printf("testing--> inside LoadL1Configs\n");

    while (1) {
        yaml_parser_parse(parser, &event);
        if (event.type == YAML_MAPPING_END_EVENT) {
            break;  // End of the mapping
        }

        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            // printf("Parsing key: %s\n", key);
            yaml_parser_parse(parser, &event);  // Parse the value


            // Map the key to corresponding log level fields
            if (strcmp(key, "numComponentCarriers") == 0) {
                global_configs.L1s.num_cc = atoi((char *)event.data.scalar.value);
            //    printf("Parsed num_cc: %d\n", global_configs.L1s.num_cc);
            } else if (strcmp(key, "prachDtxThreshold") == 0) {
                global_configs.L1s.prach_dtx_threshold = atoi((char *)event.data.scalar.value);
            //    printf("Parsed prach_dtx_threshold: %d\n", global_configs.L1s.prach_dtx_threshold);
            } else if (strcmp(key, "pucch0DtxThreshold") == 0) {
                global_configs.L1s.pucch0_dtx_threshold = atoi((char *)event.data.scalar.value);
            //    printf("Parsed pucch0_dtx_threshold: %d\n", global_configs.L1s.pucch0_dtx_threshold);
            } else if (strcmp(key, "puschDtxThreshold") == 0) {
                global_configs.L1s.pusch_dtx_threshold = atoi((char *)event.data.scalar.value);
                // printf("Parsed pusch_dtx_threshold: %d\n", global_configs.L1s.pusch_dtx_threshold);
            }
            //  else if (strcmp(key, "maxLdpcIterations") == 0) {
            //     global_configs.L1s.max_ldpc_iterations = atoi((char *)event.data.scalar.value);
            //     //printf("Parsed max_ldpc_iterations: %d\n", global_configs.L1s.max_ldpc_iterations);
            // }
             else if (strcmp(key, "txAmplifierBackoff") == 0) {
                global_configs.L1s.tx_amp_backoff_dB = atoi((char *)event.data.scalar.value);
            //    printf("Parsed tx_amp_backoff_dB: %d\n", global_configs.L1s.tx_amp_backoff_dB);
            } else if (strcmp(key, "rxThreadCore") == 0) {
                global_configs.L1s.L1_rx_thread_core = atoi((char *)event.data.scalar.value);
                // printf("Parsed L1_rx_thread_core: %d\n", global_configs.L1s.L1_rx_thread_core);
            } else if (strcmp(key, "txThreadCore") == 0) {
                global_configs.L1s.L1_tx_thread_core = atoi((char *)event.data.scalar.value);
                // printf("Parsed L1_tx_thread_core: %d\n", global_configs.L1s.L1_tx_thread_core);
            } else if (strcmp(key, "phaseCompensationEnabled") == 0) {
                global_configs.L1s.phase_compensation = atoi((char *)event.data.scalar.value);
                // printf("Parsed phase_compensation: %d\n", global_configs.L1s.phase_compensation);
            // } else if (strcmp(key, "localNetworkInterfaceName") == 0) {
            //     global_configs.L1s.local_n_if_name = strdup((char *)event.data.scalar.value);
            //     printf("Parsed local_n_if_name: %s\n", global_configs.L1s.local_n_if_name);
            // } else if (strcmp(key, "remoteNetworkAddress") == 0) {
            //     global_configs.L1s.remote_n_address = strdup((char *)event.data.scalar.value);
            //     printf("Parsed remote_n_address: %s\n", global_configs.L1s.remote_n_address);
            // } else if (strcmp(key, "localNetworkAddress") == 0) {
            //     global_configs.L1s.local_n_address = NULL;
            //     printf("Parsed local_n_address: %s\n", global_configs.L1s.local_n_address);
            // } else if (strcmp(key, "localNetworkPortC") == 0) {
            //     global_configs.L1s.local_n_portc = atoi((char *)event.data.scalar.value);
            //     printf("Parsed local_n_portc: %d\n", global_configs.L1s.local_n_portc);
            // } else if (strcmp(key, "remote_n_portc") == 0) {
            //     global_configs.L1s.remote_n_portc = atoi((char *)event.data.scalar.value);
            //     printf("Parsed remote_n_portc: %d\n", global_configs.L1s.remote_n_portc);
            // } else if (strcmp(key, "local_n_portd") == 0) {
            //     global_configs.L1s.local_n_portd = atoi((char *)event.data.scalar.value);
            //     printf("Parsed local_n_portd: %d\n", global_configs.L1s.local_n_portd);
            // } else if (strcmp(key, "remote_n_portd") == 0) {
            //     global_configs.L1s.remote_n_portd = atoi((char *)event.data.scalar.value);
            //     printf("Parsed remote_n_portd: %d\n", global_configs.L1s.remote_n_portd);
            }

        }


        yaml_event_delete(&event);  // Clean up after every event
    }
    yaml_event_delete(&event);
}


void LoadMACRLCs(yaml_parser_t *parser) {
    yaml_event_t event;
    while (1) {
        yaml_parser_parse(parser, &event);
        if (event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&event);
            break;  // End of the PLMN mapping
        }
        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            yaml_parser_parse(parser, &event); // Move to value
            if (strcmp(key, "numComponentCarriers") == 0) {
                global_vnf.macrlcs->num_cc = atoi((char *)event.data.scalar.value);;
                //printf("num_cc: %d\n", global_vnf.macrlcs->num_cc);
            } 
            // else if (strcmp(key, "localInterfaceName") == 0) {
            //     global_vnf.macrlcs->local_s_if_name = strdup((char *)event.data.scalar.value);;
            //     //printf("local_s_if_name: %s\n", global_vnf.macrlcs->local_s_if_name);
            // }
            else if (strcmp(key, "cuIpAddress") == 0) {
                global_vnf.macrlcs->remote_s_address = strdup((char *)event.data.scalar.value);;
                // printf("remote_s_address: %s\n", global_vnf.macrlcs->remote_s_address);
            }
            else if (strcmp(key, "duIpAddress") == 0) {
                global_vnf.macrlcs->local_s_address = strdup((char *)event.data.scalar.value);;
                // printf("local_s_address: %s\n", global_vnf.macrlcs->local_s_address);
            }
            else if (strcmp(key, "duPort") == 0) {
                global_vnf.macrlcs->local_s_portc = atoi((char *)event.data.scalar.value);;
            //    printf("local_s_portc: %d\n", global_vnf.macrlcs->local_s_portc);
            }
            else if (strcmp(key, "cuPort") == 0) {
                global_vnf.macrlcs->remote_s_portc = atoi((char *)event.data.scalar.value);;
                // printf("remote_s_portc: %d\n", global_vnf.macrlcs->remote_s_portc);
            }
            // else if (strcmp(key, "localNetworkPortD") == 0) {
            //     global_vnf.macrlcs->local_s_portd = atoi((char *)event.data.scalar.value);;
            //     //printf("local_s_portd: %d\n", global_vnf.macrlcs->local_s_portd);
            // }
            // else if (strcmp(key, "remoteNetworkPortD") == 0) {
            //     global_vnf.macrlcs->remote_s_portd = atoi((char *)event.data.scalar.value);;
            //     //printf("remote_s_portd: %d\n", global_vnf.macrlcs->remote_s_portd);
            // }
            
        }
        yaml_event_delete(&event);  // Ensure to delete the event
    }
}



void LoadSCTPConfigs(yaml_parser_t *parser) {
    yaml_event_t event;
    while (1) {
        yaml_parser_parse(parser, &event);
        if (event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&event);
            break;  // End of the PLMN mapping
        }
        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            yaml_parser_parse(parser, &event); // Move to value
            if (strcmp(key, "SCTP_INSTREAMS") == 0) {
                global_vnf.SCTP.SCTP_INSTREAMS = atoi((char *)event.data.scalar.value);;
                //printf("maxSCTPOutStreams: %d\n", global_vnf.SCTP.SCTP_INSTREAMS);
            } 
            else if (strcmp(key, "SCTP_OUTSTREAMS") == 0) {
                global_vnf.SCTP.SCTP_OUTSTREAMS = atoi((char *)event.data.scalar.value);;
                //printf("SCTP_OUTSTREAMS: %d\n", global_vnf.SCTP.SCTP_OUTSTREAMS);
            }
        }
        yaml_event_delete(&event);  // Ensure to delete the event
    }
}


void LoadPLMNlist(yaml_parser_t *parser) {
    yaml_event_t event;
    // global_configs.numPlmn = 0;
    while (1) {
        yaml_parser_parse(parser, &event);
        if (event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&event);
            break;  // End of the PLMN mapping
        }
        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            yaml_parser_parse(parser, &event); // Move to value
            if (strcmp(key, "mcc") == 0) {
                // plmn->mcc = atoi((char *)event.data.scalar.value);
                global_vnf.plmn_list->mcc = atoi((char *)event.data.scalar.value);;
                GlobC_ARIAConfs.y_gNBs->p_plmn.MCC = atoi((char *)event.data.scalar.value);;
                //printf("PLMN MCC: %03d\n", global_vnf.plmn_list->mcc);
            } else if (strcmp(key, "mnc") == 0) {
                // plmn->mnc = atoi((char *)event.data.scalar.value);
                global_vnf.plmn_list->mnc = atoi((char *)event.data.scalar.value);;
                GlobC_ARIAConfs.y_gNBs->p_plmn.MNC = atoi((char *)event.data.scalar.value);;
                //printf("PLMN MNC: %02d\n", global_vnf.plmn_list->mnc);
                // printf("PLMN MNC: %d\n", plmn->mnc);
            } else if (strcmp(key, "mncLength") == 0) {
                global_vnf.plmn_list->mnc_length = atoi((char *)event.data.scalar.value);
                GlobC_ARIAConfs.y_gNBs->p_plmn.MNC_len = atoi((char *)event.data.scalar.value);
                //printf("PLMN MNC Length: %d\n", global_vnf.plmn_list->mnc_length);
            } else if (strcmp(key, "sst") == 0) {
                global_vnf.plmn_list->snssaiList->sst = atoi((char *)event.data.scalar.value);
                //printf("SNSSAI SST: %d\n", global_vnf.plmn_list->snssaiList->sst);
                global_vnf.numSNSSAIlist = 1;
                //global_configs.defvals.slice_d = 0xffffff;
            } 
            else if (strcmp(key, "sd") == 0) {
                global_configs.defvals.slice_d = (int) strtol((char *)event.data.scalar.value, NULL, 16);
               // printf("SNSSAI SD: 0x%X\n", global_configs.defvals.slice_d);

                global_vnf.numSNSSAIlist = 1;
                //global_configs.defvals.slice_d = 0xffffff;
            }

        }
        yaml_event_delete(&event);  // Ensure to delete the event
    }
}


void LoadgNode_B_DU(yaml_parser_t *parser) {
    yaml_event_t event;
    // int index = -1;  // For iterating through the gNB list
    //  bool inInitialDownlinkBWP = false;

    while (1) {
        yaml_parser_parse(parser, &event);

        if (event.type == YAML_MAPPING_END_EVENT) {
            break; // End of the current mapping
        }

        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            // printf("Parsing key: %s\n", key);
            yaml_parser_parse(parser, &event); // Parse the value

            // Top-level keys

            if (strcmp(key, "gNBName") == 0) {
                global_vnf.gNBs->gNB_name = strdup((char *)event.data.scalar.value);
                GlobC_ARIAConfs.y_gNBs->EntityLabel = strdup((char *)event.data.scalar.value);
                GlobC_ARIAConfs.FunctionalNodes = strdup((char *)event.data.scalar.value);
                SM_Log_Assert(_ARIA_, (strcmp(GlobC_ARIAConfs.FunctionalNodes, ARIA_L2_NAME) == 0), "Illegal File...!!");
                SM_Log_Assert(_ARIA_, (strcmp(global_vnf.gNBs->gNB_name, ARIA_L2_NAME) == 0), "Illegal File...!!");
                // printf("Parsed gNB_name: %s\n", global_vnf.gNBs->gNB_name);
            }
            else if (strcmp(key, "gNBId") == 0) {
                global_vnf.gNBs->gNB_ID = (uint32_t)strtol((char *)event.data.scalar.value, NULL, 16);
                // printf("Parsed gNB_ID: %x\n", global_vnf.gNBs->gNB_ID);
            } 
            else if (strcmp(key, "gNBDUId") == 0) {
                global_vnf.gNBs->gNB_DU_ID = (uint32_t)strtol((char *)event.data.scalar.value, NULL, 16);
                // printf("Parsed gNB_DU_ID: %u\n", global_vnf.gNBs->gNB_DU_ID);
            } 
            else if (strcmp(key, "trackingAreaCode") == 0) {
                global_vnf.gNBs->tracking_area_code = atoi((char *)event.data.scalar.value);
                GlobC_ARIAConfs.y_gNBs->TAC = atoi((char *)event.data.scalar.value);
                // printf("Parsed tracking_area_code: %d\n", global_vnf.gNBs->tracking_area_code);
            } 
            else if (strcmp(key, "nRCellIdentity") == 0) {
                global_vnf.gNBs->nr_cellid = atol((char *)event.data.scalar.value);
                // printf("Parsed nr_cellid: %ld\n", global_vnf.gNBs->nr_cellid);
            }

        }
        yaml_event_delete(&event);
        
    }
}


void LoadGNBConfigs(yaml_parser_t *parser) {
    yaml_event_t event;
    int index = -1;  // For iterating through the gNB list
     bool inInitialDownlinkBWP = false;

    while (1) {
        yaml_parser_parse(parser, &event);

        if (event.type == YAML_MAPPING_END_EVENT) {
            break; // End of the current mapping
        }

        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            yaml_parser_parse(parser, &event); // Parse the value

            // Top-level keys
            if (strcmp(key, "duCores") == 0) {
                // printf("Found duCores section\n");

                // Parse the next event
                // if (!yaml_parser_parse(&parser, &event)) {
                //     fprintf(stderr, "YAML parsing error\n");
                //     exit(EXIT_FAILURE);
                // }

                // // Debugging: Print event type
                // printf("Parsed event type: %d\n", event.type);

                // // Check if we got a scalar string
                // if (event.type == YAML_SCALAR_EVENT) {
                    // Copy the scalar string into the target variable
                    snprintf(global_configs.thread_pool_cores, MAX_CORES, "%s", (char *)event.data.scalar.value);
                    // printf("thread_pool_cores string in GNBfunction: %s\n", global_configs.thread_pool_cores);
                // } else {
                //     fprintf(stderr, "Error: Expected a scalar string for thread_pool_cores, but got event type: %d\n", event.type);
                // }

                // Free the event to avoid memory leaks
                yaml_event_delete(&event);
            }

            else if (strcmp(key, "puschAntennaPort") == 0) {
                global_vnf.gNBs->pusch_AntennaPorts = atoi((char *)event.data.scalar.value);
                //printf("Parsed pusch_AntennaPorts: %d\n", global_vnf.gNBs->pusch_AntennaPorts);
            } else if (strcmp(key, "pdschAntennaPortXP") == 0) {
                global_vnf.gNBs->pdsch_AntennaPorts_XP = atoi((char *)event.data.scalar.value);
                //printf("Parsed pusch_AntennaPorts: %d\n", global_vnf.gNBs->pusch_AntennaPorts);
            } else if (strcmp(key, "sib1TimeDomainAllocation") == 0) {
                global_vnf.gNBs->sib1_tda = atoi((char *)event.data.scalar.value);
                //printf("Parsed sib1_tda: %d\n", global_vnf.gNBs->sib1_tda);
            } else if (strcmp(key, "csirsEnabled") == 0) {
                global_vnf.gNBs->do_CSIRS = atoi((char *)event.data.scalar.value);
                // printf("Parsed do_CSIRS: %d\n", global_vnf.gNBs->sib1_tda);
            } else if (strcmp(key, "srsEnabled") == 0) {
                global_vnf.gNBs->do_SRS = atoi((char *)event.data.scalar.value);
                //printf("Parsed do_SRS: %d\n", global_vnf.gNBs->sib1_tda);
            }

            // Nested mappings
            else if (strcmp(key, "pdcchSIB1Configs") == 0) {
                while (1) {
                    yaml_parser_parse(parser, &event);
                    if (event.type == YAML_MAPPING_END_EVENT) break;

                    if (event.type == YAML_SCALAR_EVENT) {
                        char *subkey = (char *)event.data.scalar.value;
                        yaml_parser_parse(parser, &event); // Parse the value

                        if (strcmp(subkey, "controlResourceSetZero") == 0) {
                            global_vnf.gNBs->pdcchConfigSIB1->controlResourceSetZero =atoi((char *)event.data.scalar.value);
                           // printf("Parsed controlResourceSetZero: %d\n",global_vnf.gNBs->pdcchConfigSIB1->controlResourceSetZero);
                        } else if (strcmp(subkey, "searchSpaceZero") == 0) {
                            global_vnf.gNBs->pdcchConfigSIB1->searchSpaceZero = atoi((char *)event.data.scalar.value);
                            //printf("Parsed searchSpaceZero: %d\n", global_vnf.gNBs->pdcchConfigSIB1->searchSpaceZero);
                        }
                    }
                }
            } 

            else if (strcmp(key, "servingCellConfigCommon") == 0) {
                while (1) {
                    yaml_parser_parse(parser, &event);
                    if (event.type == YAML_MAPPING_END_EVENT) break;

                    if (event.type == YAML_SCALAR_EVENT) {
                        char *subkey = (char *)event.data.scalar.value;
                        yaml_parser_parse(parser, &event); // Parse the value

                        if (strcmp(subkey, "physicalCellIdentity") == 0) {
                            global_vnf.gNBs->servingCellConfigCommon.physCellId =atoi((char *)event.data.scalar.value);
                            // global_vnf.numServingcellcmn = 1;
                            // global_vnf.numMsgASCCsParam = 1;
                           // printf("Parsed physCellId: %d\n",global_vnf.gNBs->servingCellConfigCommon.physCellId);
                        }
                    }
                }
            } 
            else if (strcmp(key, "downlinkConfigCommon") == 0) {
                while (1) {
                    yaml_parser_parse(parser, &event);
                    if (event.type == YAML_MAPPING_END_EVENT) break;

                    if (event.type == YAML_SCALAR_EVENT) {
                        char *subkey = (char *)event.data.scalar.value;
                        yaml_parser_parse(parser, &event); // Parse the value

                        if (strcmp(subkey, "absoluteFrequencySSB") == 0) {
                            global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.absoluteFrequencySSB = atoi((char *)event.data.scalar.value);
                        //    printf("Parsed absoluteFrequencySSB: %d\n",global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.absoluteFrequencySSB);
                        } else if (strcmp(subkey, "dlfrequencyBand") == 0) {
                            global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_frequencyBand = atoi((char *)event.data.scalar.value);
                            // printf("Parsed dl_frequencyBand: %d\n",global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_frequencyBand);
                        } else if (strcmp(subkey, "absoluteFrequencyPointA") == 0) {
                            global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_absoluteFrequencyPointA = atoi((char *)event.data.scalar.value);
                            // printf("Parsed dl_absoluteFrequencyPointA: %d\n",global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_absoluteFrequencyPointA);
                        } else if (strcmp(subkey, "dloffsetToCarrier") == 0) {
                            global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_offstToCarrier = atoi((char *)event.data.scalar.value);
                            // printf("Parsed dl_offstToCarrier: %d\n",global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_offstToCarrier);
                        } else if (strcmp(subkey, "dlsubcarrierSpacing") == 0) {
                            int valueCheck = atoi((char *)event.data.scalar.value);
                            switch (valueCheck)
                            {
                            case 15:
                                global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_subcarrierSpacing = 0;
                                break;
                            case 30:
                                global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_subcarrierSpacing = 1;
                                break;
                            case 60:
                                global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_subcarrierSpacing = 2;
                                break;
                            case 120:
                                global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_subcarrierSpacing = 3;
                                break;
                            case 240:
                                global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_subcarrierSpacing = 4;
                                break;
                                
                            default:
                                SM_Logs(LOG_ERROR,_ARIA_,"Invalid value for downlinkSubcarrierSpacing: %d\n",valueCheck);
                                break;
                            }
                            
                            //printf("Parsed dl_subcarrierSpacing: %d\n",global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_subcarrierSpacing);
                        } else if (strcmp(subkey, "dlcarrierBandwidth") == 0) {
                            global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_carrierBandwidth = atoi((char *)event.data.scalar.value);
                            //printf("Parsed dl_carrierBandwidth: %d\n",global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_carrierBandwidth);
                        }
                        // Add more fields as required
                
                         // Check for entering initialDownlinkBWP section
                        if (strcmp(subkey, "initialDownlinkBWPParams") == 0) {
                            while(1){
                                yaml_parser_parse(parser, &event);
                                if (event.type == YAML_MAPPING_END_EVENT) break;

                                if (event.type == YAML_SCALAR_EVENT) {
                                    char *subkey = (char *)event.data.scalar.value;
                                    yaml_parser_parse(parser, &event); // Parse the value
                                    if (strcmp(subkey, "locationAndBandwidth") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPlocationAndBandwidth =
                                            atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed initialDLBWPlocationAndBandwidth: %d\n",global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPlocationAndBandwidth);
                                    } else if (strcmp(subkey, "subcarrierSpacing") == 0) {
                                        int valueCheck = atoi((char *)event.data.scalar.value);
                                        switch(valueCheck){
                                            case 15:
                                            global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPsubcarrierSpacing = 0;
                                            break;
                                            case 30:
                                            global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPsubcarrierSpacing = 1;
                                            break;
                                            case 60:
                                            global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPsubcarrierSpacing = 2;
                                            break;
                                            case 120:
                                            global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPsubcarrierSpacing = 3;
                                            break;
                                            case 240:
                                            global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPsubcarrierSpacing = 4;
                                            break;

                                            default:
                                            SM_Logs(LOG_ERROR,_ARIA_,"Invalid value for initialDLBWPsubcarrierSpacing: %d\n",valueCheck);
                                        }
                                        // printf("Parsed initialDLBWPsubcarrierSpacing: %d\n",global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPsubcarrierSpacing);
                                    } else if (strcmp(subkey, "controlResourceSetZero") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPcontrolResourceSetZero = atoi((char *)event.data.scalar.value);
                                        // printf("Parsed initialDLBWPcontrolResourceSetZero: %d\n",global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPcontrolResourceSetZero);
                                    } else if (strcmp(subkey, "searchSpaceZero") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPsearchSpaceZero = atoi((char *)event.data.scalar.value);
                                        // printf("Parsed initialDLBWPsearchSpaceZero: %d\n",global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPsearchSpaceZero);
                                    }
                                // Add more fields as required
                                }
                            }
                        }
                    }
                }
            }
            else if (strcmp(key, "uplinkConfigCommon") == 0) {
                while (1) {
                    yaml_parser_parse(parser, &event);
                    if (event.type == YAML_MAPPING_END_EVENT) break;

                    if (event.type == YAML_SCALAR_EVENT) {
                        char *subkey = (char *)event.data.scalar.value;
                        yaml_parser_parse(parser, &event); // Parse the value

                        if (strcmp(subkey, "ulfrequencyBand") == 0) {
                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_frequencyBand =atoi((char *)event.data.scalar.value);
                        //    printf("Parsed ul_frequencyBand: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_frequencyBand);
                        } else if (strcmp(subkey, "uloffsetToCarrier") == 0) {
                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_offstToCarrier =atoi((char *)event.data.scalar.value);
                            // printf("Parsed ul_offstToCarrier: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_offstToCarrier);
                        } else if (strcmp(subkey, "ulsubcarrierSpacing") == 0) {
                                int valueCheck = atoi((char *)event.data.scalar.value);

                                switch (valueCheck) {
                                    case 15:    
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_subcarrierSpacing = 0;
                                        break;
                                    case 30:
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_subcarrierSpacing = 1;
                                        break;
                                    case 60:
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_subcarrierSpacing = 2;
                                        break;
                                    case 120:
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_subcarrierSpacing = 3;
                                        break;
                                    case 240:
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_subcarrierSpacing = 4;
                                        break;
                                    default:
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_subcarrierSpacing = -1; // Assign default invalid value
                                        SM_Logs(LOG_ERROR,_ARIA_,"Invalid value for uplinkSubcarrierSpacing: %d\n",valueCheck);
                                        break;
                                }
                            }
                             else if (strcmp(subkey, "ulcarrierBandwidth") == 0) {
                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_carrierBandwidth =atoi((char *)event.data.scalar.value);
                           // printf("Parsed ul_carrierBandwidth: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_carrierBandwidth);
                        } else if (strcmp(subkey, "maximumTransmissionPower") == 0) {
                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pMax =atoi((char *)event.data.scalar.value);
                            //printf("Parsed pMax: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pMax);
                        } 

                        if (strcmp(subkey, "initialUplinkBWPParams") == 0) {
                            while(1){
                                yaml_parser_parse(parser, &event);
                                if (event.type == YAML_MAPPING_END_EVENT) break;

                                if (event.type == YAML_SCALAR_EVENT) {
                                    char *subBWPkey = (char *)event.data.scalar.value;
                                    yaml_parser_parse(parser, &event); // Parse the value
                                    if (strcmp(subBWPkey, "locationAndBandwidth") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.initialUplinkBWP.initialULBWPlocationAndBandwidth =
                                            atoi((char *)event.data.scalar.value);
                                      //  printf("Parsed initialDLBWPlocationAndBandwidth: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.initialUplinkBWP.initialULBWPlocationAndBandwidth);
                                    } else if (strcmp(subBWPkey, "subcarrierSpacing") == 0) {
                                        int valueCheck = atoi((char *)event.data.scalar.value);

                                        switch (valueCheck) {
                                            case 15:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.initialUplinkBWP.initialULBWPsubcarrierSpacing = 0;
                                                break;
                                            case 30:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.initialUplinkBWP.initialULBWPsubcarrierSpacing = 1;
                                                break;
                                            case 60:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.initialUplinkBWP.initialULBWPsubcarrierSpacing = 2;
                                                break;
                                            case 120:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.initialUplinkBWP.initialULBWPsubcarrierSpacing = 3;
                                                break;
                                            case 240:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.initialUplinkBWP.initialULBWPsubcarrierSpacing = 4;
                                                break;
                                            default:
                                                SM_Logs(LOG_ERROR,_ARIA_,"Invalid value for initialULBWPSubcarrierSpacing: %d\n",valueCheck);
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.initialUplinkBWP.initialULBWPsubcarrierSpacing = -1; // Assign default invalid value
                                                break;
                                        }
                                    }

                                // Add more fields as required
                                }
                            }
                        } 
                        else if (strcmp(subkey, "RACHConfigCommon") == 0) {
                            while(1){
                                yaml_parser_parse(parser, &event);
                                if (event.type == YAML_MAPPING_END_EVENT) break;

                                if (event.type == YAML_SCALAR_EVENT) {
                                    char *subkey = (char *)event.data.scalar.value;
                                    yaml_parser_parse(parser, &event); // Parse the value
                                    if (strcmp(subkey, "prachConfigIndex") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_ConfigurationIndex =
                                            atoi((char *)event.data.scalar.value);
                                    //    printf("\nParsed prach_ConfigurationIndex: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_ConfigurationIndex);
                                    } 
                                    else if (strcmp(subkey, "prachMsg1FDM") == 0) {
                                            int valueCheck = atoi((char *)event.data.scalar.value);

                                            switch (valueCheck) {
                                                case 1:
                                                    global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_msg1_FDM = 0;
                                                    break;
                                                case 2:
                                                    global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_msg1_FDM = 1;
                                                    break;
                                                case 4:
                                                    global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_msg1_FDM = 2;
                                                    break;
                                                case 8:
                                                    global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_msg1_FDM = 3;
                                                    break;
                                                default:
                                                    global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_msg1_FDM = -1; // Assign default invalid value
                                                    SM_Logs(LOG_INFO,_ARIA_,"Invalid value for prachMsg1FDM: %d",valueCheck);
                                                    break;
                                            }
                                        }

                                    else if (strcmp(subkey, "prachMsg1FrequencyStart") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_msg1_FrequencyStart = atoi((char *)event.data.scalar.value);
                                        // printf("Parsed prach_msg1_FrequencyStart: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_msg1_FrequencyStart);
                                    } 
                                    else if (strcmp(subkey, "zeroCorrelationZoneConfig") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.zeroCorrelationZoneConfig = atoi((char *)event.data.scalar.value);
                                        // printf("Parsed zeroCorrelationZoneConfig: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.zeroCorrelationZoneConfig);
                                    } 
                                    else if (strcmp(subkey, "preambleReceivedTargetPower") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.preambleReceivedTargetPower = atoi((char *)event.data.scalar.value);
                                        // printf("Parsed preambleReceivedTargetPower: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.preambleReceivedTargetPower);
                                    } 
                                    else if (strcmp(subkey, "preambleTransmissionMax") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.preambleTransMax = atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed preambleTransMax: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.preambleTransMax);
                                    } 
                                    else if (strcmp(subkey, "powerRampingStep") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.powerRampingStep = atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed powerRampingStep: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.powerRampingStep);
                                    } 
                                    else if (strcmp(subkey, "raResponseWindow") == 0) {
                                        int valueCheck = atoi((char *)event.data.scalar.value);

                                        switch (valueCheck) {
                                            case 1:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ResponseWindow = 0;
                                                break;
                                            case 2:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ResponseWindow = 1;
                                                break;
                                            case 4:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ResponseWindow = 2;
                                                break;
                                            case 8:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ResponseWindow = 3;
                                                break;
                                            case 10:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ResponseWindow = 4;
                                                break;
                                            case 20:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ResponseWindow = 5;
                                                break;
                                            case 40:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ResponseWindow = 6;
                                                break;
                                            case 80:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ResponseWindow = 7;
                                                break;
                                            default:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ResponseWindow = -1; // Assign invalid/default value
                                                SM_Logs(LOG_ERROR,_ARIA_,"Invalid Value of raResponseWindowSize: %d\n",valueCheck);
                                                break;
                                        }
                                    }

                                    else if (strcmp(subkey, "ssbPerRACHOccasion") == 0) {
                                        char *valueStr = (char *)event.data.scalar.value;

                                        if (strcmp(valueStr, "oneeighth") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR = 1;
                                        } else if (strcmp(valueStr, "onefourth") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR = 2;
                                        } else if (strcmp(valueStr, "half") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR = 3;
                                        } else if (strcmp(valueStr, "one") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR = 4;
                                        } else if (strcmp(valueStr, "two") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR = 5;
                                        } else if (strcmp(valueStr, "four") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR = 6;
                                        } else if (strcmp(valueStr, "eight") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR = 7;
                                        } else if (strcmp(valueStr, "sixteen") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR = 8;
                                        } else {
                                            // Handle unknown values (optional)
                                            SM_Logs(LOG_ERROR,_ARIA_,"Invalid value for ssbPerRACHOccasion: %s",valueStr);
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR = -1;
                                        }

                                        // printf("Parsed ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR: %d\n", 
                                            // global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR);
                                    }


                                    else if (strcmp(subkey, "cbPreamblesPerSSB") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB = atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed ssb_perRACH_OccasionAndCB_PreamblesPerSSB: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB);
                                    } 
                                    else if (strcmp(subkey, "raContentionResolutionTimer") == 0) {
                                        char *valueStr = (char *)event.data.scalar.value;

                                        if (strcmp(valueStr, "sf8") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ContentionResolutionTimer = 0;
                                        } else if (strcmp(valueStr, "sf16") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ContentionResolutionTimer = 1;
                                        } else if (strcmp(valueStr, "sf24") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ContentionResolutionTimer = 2;
                                        } else if (strcmp(valueStr, "sf32") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ContentionResolutionTimer = 3;
                                        } else if (strcmp(valueStr, "sf40") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ContentionResolutionTimer = 4;
                                        } else if (strcmp(valueStr, "sf48") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ContentionResolutionTimer = 5;
                                        } else if (strcmp(valueStr, "sf56") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ContentionResolutionTimer = 6;
                                        } else if (strcmp(valueStr, "sf64") == 0) {
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ContentionResolutionTimer = 7;
                                        } else {
                                            // Handle unknown values (optional safeguard)
                                            SM_Logs(LOG_ERROR,_ARIA_,"Invalid value for raContentionResolutionTimer: %s\n",valueStr);
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ContentionResolutionTimer = -1;
                                        }

                                        // printf("Parsed ra_ContentionResolutionTimer: %d\n", 
                                            // global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ContentionResolutionTimer);
                                    }

                                    else if (strcmp(subkey, "rsrpThresholdSSB") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.rsrp_ThresholdSSB = atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed rsrp_ThresholdSSB: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.rsrp_ThresholdSSB);
                                    } 
                                    else if (strcmp(subkey, "prachRootSequenceIndexPR") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_RootSequenceIndex_PR = atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed prach_RootSequenceIndex_PR: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_RootSequenceIndex_PR);
                                    } 
                                    else if (strcmp(subkey, "prachRootSequenceIndexValue") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_RootSequenceIndex = atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed prach_RootSequenceIndex: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_RootSequenceIndex);
                                    } 
                                    else if (strcmp(subkey, "msg1SubcarrierSpacing") == 0) {
                                        int valueCheck = atoi((char *)event.data.scalar.value);

                                        switch (valueCheck) {
                                            case 15:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.msg1_SubcarrierSpacing = 0;
                                                break;
                                            case 30:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.msg1_SubcarrierSpacing = 1;
                                                break;
                                            case 60:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.msg1_SubcarrierSpacing = 2;
                                                break;
                                            case 120:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.msg1_SubcarrierSpacing = 3;
                                                break;
                                            case 240:
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.msg1_SubcarrierSpacing = 4;
                                                break;
                                            default:
                                                SM_Logs(LOG_ERROR,_ARIA_,"Invalid value of msg1SubcarrierSpacingIndex: %d",valueCheck);
                                                // Handle unexpected values
                                                global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.msg1_SubcarrierSpacing = -1;
                                                break;
                                        }
                                    }

                                    else if (strcmp(subkey, "restrictedSetConfig") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.restrictedSetConfig = atoi((char *)event.data.scalar.value);
                                        // printf("Parsed restrictedSetConfig: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.restrictedSetConfig);
                                    } 
                                    else if (strcmp(subkey, "msg3DeltaPreamble") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.msg3_DeltaPreamble = atoi((char *)event.data.scalar.value);
                                        // printf("Parsed msg3_DeltaPreamble: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.msg3_DeltaPreamble);
                                    } 
                                    else if (strcmp(subkey, "p0NominalWithGrant") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.p0_NominalWithGrant = atoi((char *)event.data.scalar.value);
                                        // printf("Parsed p0_NominalWithGrant: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.p0_NominalWithGrant);
                                    } 
                                    
                                // Add more fields as required
                                }
                            }
                        } 
                        else if (strcmp(subkey, "pucchConfigCommon") == 0) {
                            while(1){
                                yaml_parser_parse(parser, &event);
                                if (event.type == YAML_MAPPING_END_EVENT) break;

                                if (event.type == YAML_SCALAR_EVENT) {
                                    char *subkey = (char *)event.data.scalar.value;
                                    yaml_parser_parse(parser, &event); // Parse the value
                                    if (strcmp(subkey, "pucchGroupHopping") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.pucchGroupHopping = atoi((char *)event.data.scalar.value);
                                    //    printf("\nParsed pucchGroupHopping: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.pucchGroupHopping);
                                    } 
                                    else if (strcmp(subkey, "hoppingID") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.hoppingId = atoi((char *)event.data.scalar.value);
                                        // printf("Parsed hoppingId: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.hoppingId);
                                    }
                                    else if (strcmp(subkey, "p0Nominal") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.p0_nominal = atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed p0_nominal: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.p0_nominal);
                                    }
                                    else if (strcmp(subkey, "ssbPositionsInBurstBitmapType") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.ssb_PositionsInBurst_PR = atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed ssb_PositionsInBurst_PR: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.ssb_PositionsInBurst_PR);
                                    }
                                    else if (strcmp(subkey, "ssbPositionsInBurst") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.ssb_PositionsInBurst_Bitmap = atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed ssb_PositionsInBurst_Bitmap: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.ssb_PositionsInBurst_Bitmap);
                                    }
                                    else if (strcmp(subkey, "ssbPeriodicityServingCell") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.ssb_periodicityServingCell = atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed ssb_periodicityServingCell: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.ssb_periodicityServingCell);
                                    }
                                    else if (strcmp(subkey, "dmrsTypeAPosition") == 0) {
                                        global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.dmrs_TypeA_Position = atoi((char *)event.data.scalar.value);
                                    //    printf("Parsed dmrs_TypeA_Position: %d\n",global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.dmrs_TypeA_Position);
                                    }
                                    else if (strcmp(subkey, "subcarrierSpacing") == 0) {
                                    int valueCheck = atoi((char *)event.data.scalar.value);
                                
                                    switch (valueCheck) {
                                        case 15:
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.subcarrierSpacing = 0;
                                            break;
                                        case 30:
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.subcarrierSpacing = 1;
                                            break;
                                        case 60:
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.subcarrierSpacing = 2;
                                            break;
                                        case 120:
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.subcarrierSpacing = 3;
                                            break;
                                        case 240:
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.subcarrierSpacing = 4;
                                            break;
                                        default:
                                            // Handle unexpected values
                                            SM_Logs(LOG_ERROR,_ARIA_,"Invalid value for subcarrierSpacingValue: %d\n",valueCheck);
                                            global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.subcarrierSpacing = -1;
                                            break;
                                    }
                                }
                                
                                // Add more fields as required
                                }
                            }
                        } 
                    }
                }
            } 
            else if (strcmp(key, "tddULDLConfigCommon") == 0) {
                while (1) {
                    yaml_parser_parse(parser, &event);
                    if (event.type == YAML_MAPPING_END_EVENT) break;

                    if (event.type == YAML_SCALAR_EVENT) {
                        char *subkey = (char *)event.data.scalar.value;
                        yaml_parser_parse(parser, &event); // Parse the value

                     if (strcmp(subkey, "referenceSubcarrierSpacing") == 0) {
                        int valueCheck = atoi((char *)event.data.scalar.value);
                    
                        switch (valueCheck) {
                            case 15:
                                global_vnf.tddConfigCommon.referenceSubcarrierSpacing = 0;
                                break;
                            case 30:
                                global_vnf.tddConfigCommon.referenceSubcarrierSpacing = 1;
                                break;
                            case 60:
                                global_vnf.tddConfigCommon.referenceSubcarrierSpacing = 2;
                                break;
                            case 120:
                                global_vnf.tddConfigCommon.referenceSubcarrierSpacing = 3;
                                break;
                            case 240:
                                global_vnf.tddConfigCommon.referenceSubcarrierSpacing = 4;
                                break;
                            default:
                                // Handle unexpected values
                                SM_Logs(LOG_ERROR,_ARIA_,"Invalid value for referenceSubcarrierSpacingIndex: %d\n",valueCheck);
                                global_vnf.tddConfigCommon.referenceSubcarrierSpacing = -1;
                                break;
                        }
                        // printf("\nParsed referenceSubcarrierSpacing: %d\n", global_vnf.tddConfigCommon.referenceSubcarrierSpacing);
                    }

                        else if (strcmp(subkey, "dlUlTransmissionPeriodicity") == 0) {
                            global_vnf.tddConfigCommon.dl_UL_TransmissionPeriodicity =atoi((char *)event.data.scalar.value);
                        //    printf("Parsed dl_UL_TransmissionPeriodicity: %d\n",global_vnf.tddConfigCommon.dl_UL_TransmissionPeriodicity);
                        }
                        else if (strcmp(subkey, "nrofDownlinkSlots") == 0) {
                            global_vnf.tddConfigCommon.nrofDownlinkSlots =atoi((char *)event.data.scalar.value);
                        //    printf("Parsed nrofDownlinkSlots: %d\n",global_vnf.tddConfigCommon.nrofDownlinkSlots);
                        }
                        else if (strcmp(subkey, "nrofDownlinkSymbols") == 0) {
                            global_vnf.tddConfigCommon.nrofDownlinkSymbols =atoi((char *)event.data.scalar.value);
                        //    printf("Parsed nrofDownlinkSymbols: %d\n",global_vnf.tddConfigCommon.nrofDownlinkSymbols);
                        }
                        else if (strcmp(subkey, "nrofUplinkSlots") == 0) {
                            global_vnf.tddConfigCommon.nrofUplinkSlots =atoi((char *)event.data.scalar.value);
                        //    printf("Parsed nrofUplinkSlots: %d\n",global_vnf.tddConfigCommon.nrofUplinkSlots);
                        }
                        else if (strcmp(subkey, "nrofUplinkSymbols") == 0) {
                            global_vnf.tddConfigCommon.nrofUplinkSymbols =atoi((char *)event.data.scalar.value);
                        //    printf("Parsed nrofUplinkSymbols: %d\n",global_vnf.tddConfigCommon.nrofUplinkSymbols);
                        }
                        else if (strcmp(subkey, "ssPBCHBlockPower") == 0) {
                            global_vnf.tddConfigCommon.ssPBCH_BlockPower =atoi((char *)event.data.scalar.value);
                        //    printf("Parsed ssPBCH_BlockPower: %d\n",global_vnf.tddConfigCommon.ssPBCH_BlockPower);
                        }    
                    }
                }
            }
            yaml_event_delete(&event);
        }
    }
}




void initializeDuDefaultConfigs(){

                            global_configs.defvals.OPT_typ = 3;
                            global_configs.defvals.LOGparamDEBUG = 0;
                            global_configs.defvals.LOGparamDUMP = 0;
                            global_configs.defvals.LOGparamlogFILE = 0;
                            global_configs.defvals.NFAPI_idx = 27;
                            global_configs.defvals.NFAPI_mde = 0;

                            global_configs.numTHREADlist = 0;
                            global_configs.defvals.numPRSconfigs = 0;

                            global_configs.defvals.pdsch_AntennaPorts_N1 = 1;
                            global_configs.defvals.pdsch_AntennaPorts_N2 = 1;
                            global_configs.defvals.pdsch_AntennaPorts_XP = 2;
                            global_configs.defvals.pusch_AntennaPorts_idx = 4;

                            global_configs.defvals.minRXTXTIME = 2;
                            global_configs.defvals.sib1_tda = 15;
                            global_configs.defvals.do_CSIRS = 1;
                            global_configs.defvals.do_SRS = 0;
                            global_configs.defvals.force_256qam_dis = 0;

                            global_configs.defvals.force_UL256qam_off = 0;
                            global_configs.defvals.use_deltaMCS = 0;
                            global_configs.defvals.maxMIMO_layers = -1;
                            global_configs.defvals.disable_harq = 0;
                            global_configs.defvals.num_dlharq = 16;
                            global_configs.defvals.num_ulharq = 16;

                            global_configs.defvals.sr_ProhibitTimer = 0;
                            global_configs.defvals.sr_TransMax = 64;
                            global_configs.defvals.sr_ProhibitTimer_v1700 = 0;
                            global_configs.defvals.t300 = 400;
                            global_configs.defvals.t301 = 400;
                            global_configs.defvals.t310 = 2000;
                            global_configs.defvals.n310 = 10;
                            global_configs.defvals.t311 = 3000;
                            global_configs.defvals.n311 = 1;
                            global_configs.defvals.t319 = 400;
                            global_configs.defvals.GNB_BEAMWEIGH_IDX = 0;
                            global_configs.defvals.GNBwtsidx = true;
                            global_configs.fhi_72.fileprefix = strdup("wls_0");

                            global_vnf.numServingcellcmn = 1;
                            global_vnf.numMsgASCCsParam = 1;
                            global_configs.RUs.LOCALifname = strdup("lo");
                            global_configs.RUs.LOCALaddr = strdup("127.0.0.2");
                            global_configs.RUs.REMOTEaddr = strdup("127.0.0.1");

                            global_configs.RUs.LOCALportC = 50000;
                            global_configs.RUs.LOCALportD = 50001;
                            global_configs.RUs.REMOTEportC = 50000;
                            global_configs.RUs.REMOTEportD = 50001;
                            global_vnf.macrlcs->F1uaddr = NULL;
                            GlobC_ARIAConfs.y_defvalues.D_DRBs = 1;
                            GlobC_ARIAConfs.y_defvalues.Default_DRBs= 0;
                            global_configs.RUs.RU_IFfreq = 0;
                            global_configs.RUs.RU_if_freqoffset = 0;
                             global_configs.RUs.RU_nrflag = 0;
                             global_configs.RUs.RU_nr_scsRaster = 1;

                            // printf("Size of new_UplinkConfigCommon_t: %zu\n", sizeof(new_UplinkConfigCommon_t));
                            // printf("Size of BWP_UplinkCommon_t: %zu\n", sizeof(new_BWP_UplinkCommon_t)); // commented
                            // printf("Size of new_NR_SetupRel_RACH_ConfigCommon_t: %zu\n", sizeof(new_NR_SetupRel_RACH_ConfigCommon_t));
                            // printf("Size of new_NR_RACH_ConfigCommon_t: %zu\n", sizeof(new_NR_RACH_ConfigCommon_t));
                            // printf("Size of new_NR_RACH_ConfigGeneric_t: %zu\n", sizeof(new_NR_RACH_ConfigGeneric_t));


                            int carrier_count = 10;
                            global_vnf.GSCC = malloc(sizeof(*global_vnf.GSCC));
                            global_vnf.GSCC->uplinkConfigCommon = malloc(sizeof(new_UplinkConfigCommon_t));
                            global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL = malloc(sizeof(new_NR_FrequencyInfoUL_t));
                            global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->frequencyBandList = malloc(sizeof(new_NR_MultiFrequencyBandListNR_t));
                            global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->frequencyBandList->list.array = malloc(sizeof(new_NR_FreqBandIndicatorNR_t));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP = malloc(sizeof(new_BWP_UplinkCommon_t));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon = malloc(sizeof(new_NR_SetupRel_RACH_ConfigCommon_t));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup = malloc(sizeof(new_NR_RACH_ConfigCommon_t));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric = malloc(sizeof(new_NR_RACH_ConfigGeneric_t));
                            global_vnf.GSCC->downlinkConfigCommon = malloc(sizeof(new_DownlinkConfigCommon_t));
                            global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL = malloc(sizeof(new_NR_FrequencyInfoDL_t));
                            global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP = malloc(sizeof(new_NR_BWP_DownlinkCommon_t));
                            global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->frequencyBandList = malloc(sizeof(new_NR_MultiFrequencyBandListNR_t));
                            global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->frequencyBandList->list.array = malloc(sizeof(new_NR_FreqBandIndicatorNR_t));
                            // global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->new_scs_SpecificCarrierList = malloc(sizeof(struct new_NR_SCS_SpecificCarrier));
                            global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->new_scs_SpecificCarrierList.list.array = (struct new_NR_SCS_SpecificCarrier *)malloc(sizeof(struct new_NR_SCS_SpecificCarrier));
                            // global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->new_scs_SpecificCarrierList.list.array = malloc(sizeof(struct new_NR_SCS_SpecificCarrier) * carrier_count);
                            global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->new_scs_SpecificCarrierList.list.count = carrier_count;
                            global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->new_scs_SpecificCarrierList.list.size = carrier_count;



                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->msg1_FDM = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_msg1_FDM;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->present = new_NR_SetupRelease_RACH_ConfigCommon_PR_setup;
                            global_vnf.GSCC->physCellId = global_vnf.gNBs->servingCellConfigCommon.physCellId;
                            global_vnf.GSCC->n_TimingAdvanceOffset = 0;
                            global_vnf.GSCC->ssb_periodicityServingCell = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.ssb_periodicityServingCell;  //will review this later 
                            global_vnf.GSCC->dmrs_TypeA_Position = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.dmrs_TypeA_Position;
                            global_vnf.GSCC->ssbSubcarrierSpacing = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.subcarrierSpacing;
                            
                            global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencySSB = global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.absoluteFrequencySSB;
                            global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->absoluteFrequencyPointA = global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_absoluteFrequencyPointA;

                            global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->frequencyBandList->list.array[0] = global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_frequencyBand;

                            global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->genericParameters = malloc(sizeof(struct new_NR_BWP));
                            global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon = malloc(sizeof(struct new_NR_SetupRelease_PDCCH_ConfigCommon));
                            global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup = malloc(sizeof(struct new_NR_PDCCH_ConfigCommon));

                            // global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->new_scs_SpecificCarrierList.list.array[0]->offsetToCarrier = 0;
                            // global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->new_scs_SpecificCarrierList.list.array[0]->carrierBandwidth = 273;
                            // global_vnf.GSCC->downlinkConfigCommon->frequencyInfoDL->new_scs_SpecificCarrierList.list.array[0]->subcarrierSpacing = 1;
                            global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->genericParameters->locationAndBandwidth = global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPlocationAndBandwidth;
                            global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->genericParameters->subcarrierSpacing = global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPsubcarrierSpacing;
                            global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->controlResourceSetZero = global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPcontrolResourceSetZero;
                            global_vnf.GSCC->downlinkConfigCommon->initialDownlinkBWP->pdcch_ConfigCommon->choice.setup->searchSpaceZero = global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.initialDownlinkBWP.initialDLBWPsearchSpaceZero;
                            global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->frequencyBandList->list.array[0] = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_frequencyBand;
                            global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->absoluteFrequencyPointA = -1;
                            global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->p_Max = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pMax;
                            // global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->new_scs_SpecificCarrierList.list.array[0]->offsetToCarrier = 0;
                            // global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->new_scs_SpecificCarrierList.list.array[0]->subcarrierSpacing = 1;
                            // global_vnf.GSCC->uplinkConfigCommon->frequencyInfoUL->new_scs_SpecificCarrierList.list.array[0]->carrierBandwidth = 273;

                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->genericParameters = malloc(sizeof(struct new_NR_BWP));

                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->genericParameters->locationAndBandwidth = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.initialUplinkBWP.initialULBWPlocationAndBandwidth;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->genericParameters->subcarrierSpacing = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.initialUplinkBWP.initialULBWPsubcarrierSpacing;
                            

                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->msg1_FrequencyStart = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_msg1_FrequencyStart;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->powerRampingStep = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.powerRampingStep;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->prach_ConfigurationIndex = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_ConfigurationIndex;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->preambleReceivedTargetPower = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.preambleReceivedTargetPower;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->preambleTransMax = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.preambleTransMax;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->ra_ResponseWindow = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ResponseWindow;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rach_ConfigGeneric->zeroCorrelationZoneConfig = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.zeroCorrelationZoneConfig;

                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg3_transformPrecoder = malloc(sizeof(struct new_NR_RACH_ConfigCommon));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg1_SubcarrierSpacing = malloc(sizeof(struct new_NR_RACH_ConfigCommon));
                            
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg3_transformPrecoder = new_NR_PUSCH_Config__transformPrecoder_disabled;   
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->msg1_SubcarrierSpacing = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.msg1_SubcarrierSpacing;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ra_ContentionResolutionTimer = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ra_ContentionResolutionTimer;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.choice.l139 = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_RootSequenceIndex;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->prach_RootSequenceIndex.present = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.prach_RootSequenceIndex_PR;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->restrictedSetConfig = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.restrictedSetConfig;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->rsrp_ThresholdSSB = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.rsrp_ThresholdSSB;
                            // global_vnf.GSCC.uplinkConfigCommon.initialUplinkBWP.rach_ConfigCommon->choice.setup->rsrp_ThresholdSSB_SUL = 0;
                            // global_vnf.GSCC.uplinkConfigCommon.initialUplinkBWP.rach_ConfigCommon.choice.setup-> msg3_transformPrecoder = ;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB = malloc(sizeof(new_NR_RACH_ConfigCommon__ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->choice.one = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->rach_ConfigCommon->choice.setup->ssb_perRACH_OccasionAndCB_PreamblesPerSSB->present = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.ssb_perRACH_OccasionAndCB_PreamblesPerSSB_PR;

                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon = malloc(sizeof(struct new_NR_SetupRelease_PUSCH_ConfigCommon));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup = malloc(sizeof(struct new_NR_PUSCH_ConfigCommon));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->groupHoppingEnabledTransformPrecoding = malloc(sizeof(struct new_NR_PUSCH_ConfigCommon));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->msg3_DeltaPreamble = malloc(sizeof(struct new_NR_PUSCH_ConfigCommon));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->pusch_TimeDomainAllocationList = malloc(sizeof(struct new_NR_PUSCH_TimeDomainResourceAllocationList));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->groupHoppingEnabledTransformPrecoding = malloc(sizeof(struct new_NR_PUSCH_ConfigCommon));

                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->msg3_DeltaPreamble = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.msg3_DeltaPreamble;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->groupHoppingEnabledTransformPrecoding = 0;
                            // global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->pusch_TimeDomainAllocationList->list.array[0] = 0;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pusch_ConfigCommon->choice.setup->p0_NominalWithGrant = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.rachConfigCommon.p0_NominalWithGrant;

                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon = malloc(sizeof(struct new_NR_SetupRelease_PUCCH_ConfigCommon));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup = malloc(sizeof(struct new_NR_PUCCH_ConfigCommon));
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->pucch_ResourceCommon = malloc(sizeof(struct new_NR_PUCCH_ConfigCommon));;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->hoppingId = malloc(sizeof(struct new_NR_PUCCH_ConfigCommon));;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->p0_nominal = malloc(sizeof(struct new_NR_PUCCH_ConfigCommon));;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->pucch_GroupHopping = malloc(sizeof(struct new_NR_PUCCH_ConfigCommon));

                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->pucch_ResourceCommon = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.pucchGroupHopping;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->hoppingId = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.hoppingId;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->p0_nominal = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.p0_nominal;
                            global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->choice.setup->pucch_GroupHopping = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.pucchConfigCommon.pucchGroupHopping;
                            // global_vnf.GSCC->uplinkConfigCommon->initialUplinkBWP->pucch_ConfigCommon->present = new_NR_SetupRelease_PUCCH_ConfigCommon_PR_setup;

                            // global_vnf.GSCC->ssb_PositionsInBurst = malloc(sizeof(struct new_NR_ServingCellConfigCommon__ssb_PositionsInBurst));
                            // global_vnf.GSCC->ssb_PositionsInBurst->present = malloc(sizeof(new_NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR));
                            // global_vnf.GSCC->ssb_PositionsInBurst->present = new_NR_ServingCellConfigCommon__ssb_PositionsInBurst_PR_NOTHING;


                            global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon = malloc(sizeof(struct new_NR_TDD_UL_DL_ConfigCommon));
                            global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1 = malloc(sizeof(new_NR_TDD_UL_DL_Pattern_t));
                            // global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern2 =malloc(sizeof(struct new_NR_TDD_UL_DL_Pattern));
                            global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->referenceSubcarrierSpacing = malloc(sizeof(new_NR_SubcarrierSpacing_t));

                            global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->referenceSubcarrierSpacing = global_vnf.tddConfigCommon.referenceSubcarrierSpacing;
                            global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->dl_UL_TransmissionPeriodicity = global_vnf.tddConfigCommon.dl_UL_TransmissionPeriodicity;
                            global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofDownlinkSlots = global_vnf.tddConfigCommon.nrofDownlinkSlots;
                            global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofDownlinkSymbols = global_vnf.tddConfigCommon.nrofDownlinkSymbols;
                            global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofUplinkSlots = global_vnf.tddConfigCommon.nrofUplinkSlots;
                            global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern1->nrofUplinkSymbols = global_vnf.tddConfigCommon.nrofUplinkSymbols;

                            // global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern2->dl_UL_TransmissionPeriodicity = 0;
                            // global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern2->nrofDownlinkSlots = 8;
                            // global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern2->nrofDownlinkSymbols = 16;
                            // global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern2->nrofUplinkSlots = 24;
                            // global_vnf.GSCC->tdd_UL_DL_ConfigurationCommon->pattern2->nrofUplinkSymbols = 32;
                            global_vnf.dloffsetToCarrier = global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_offstToCarrier;
                            global_vnf.dlsubcarrierSpacing = global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_subcarrierSpacing;
                            global_vnf.dlcarrierBandwidth = global_vnf.gNBs->servingCellConfigCommon.downlinkConfigCommon.dl_carrierBandwidth;

                            global_vnf.uloffsetToCarrier = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_offstToCarrier;
                            global_vnf.ulsubcarrierSpacing = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_subcarrierSpacing;
                            global_vnf.ulcarrierBandwidth = global_vnf.gNBs->servingCellConfigCommon.uplinkConfigCommon.ul_carrierBandwidth;
                            

                            global_vnf.GSCC->ss_PBCH_BlockPower = global_vnf.tddConfigCommon.ssPBCH_BlockPower;


                            global_vnf.mSCC.msgB_ResponseWindow_r16 = -1;
                            global_vnf.mSCC.msgA_RSRP_Threshold_r16 = 19;
                            global_vnf.mSCC.msgA_MCS_r16 = 2;
                            global_vnf.mSCC.nrofSlotsMsgA_PUSCH_r16 = 1;
                            global_vnf.mSCC.nrofMsgA_PO_PerSlot_r16 = 0;
                            global_vnf.mSCC.msgA_PUSCH_TimeDomainOffset_r16 = 0;
                            global_vnf.mSCC.startSymbolAndLengthMsgA_PO_r16 = 38;
                            global_vnf.mSCC.mappingTypeMsgA_PUSCH_r16 = 0;
                            global_vnf.mSCC.guardBandMsgA_PUSCH_r16 = 0;
                            global_vnf.mSCC.frequencyStartMsgA_PUSCH_r16 = 0;
                            global_vnf.mSCC.nrofPRBs_PerMsgA_PO_r16 = 8;
                            global_vnf.mSCC.nrofMsgA_PO_FDM_r16 = 0;
                            global_vnf.mSCC.msgA_PUSCH_NrofPorts_r16 = 0;
                            global_vnf.mSCC.nrofDMRS_Sequences_r16 = 1;
                            global_vnf.mSCC.msgA_TransformPrecoder_r16 = 1;
                            global_vnf.mSCC.msgA_CB_PreamblesPerSSB_PerSharedRO_r16 = 1;


                            global_vnf.ephemerisPositionX = 0;
                            global_vnf.ephemerisPositionY = 0;
                            global_vnf.ephemerisPositionZ = 0;
                            global_vnf.ephemerisVelocityVX = 0;
                            global_vnf.ephemerisVelocityVY = 0;
                            global_vnf.ephemerisVelocityVZ = 0;
                            global_vnf.taCommon = 0;
                            GlobC_ARIAConfs.numplmnlist = 1;
                            GlobC_ARIAConfs.numSECURITY = 0;
                            GlobC_ARIAConfs.numINTEGalgo = 0;
                            //------------------------------------------------------------------------------------------------

                            global_vnf.numMACRLCs = 1;
                            global_vnf.numactiveGNBs = 1;
                            global_vnf.numSCDconfigs = 0;
                            global_vnf.bwdidx = 11;
                            GlobC_ARIAConfs.y_security.CipherModes[0] = NULL;
                            GlobC_ARIAConfs.y_security.AuthModes[0]=NULL;
                            GlobC_ARIAConfs.y_security.BchannelEncryp = strdup("no");
                            GlobC_ARIAConfs.y_security.Bchannelauthentication = strdup("no");

                            global_configs.U_SF_record = 0;
                            global_configs.U_SF_replay = 0;
                            global_configs.use_mmap = 1;

                                                        // Hardcoding values to the provided variables
                            global_configs.defvals.ULSCH_max_frame = 10;

                            double dloption_upper_value = 0.15; // Use these to avoid dangling pointers
                            double dloption_lower_value = 0.05;
                            global_configs.defvals.dloption_upper = 0.15;
                            //printf(" DL BLER Target Upper: %.2f\n", global_configs.defvals.dloption_upper);
                            global_configs.defvals.dloption_lower = 0.05;

                            global_configs.defvals.dloption_maxmcs = 28;
                            global_configs.defvals.dloption_harqmaxround = 4;

                            double uloption_upper_value = 0.15;
                            double uloption_lower_value = 0.05;
                            global_configs.defvals.uloption_upper = 0.15;
                            global_configs.defvals.uloption_lower = 0.05;

                            global_configs.defvals.uloption_maxmcs = 28;
                            global_configs.defvals.uloption_harqmaxround = 4;

                            global_configs.defvals.min_grant_PRB = 5;
                            global_configs.defvals.min_grant_MCS = 9;

                            global_configs.defvals.identity_PM = false; // Identity PM is 0 in the data, representing "false"
                            global_configs.defvals.sdapflag = strdup("disabled"); // Number of UL PRB Blacklist Entries
                            global_configs.defvals.x2ap_state = NULL;
                            global_configs.defvals.UMONDEFAULTDRB = 0;
                            global_configs.defvals.D_DRBs = 1;
                            global_configs.defvals.Cell_typ = strdup("CELL_MACRO_GNB");
                            global_configs.defvals.GNB_Remote_S_PORTD = 50001;
                            global_configs.defvals.GNB_local_S_ADDR = strdup("127.0.0.1");
                            global_configs.defvals.GNB_local_S_PORTD = 50001;
                            global_configs.defvals.GNB_cuup_id = 1;
                            
                            global_configs.defvals.PUSCH_target_snrx10 = 200;
                            global_configs.defvals.PUCCH_target_snrx10 = 150;
                            global_configs.defvals.UL_PRBblack_SNR_thres = 10;
                            global_configs.defvals.PUCCH_fail_thres = 10;
                            global_configs.defvals.PUSCH_fail_thres = 10;
                            global_configs.fhi_72.cp_vlan_tag = 0;
                            global_configs.fhi_72.cp_vlan_tag =0;
                            global_configs.loddt_shlibpath = "";
                             global_configs.maxshlibs  = 10;
                            global_configs.numPl = 8;
                            global_configs.defvals.gnb_tr_s_pref = strdup("local_mac");
                            global_configs.fhi_72.pkt_proc_core = 4;
                            global_vnf.VNFactive = true;
                            global_configs.PNFactive = false;
                            global_configs.fhi_72.nEthLinePerPort = 1;
                            global_configs.fhi_72.nEthLineSpeed = 10;
                            global_configs.fhi_72.timingcores = 4;
                            char *global_config_thread_pool_cores = strdup("2,4,6,7");
                            char *thread_pool_cores[MAX_CORES];
                            int numCores = 0;
                            global_vnf.macrlcs->tr_s_preference = strdup("local_L1");
                            global_vnf.macrlcs->tr_n_preference = strdup("f1");
                            global_configs.RUs.tr_preference = strdup("raw_if4p5");
                            global_configs.L1s.tr_n_preference = strdup("local_mac");
                        
                            // Parse the string into individual cores
                            // char *token = strtok(global_config_thread_pool_cores, ",");
                            // while (token != NULL && numCores < MAX_CORES) {
                            //    global_configs.thread_pool_cores[numCores++] = strdup(token); // Duplicate and store the core
                            //     token = strtok(NULL, ",");
                            // }
                        
                            // Print the parsed cores
                            // printf("Parsed cores:\n");
                            // for (int i = 0; i < numCores; i++) {
                            //     printf("Core %d: %s\n", i, global_configs.thread_pool_cores[i]);
                            //     //free(global_configs.thread_pool_cores[i]); // Free each allocated core ID
                            // }
                            GlobC_ARIAConfs.CUActive = false;
                            global_configs.L1DUactive = true;

                            global_vnf.ULPRBBlacklist = "";

                            global_configs.defvals.L1_SRS_DTX_THRES = 30;
                            global_configs.defvals.OFDM_offset_divisor = 8;
                            global_configs.defvals.num_ulprbbl = 0;
                            global_configs.defvals.L1_THREAD_POOL_SIZ = 2022;

                            global_configs.L1s.local_n_address = strdup("null");
                            global_configs.L1s.remote_n_address = strdup("NULL");
                            global_configs.L1s.local_n_portc = 0;
                            global_configs.L1s.local_n_portd = 0;
                            global_configs.L1s.remote_n_portc = 0;
                            global_configs.L1s.remote_n_portd = 0;

                            global_vnf.macrlcs->pusch_failure_thres = 10;

                            global_vnf.ANALOG_BEAMFORMG_IDX = 0;
                            global_vnf.macrlcs->remote_s_portd = 2152;
                            global_vnf.macrlcs->local_s_portd = 2152;
                            global_configs.L1s.max_ldpc_iterations = 10;
                            global_configs.RUs.sf_extension = 0;
                            global_configs.RUs.eNB_instances = 0;

}


int YamlVNFGlobal(configmodule_interface_t *cfgptr, const char *filename) {
   // printf("\ntesting--> inside YamlVNFGlobal\n");

    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Failed to open YAML file: %s\n", filename);
        return;
    }
    

    yaml_parser_t parser;
    yaml_event_t event;

    // Initialize YAML parser
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, file);

    // Parse through the YAML file
    while (1) {
        yaml_parser_parse(&parser, &event);

        if (event.type == YAML_STREAM_END_EVENT) {
            break; // End of the YAML stream
        }

        if (event.type == YAML_SCALAR_EVENT) {
            // Capture the current key
            char *key = (char *)event.data.scalar.value;



            if (strcmp(key, "ReleaseVersion") == 0) {
               yaml_parser_parse(&parser, &event);
               if (event.type == YAML_SCALAR_EVENT) {
                   char *endptr;
                   global_vnf.ReleaseVersion = strdup((char *)event.data.scalar.value);
                 
                  
                   SM_Log_Assert(_ARIA_, strcmp(global_vnf.ReleaseVersion, ARIA_VERSION) == 0, "Illegal File...!!\n");
                    
                 }
           }

           else if (strcmp(key, "gNodeB-DU") == 0) {
                    yaml_parser_parse(&parser, &event);
                    if (event.type == YAML_SEQUENCE_START_EVENT) {
                        while (1) {
                            yaml_parser_parse(&parser, &event);
                            if (event.type == YAML_SEQUENCE_END_EVENT) {
                                break; // End of sequence
                            }
                            if (event.type == YAML_MAPPING_START_EVENT) {
                                // Initialize DU Configs
                                // initializeDuDefaultConfigs();
                                LoadgNode_B_DU(&parser);
                
                                
                            }
                        }
                    }
                }
                


            // Parse L1 section
            else if (strcmp(key, "duConfig") == 0) {
                //printf("Found gNBs section, calling gNBs\n\n");
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(&parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break; // End of gNBs sequence
                        }
                        if (event.type == YAML_MAPPING_START_EVENT) {
                            // Call fill_gNB for each gNB in the list
                            LoadGNBConfigs(&parser);
                            initializeDuDefaultConfigs();
                            
                        }
                    }
                }
            }

            else if (strcmp(key, "plmnConfig") == 0) {
               // printf("Found plmn_list section, calling LoadPLMNlist\n\n");

                // Expect a sequence here
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(&parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break;  // End of the plmn_list sequence
                        }

                        if (event.type == YAML_MAPPING_START_EVENT) {
                            // Parse each plmn_list entry
                            LoadPLMNlist(&parser);
                            global_vnf.numPlmnlist = 1;
                            // global_configs.numPlmn++;
                            // printf("numplmn : %d\n", global_configs.numPlmn);
                        }
                    }
                } 
            }

            // Parse security section
            else if (strcmp(key, "SCTP") == 0) {
               // printf("\nFound SCTP section, calling LoadSCTPConfigs\n");
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_MAPPING_START_EVENT) {
                    LoadSCTPConfigs(&parser);
                    // global_configs.numSECURITY++;
                    // printf("numSECURITY : %d\n",global_configs.numSECURITY);
                } else {
                    //printf("Invalid event type for security, expected a mapping.\n");
                }
            }

            else if (strcmp(key, "f1apInterface") == 0) {
               // printf("Found MACRLCs section, calling LoadAMFConfigs\n\n");

                // Parse the sequence start event (since the amf_ip_address is a list)
                yaml_parser_parse(&parser, &event);
                
                if (event.type == YAML_SEQUENCE_START_EVENT) {
                    // Process each item in the sequence (each item is a mapping)
                    while (1) {
                        yaml_parser_parse(&parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break;  // End of sequence
                        }
                        if (event.type == YAML_MAPPING_START_EVENT) {
                            LoadMACRLCs(&parser); // Process the mapping
                        } else {
                            printf("Invalid event type inside sequence for MACRLCs, expected a mapping.\n");
                        }
                    }
                }
            }

            else if (strcmp(key, "L1ConfigList") == 0) {
                //  printf("Found L1s section, calling LoadL1Configs\n\n");
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(&parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break; // End of gNBs sequence
                        }
                        if (event.type == YAML_MAPPING_START_EVENT) {
                            // Call fill_gNB for each gNB in the list
                            LoadL1Configs(&parser);
                            global_vnf.numL1++;
                           // printf("global_vnf.numL1 : %d\n",global_vnf.numL1++);
                        }
                    }
                }
            }

            else if (strcmp(key, "RUConfigList") == 0) {
               // printf("Found RUs section, calling LoadRUConfigs\n\n");

                // Expect a sequence here
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(&parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break;  // End of the plmn_list sequence
                        }

                        if (event.type == YAML_MAPPING_START_EVENT) {
                            // Parse each plmn_list entry
                            LoadRUConfigs(&parser);
                            global_configs.numRUs++;
                           // printf("numRUs : %d\n", global_configs.numRUs);
                        }
                    }
                } 
            }

            // Parse security section
            else if (strcmp(key, "FHI7p2Config") == 0) {
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_MAPPING_START_EVENT) {
                    LoadFhiConfigs(&parser, &cfgptr->yaml_config.fhi_72);
                    global_configs.numfhi_72++;
                } else {
                    //printf("Invalid event type for security, expected a mapping.\n");
                }
            }

            else if (strcmp(key, "generateLogFile") == 0) {
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SCALAR_EVENT) {
                    char* temp = strdup((char *)event.data.scalar.value);
                    if(strcmp(temp,"true") == 0){
                        global_vnf.GenerateLogFile = 1;
                    } else if (strcmp(temp, "false") == 0){
                        global_vnf.GenerateLogFile = 0;
                    } else{
                        SM_Logs(LOG_ERROR,_ARIA_,"Illegal option for Log_File ");
                    }
                } else {
                    //printf("Invalid event type for security, expected a mapping.\n");
                }
            }


            else if (strcmp(key, "logConfig") == 0) {
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_MAPPING_START_EVENT) {
                    fillLogConfigs(&parser, &global_vnf.logConfigs);
                } else {
                    //printf("Invalid event type for security, expected a mapping.\n");
                }
            }

             else if (strcmp(key, "logsFileConfig") == 0) {
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_MAPPING_START_EVENT) {
                    fillLogFileConfigs(&parser, &global_vnf.logFileConfigs);
                } else {
                    //printf("Invalid event type for security, expected a mapping.\n");
                }
            }

            
            if (strcmp(key, "L1StatsUpdateInterval") == 0) {
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SCALAR_EVENT) {
                    char *endptr;
                    global_vnf.L1_stats_update_interval = strtol((char *)event.data.scalar.value, &endptr, 10);

                    if (*endptr != '\0') {
                        printf("Warning: Invalid integer value for L1_Stats_Update_Interval\n");
                        global_vnf.L1_stats_update_interval = 0; // Default or error handling
                    }

                    //printf("The value of stats update interval is: %d\n", global_vnf.L1_stats_update_interval);
                }
            }

            else if (strcmp(key, "duCores") == 0) {
                // Expect a scalar string here
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SCALAR_EVENT) {
                    // Copy the scalar string into the target variable
                    snprintf(global_configs.thread_pool_cores, MAX_CORES, "%s", (char *)event.data.scalar.value);
                } else {
                    fprintf(stderr, "Error: Expected a scalar string for thread_pool_cores\n");
                    exit(EXIT_FAILURE);
                }
            }


        }

        // Cleanup event
        yaml_event_delete(&event);
    }

    // Cleanup parser and file
    yaml_parser_delete(&parser);
    fclose(file);
    return 0;
}



//---------------------------------------------for new config file-----------------------------------------------------------

void new_fill_plmn_list(yaml_parser_t *parser, plmn_list_t *p_plmn) {
    yaml_event_t event;
    GlobC_ARIAConfs.numPlmn = 0;
    while (1) {
        yaml_parser_parse(parser, &event);
        if (event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&event);
            break;  // End of the PLMN mapping
        }
        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            yaml_parser_parse(parser, &event); // Move to value
            if (strcmp(key, "mcc") == 0) {
                 GlobC_ARIAConfs.y_gNBs->p_plmn.MCC = atoi((char *)event.data.scalar.value);
            } else if (strcmp(key, "mnc") == 0) {
                GlobC_ARIAConfs.y_gNBs->p_plmn.MNC = atoi((char *)event.data.scalar.value);
            } else if (strcmp(key, "mncLength") == 0) {
                GlobC_ARIAConfs.y_gNBs->p_plmn.MNC_len = atoi((char *)event.data.scalar.value);
            } else if (strcmp(key, "sst") == 0) {
                GlobC_ARIAConfs.y_gNBs->p_plmn.s_snssai.SvcTypeID = atoi((char *)event.data.scalar.value);
            } else if (strcmp(key, "sd") == 0) {
                GlobC_ARIAConfs.y_gNBs->p_plmn.s_snssai.SliceDiff = strtol((char *)event.data.scalar.value, NULL, 16);
            } 
        }
        yaml_event_delete(&event);  // Ensure to delete the event
    }
}


void new_fill_NCC(yaml_parser_t *parser, plmn_list_t *p_plmn) {
    yaml_event_t event;
    GlobC_ARIAConfs.numPlmn = 0;
    while (1) {
        yaml_parser_parse(parser, &event);
        if (event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&event);
            break;
        }
        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            yaml_parser_parse(parser, &event); // Move to value
            if (strcmp(key, "localIpAddress") == 0) {
                GlobC_ARIAConfs.y_gNBs->HostServIP = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "duIpAddress") == 0) {
                GlobC_ARIAConfs.y_gNBs->ExtPointIP = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "localPort") == 0) {
                GlobC_ARIAConfs.y_gNBs->HostSCrlPort = atoi((char *)event.data.scalar.value);
            } 
            else if (strcmp(key, "duPort") == 0) {
                GlobC_ARIAConfs.y_gNBs->ExtScrlPort = atoi((char *)event.data.scalar.value);
            } 
        }
        yaml_event_delete(&event);  // Ensure to delete the event
    }
}


void new_fill_Nodes(yaml_parser_t *parser, gnb_t *gnb) {
    yaml_event_t event;
    while (1) {
        yaml_parser_parse(parser, &event);
        if (event.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&event);
            break;  // End of the gNB mapping
        }

        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            yaml_parser_parse(parser, &event);  // Parse the value

            // printf("Processing key: %s\n", key);

            if (strcmp(key, "gNBId") == 0) {
                GlobC_ARIAConfs.y_gNBs->NodeKey = strtol((char *)event.data.scalar.value, NULL, 16); // Parsing hex ID
                //printf("NodeKey (hex): 0x%lx\n", GlobC_ARIAConfs.y_gNBs->NodeKey);
                //printf("NodeKey : %d\n", GlobC_ARIAConfs.y_gNBs->NodeKey);
            } else if (strcmp(key, "gNBName") == 0) {
                GlobC_ARIAConfs.y_gNBs->EntityLabel = strdup((char *)event.data.scalar.value);
                GlobC_ARIAConfs.FunctionalNodes = strdup((char *)event.data.scalar.value);
                SM_Log_Assert(_ARIA_,(strcmp(GlobC_ARIAConfs.FunctionalNodes,ARIA_L3_NAME)==0) ,"Illegal File...!!");
                //printf("EntityLabel: %s\n", GlobC_ARIAConfs.y_gNBs->EntityLabel);
            } else if (strcmp(key, "trackingAreaCode") == 0) {
                GlobC_ARIAConfs.y_gNBs->TAC = atoi((char *)event.data.scalar.value);
                //printf("TAC: %d\n", GlobC_ARIAConfs.y_gNBs->TAC);

            }  else if (strcmp(key, "nRCellIdentity") == 0) {
                GlobC_ARIAConfs.y_gNBs->SegmentID = strtol((char *)event.data.scalar.value, NULL, 10);
            }
            // else if (strcmp(key, "RIF_Choice") == 0) {
            //     GlobC_ARIAConfs.y_gNBs->RIF_Choice = strdup((char *)event.data.scalar.value);
            //     printf("RIF_Choice: %s\n", GlobC_ARIAConfs.y_gNBs->RIF_Choice);
            // } 
            else if (strcmp(key, "NodeClass") == 0) {
                GlobC_ARIAConfs.y_gNBs->NodeClass = strdup((char *)event.data.scalar.value);
            } 
        }

        yaml_event_delete(&event);
    }
}


void new_fill_security(yaml_parser_t *parser, security_t *security) {
    yaml_event_t event;
    int cipher_algo_index = 0;
    int integrity_algo_index = 0;
    while (1) {
        yaml_parser_parse(parser, &event);
        if (event.type == YAML_MAPPING_END_EVENT) {
            break;  // End of the mapping
        }

        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            yaml_parser_parse(parser, &event);  // Parse the value

            if (strcmp(key, "CipherModes") == 0) {
                if (event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break;
                        }
                        if (event.type == YAML_SCALAR_EVENT) {
                            if (cipher_algo_index < MAX_ALGO_COUNT) {
                                GlobC_ARIAConfs.y_security.CipherModes[cipher_algo_index] = strdup((char *)event.data.scalar.value);
                                cipher_algo_index++;
                                GlobC_ARIAConfs.numCIPHERalgo = 1;
                            } else {
                                printf("Warning: Maximum number of cipher algorithms reached!\n");
                            }
                        }
                    }
                }
            } else if (strcmp(key, "AuthModes") == 0) {
                if (event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break;
                        }
                        if (event.type == YAML_SCALAR_EVENT) {
                            if (integrity_algo_index < MAX_ALGO_COUNT) {
                                GlobC_ARIAConfs.y_security.AuthModes[integrity_algo_index] = strdup((char *)event.data.scalar.value);
                                integrity_algo_index++;
                                GlobC_ARIAConfs.numINTEGalgo = 2;;
                            } else {
                                printf("Warning: Maximum number of integrity algorithms reached!\n");
                            }
                        }
                    }
                }
            } else if (strcmp(key, "BchannelEncryp") == 0) {
                GlobC_ARIAConfs.y_security.BchannelEncryp = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "Bchannelauthentication") == 0) {
                GlobC_ARIAConfs.y_security.Bchannelauthentication = strdup((char *)event.data.scalar.value);
                printf("DRB Integrity: %s\n", GlobC_ARIAConfs.y_security.Bchannelauthentication);
            }
        }

        yaml_event_delete(&event);
    }
}


void new_fill_log_config(yaml_parser_t *parser) {
    yaml_event_t event;

    for (int i = 0; i < MAX_LOG_PREDEF_COMPONENTS; i++) {
        GlobC_ARIAConfs.y_log_config.log_levels[i] = strdup(DEFAULT_LOG_LEVEL);
    }

    while (1) {
        yaml_parser_parse(parser, &event);
        if (event.type == YAML_MAPPING_END_EVENT) {
            break;  // End of the mapping
        }

        if (event.type == YAML_SCALAR_EVENT) {
            char *key = (char *)event.data.scalar.value;
            yaml_parser_parse(parser, &event);  // Parse the value

            // Map the key to corresponding log level fields
            if (strcmp(key, "global_log_level") == 0) {
                GlobC_ARIAConfs.y_log_config.l_global_log_level = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "hw_log_level") == 0) {
                GlobC_ARIAConfs.y_log_config.log_levels[HW] = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "phy_log_level") == 0) {
                GlobC_ARIAConfs.y_log_config.log_levels[PHY] = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "mac_log_level") == 0) {
                GlobC_ARIAConfs.y_log_config.log_levels[MAC] = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "rlc_log_level") == 0) {
                GlobC_ARIAConfs.y_log_config.log_levels[RLC] = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "pdcp_log_level") == 0) {
                GlobC_ARIAConfs.y_log_config.log_levels[PDCP] = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "rrc_log_level") == 0) {
                GlobC_ARIAConfs.y_log_config.log_levels[RRC] = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "f1ap_log_level") == 0) {
                GlobC_ARIAConfs.y_log_config.log_levels[F1AP] = strdup((char *)event.data.scalar.value);
            } else if (strcmp(key, "l_global_log_options") == 0) {
                GlobC_ARIAConfs.y_log_config.l_global_log_options = strdup((char *)event.data.scalar.value);
            }
        }

        yaml_event_delete(&event);  // Clean up after every event
    }
}


// Initialize default values directly
void initializeDefaultValues() {
    GlobC_ARIAConfs.FunctionalNodes = strdup("DefaultNode");
    GlobC_ARIAConfs.active_gnb_count = 1; 

    // Fill default values
    GlobC_ARIAConfs.y_defvalues.LOC_S_PORTD = 50001;
    GlobC_ARIAConfs.y_defvalues.REM_S_PORTC = 50000;
    GlobC_ARIAConfs.y_defvalues.sdapflag = strdup("disabled");
    GlobC_ARIAConfs.y_defvalues.UMONDEFAULTDRD = 0;
    GlobC_ARIAConfs.y_defvalues.D_DRBs = 1;
    GlobC_ARIAConfs.y_defvalues.Default_DRBs = 0;
    GlobC_ARIAConfs.numTHREADParamList = 0;
    GlobC_ARIAConfs.numGNBparmlist = 1;
    GlobC_ARIAConfs.numplmnlist = 1;
    GlobC_ARIAConfs.numSNSSAIlist = 1;
    GlobC_ARIAConfs.numNGparamlist = 1;
    GlobC_ARIAConfs.y_defvalues.Cell_typ = strdup("CELL_MACRO_GNB");
    global_configs.defvals.OPT_typ = 3;
    GlobC_ARIAConfs.CUActive = true;
    global_configs.L1DUactive = false;
    GlobC_ARIAConfs.y_gNBs->EntityLabel = strdup("gNB-Eurecom-CU");
    GlobC_ARIAConfs.y_defvalues.GNBcuupID = 1;
    GlobC_ARIAConfs.y_defvalues.cucp_ipv4 = NULL;
    GlobC_ARIAConfs.y_defvalues.cuup_ipv4 = NULL;
    global_configs.defvals.NFAPI_idx = 27;
    global_configs.defvals.NFAPI_mde = 0;
    global_vnf.numactiveGNBs = 1;
    global_configs.U_SF_record = 0;
    global_configs.U_SF_replay = 0;
    global_configs.use_mmap = 1;
    GlobC_ARIAConfs.y_gNBs->RIF_Choice = strdup("f1");
    GlobC_ARIAConfs.y_gNBs->HostSdtPort = 2152;
    GlobC_ARIAConfs.y_gNBs->ExtSdtPort = 2152;
}

void fillSecurityConfigs(){

    GlobC_ARIAConfs.y_security.CipherModes[0] = strdup("nea0");
   // GlobC_ARIAConfs.y_security.CipherModes[]
    GlobC_ARIAConfs.numCIPHERalgo = 1;
    GlobC_ARIAConfs.y_security.AuthModes[0] = strdup("nia2");
    GlobC_ARIAConfs.y_security.AuthModes[1] = strdup("nia0");

    GlobC_ARIAConfs.numINTEGalgo = 2;
    GlobC_ARIAConfs.y_security.BchannelEncryp = strdup("yes");
    GlobC_ARIAConfs.y_security.Bchannelauthentication = strdup("no");
    GlobC_ARIAConfs.numSECURITY = 1;

}

int GLOBALCUConfs(config_yaml_t yaml_config, const char *filename) {
    //printf("\ntesting--> inside YamlGlobalStruct\n");

    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("Failed to open YAML file: %s\n", filename);
        return;
    }

    yaml_parser_t parser;
    yaml_event_t event;

    // Initialize YAML parser
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_file(&parser, file);
    fillSecurityConfigs();
    
    // Variables for gNB counter
    int gnb_index = 0;
    GlobC_ARIAConfs.active_gnb_count = 0;  

    // Allocate initial memory for FunctionalNodes
    yaml_config.FunctionalNodes = malloc(sizeof(char *) * 1); // Allocate space for one gNB

    // Parse through the YAML file
    while (1) {
        yaml_parser_parse(&parser, &event);

        if (event.type == YAML_STREAM_END_EVENT) {
            break; // End of the YAML stream
        }

        if (event.type == YAML_SCALAR_EVENT) {
            // Capture the current key
            char *key = (char *)event.data.scalar.value;


            // Parse Asn1Granularity section
            if (strcmp(key, "Asn1Granularity") == 0) {
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SCALAR_EVENT) {
                    GlobC_ARIAConfs.Asn1Granularity = strdup("NONE");
                    //printf("Asn1Granularity : %s\n", GlobC_ARIAConfs.Asn1Granularity);
                }
            }

            else if (strcmp(key, "ReleaseVersion") == 0) {
               yaml_parser_parse(&parser, &event);
               if (event.type == YAML_SCALAR_EVENT) {
                   char *endptr;
                   GlobC_ARIAConfs.ReleaseVersion = strdup((char *)event.data.scalar.value);
                   //printf("ReleaseVersion : %s\n", global_vnf.ReleaseVersion);
                   //printf("Value of ARIA_VERSION: %s\n",ARIA_VERSION);
                  initializeDefaultValues();
                   SM_Log_Assert(_ARIA_, strcmp(GlobC_ARIAConfs.ReleaseVersion, ARIA_VERSION) == 0, "Illegal File...!!\n");
                    
                    //printf("Value of ARIA_VERSION: %s\n",ARIA_VERSION);
                    
                 }
           }





            // Parse Nodes section
            else if (strcmp(key, "gNodeB-CU") == 0) {
               // printf("Found Nodes section, calling fill_Nodes\n\n");
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(&parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break; // End of Nodes sequence
                        }
                        if (event.type == YAML_MAPPING_START_EVENT) {
                            // Call fill_Nodes for each Nodes in the list
                            new_fill_Nodes(&parser,yaml_config.y_gNBs);
                            gnb_index++;
                        }
                    }
                }
            }

             else if (strcmp(key, "ngapConfig") == 0) {
                    yaml_parser_parse(&parser, &event);  
                    while (event.type != YAML_MAPPING_END_EVENT) {
                        if (event.type == YAML_SCALAR_EVENT) {
                            char *key = (char *)event.data.scalar.value;
                            yaml_parser_parse(&parser, &event);

                            if (strcmp(key, "n2IpAddress") == 0) {
                                GlobC_ARIAConfs.y_gNBs->n_net_if.NgAmfDevIPv4 = strdup((char *)event.data.scalar.value);
                            } else if (strcmp(key, "n3IpAddress") == 0) {
                                GlobC_ARIAConfs.y_gNBs->n_net_if.NguDevIPv4 = strdup((char *)event.data.scalar.value);
                            } else if (strcmp(key, "S1uPort") == 0) {
                                GlobC_ARIAConfs.y_gNBs->n_net_if.S1uPort = atoi((char *)event.data.scalar.value);
                            } else if (strcmp(key, "amfN2IpAddress") == 0) {
                                GlobC_ARIAConfs.y_gNBs->a_amf_ip.AMFDevIPv4 = strdup((char *)event.data.scalar.value);
                            } 
                            else if (strcmp(key, "sctpRxStream") == 0) {
                                GlobC_ARIAConfs.y_gNBs->C_SCTP.FlexReceptionStreamSet = atoi((char *)event.data.scalar.value);
                            } else if (strcmp(key, "sctpTxStream") == 0) {
                                            
                                GlobC_ARIAConfs.y_gNBs->C_SCTP.FlexTransmissionStreamSet = atoi((char *)event.data.scalar.value);
                            }
                            
                           
                        }       
                        yaml_parser_parse(&parser, &event); 
                    }
                }

            else if (strcmp(key, "f1apConfig") == 0) {
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_MAPPING_START_EVENT) {
                    new_fill_NCC(&parser,&yaml_config.y_security);
                } else {
                   // //printf("Invalid event type for security, expected a mapping.\n");
                }
            }

            else if (strcmp(key, "plmnConfig") == 0) {

                // Expect a sequence here
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SEQUENCE_START_EVENT) {
                    while (1) {
                        yaml_parser_parse(&parser, &event);
                        if (event.type == YAML_SEQUENCE_END_EVENT) {
                            break;  // End of the plmn_list sequence
                        }

                        if (event.type == YAML_MAPPING_START_EVENT) {
                            // Parse each plmn_list entry
                            new_fill_plmn_list(&parser,&yaml_config.y_gNBs->p_plmn);
                            GlobC_ARIAConfs.numPlmn++;
                           // printf("numplmn : %d\n", GlobC_ARIAConfs.numPlmn);
                        }
                    }
                } 
            }

            else if (strcmp(key, "log_config") == 0) {
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_MAPPING_START_EVENT) {
                    new_fill_log_config(&parser);
                }
            }

            else if (strcmp(key, "logConfig") == 0) {
              //  printf("Found Log_config_List\n\n");
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_MAPPING_START_EVENT) {
                    fillLogConfigs(&parser, &global_vnf.logConfigs);
                    
                } else {
                    //printf("Invalid event type for security, expected a mapping.\n");
                }
            }

            else if (strcmp(key, "logFileConfig") == 0) {
               // printf("Found Log_config_List\n\n");
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_MAPPING_START_EVENT) {
                    fillLogFileConfigs(&parser, &global_vnf.logFileConfigs);
                    
                } else {
                    //printf("Invalid event type for security, expected a mapping.\n");
                }
            }

            else if (strcmp(key, "generateLogFile") == 0) {
               // printf("Found LOG_FILE\n\n");
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SCALAR_EVENT) {
                    char* temp = strdup((char *)event.data.scalar.value);
                    if(strcmp(temp,"true") == 0){
                        //printf("I'm inside 1: \n");
                        global_vnf.GenerateLogFile = 1;
                    } else if (strcmp(temp, "false") == 0){
                        //printf("I'm inside 0: \n");
                        global_vnf.GenerateLogFile = 0;
                    } else{
                        SM_Logs(LOG_ERROR,_ARIA_,"Illegal option for Log_File ");
                    }
                } else {
                    //printf("Invalid event type for security, expected a mapping.\n");
                }
            }

            else if (strcmp(key, "isolatedCores") == 0) {
                printf("Found isolatedCores section\n\n");

                // Expect a scalar string here
                yaml_parser_parse(&parser, &event);
                if (event.type == YAML_SCALAR_EVENT) {
                    // Copy the scalar string into the target variable
                    snprintf(global_configs.thread_pool_cores, MAX_CORES, "%s", (char *)event.data.scalar.value);
                    printf("thread_pool_cores string: %s\n", global_configs.thread_pool_cores);
                } else {
                    fprintf(stderr, "Error: Expected a scalar string for thread_pool_cores\n");
                    exit(EXIT_FAILURE);
                }
            }

            else if (strcmp(key, "cuConfig") == 0) {  
                 yaml_parser_parse(&parser, &event);
                 while (event.type != YAML_MAPPING_END_EVENT) {
                     if (event.type == YAML_SCALAR_EVENT) {
                         char *cu_key = (char *)event.data.scalar.value;
                         yaml_parser_parse(&parser, &event);
             
                         if (strcmp(cu_key, "sdapMode") == 0) {
                             GlobC_ARIAConfs.SDAPMode = strdup((char *)event.data.scalar.value);
                            //  printf("The value of GlobC_ARIAConfs.SDAPMode: %s\n", GlobC_ARIAConfs.SDAPMode);
             
                             if (strcmp(GlobC_ARIAConfs.SDAPMode, "enable") == 0) {
                                 SM_Logs(LOG_INFO, _ARIA_, "SDAP Enabled mode is not implemented yet");
                             }
                         }
                         else if (strcmp(cu_key, "isolatedCores") == 0) {

                                snprintf(global_configs.thread_pool_cores, MAX_CORES, "%s", (char *)event.data.scalar.value);
                            yaml_event_delete(&event);
                        }
                     }
                     yaml_parser_parse(&parser, &event);
                 }
             }
        }

        // Cleanup event
        yaml_event_delete(&event);
    }
            
    // Cleanup parser and file
    yaml_parser_delete(&parser);
    fclose(file);
    return 0;
}
        
    













void write_parsedcfg(configmodule_interface_t *cfgptr)
{
  if (cfgptr->status && (cfgptr->rtflags & CONFIG_SAVERUNCFG)) {

  }
  if (cfgptr->write_parsedcfg != NULL) {
    cfgptr->write_parsedcfg(cfgptr);
  }
}

void end_configmodule(configmodule_interface_t *cfgptr)
{
  if (cfgptr != NULL) {
    write_parsedcfg(cfgptr);
    if (cfgptr->end != NULL) {
      cfgptr->end(cfgptr);
    }

    pthread_mutex_lock(&cfgptr->memBlocks_mutex);

    for(int i=0; i<cfgptr->numptrs ; i++) {
      if (cfgptr->oneBlock[i].ptrs != NULL && cfgptr->oneBlock[i].ptrsAllocated== true && cfgptr->oneBlock[i].toFree) {
        free(cfgptr->oneBlock[i].ptrs);
        memset(&cfgptr->oneBlock[i], 0, sizeof(cfgptr->oneBlock[i]));
      }
    }
    
    cfgptr->numptrs=0;
    pthread_mutex_unlock(&cfgptr->memBlocks_mutex);
    if (cfgptr->cfgmode)
      free(cfgptr->cfgmode);

    if (cfgptr->argv_info)
      free(cfgptr->argv_info);

    free(cfgptr);
  }
}
