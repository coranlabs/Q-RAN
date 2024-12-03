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

/*! \file log.h
* \brief openair log generator (OLG) for
* \author Navid Nikaein
* \date 2009 - 2014
* \version 0.5
* @ingroup util
*/

#ifndef __LOG_H__
#define __LOG_H__

/*--- INCLUDES ---------------------------------------------------------------*/
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <stdint.h>
#ifndef __STDC_FORMAT_MACROS
  #define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>
#ifndef _GNU_SOURCE
  #define _GNU_SOURCE
#endif
#include <pthread.h>
#include <common/utils/utils.h>
#if ENABLE_LTTNG
#include "lttng-log.h"
#endif
/*----------------------------------------------------------------------------*/
#include <assert.h>
#ifdef NDEBUG
#warning assert is disabled
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup _max_length Maximum Length of LOG
 *  @ingroup _macro
 *  @brief the macros that describe the maximum length of LOG
 * @{*/

#define MAX_LOG_TOTAL 16384 /*!< \brief the maximum length of a log */
/** @}*/

/** @defgroup _log_level Message levels defined by LOG
 *  @ingroup _macro
 *  @brief LOG defines 9 levels of messages for users. Importance of these levels decrease gradually from 0 to 8
 * @{*/
# define  OAILOG_DISABLE -1 /*!< \brief disable all LOG messages, cannot be used in LOG macros, use only in LOG module */
# define  OAILOG_ERR      0 /*!< \brief critical error conditions, impact on "must have" functionalities */
# define  OAILOG_WARNING  1 /*!< \brief warning conditions, shouldn't happen but doesn't impact "must have" functionalities */
# define  OAILOG_ANALYSIS 2 /*!< \brief informational messages most people don't need, shouldn't impact real-time behavior */
# define  OAILOG_INFO     3 /*!< \brief informational messages most people don't need, shouldn't impact real-time behavior */
# define  OAILOG_DEBUG    4 /*!< \brief first level debug-level messages, for developers, may impact real-time behavior */
# define  OAILOG_TRACE    5 /*!< \brief second level debug-level messages, for developers, likely impact real-time behavior*/

#define NUM_LOG_LEVEL 6 /*!< \brief the number of message levels users have with LOG (OAILOG_DISABLE is not available to user as a level, so it is not included)*/
/** @}*/

#define SET_LOG_OPTION(O)   g_log->flag = (g_log->flag | O)
#define CLEAR_LOG_OPTION(O) g_log->flag = (g_log->flag & (~O))

/** @defgroup macros to identify a debug entity
 *  @ingroup each macro is a bit mask where the unique bit set identifies an entity to be debugged
 *            it allows to dynamically activate or not blocks of code. The  LOG_MASKMAP_INIT macro
 *            is used to map a character string name to each debug bit, it allows to set or clear
 *            the corresponding bit via the defined name, from the configuration or from the telnet
 *            server.
 *  @brief
 * @{*/
#define DEBUG_PRACH        (1<<0)
#define DEBUG_RU           (1<<1)
#define DEBUG_UE_PHYPROC   (1<<2)
#define DEBUG_LTEESTIM     (1<<3)
#define DEBUG_DLCELLSPEC   (1<<4)
#define DEBUG_ULSCH        (1<<5)
#define DEBUG_RRC          (1<<6)
#define DEBUG_PDCP         (1<<7)
#define DEBUG_DFT          (1<<8)
#define DEBUG_ASN1         (1<<9)
#define DEBUG_CTRLSOCKET   (1<<10)
#define DEBUG_SECURITY     (1<<11)
#define DEBUG_NAS          (1<<12)
#define DEBUG_RLC          (1<<13)
#define DEBUG_DLSCH_DECOD  (1<<14)
#define UE_TIMING          (1<<20)
// #define DEBUG_DTLS         (1<<15)


#define SET_LOG_DEBUG(B)   g_log->debug_mask = (g_log->debug_mask | B)
#define CLEAR_LOG_DEBUG(B) g_log->debug_mask = (g_log->debug_mask & (~B))

#define SET_LOG_DUMP(B)   g_log->dump_mask = (g_log->dump_mask | B)
#define CLEAR_LOG_DUMP(B) g_log->dump_mask = (g_log->dump_mask & (~B))

#define FOREACH_COMP(COMP_DEF)  \
  COMP_DEF(PHY, log)            \
  COMP_DEF(MAC, log)            \
  COMP_DEF(EMU, log)            \
  COMP_DEF(SIM, txt)            \
  COMP_DEF(OMG, csv)            \
  COMP_DEF(OPT, log)            \
  COMP_DEF(OTG, log)            \
  COMP_DEF(OTG_LATENCY, dat)    \
  COMP_DEF(OTG_LATENCY_BG, dat) \
  COMP_DEF(OTG_GP, dat)         \
  COMP_DEF(OTG_GP_BG, dat)      \
  COMP_DEF(OTG_JITTER, dat)     \
  COMP_DEF(RLC, )               \
  COMP_DEF(PDCP, )              \
  COMP_DEF(DTLS, )              \
  COMP_DEF(RRC, )               \
  COMP_DEF(NAS, log)            \
  COMP_DEF(OIP, )               \
  COMP_DEF(CLI, )               \
  COMP_DEF(OCM, )               \
  COMP_DEF(GTPU, )              \
  COMP_DEF(SDAP, )              \
  COMP_DEF(SPGW, )              \
  COMP_DEF(S1AP, )              \
  COMP_DEF(F1AP, )              \
  COMP_DEF(E1AP, )              \
  COMP_DEF(SCTP, )              \
  COMP_DEF(HW, )                \
  COMP_DEF(OSA, )               \
  COMP_DEF(ENB_APP, log)        \
  COMP_DEF(MCE_APP, log)        \
  COMP_DEF(MME_APP, log)        \
  COMP_DEF(TMR, )               \
  COMP_DEF(USIM, log)           \
  COMP_DEF(F1U, )               \
  COMP_DEF(X2AP, )              \
  COMP_DEF(M2AP, )              \
  COMP_DEF(M3AP, )              \
  COMP_DEF(NGAP, )              \
  COMP_DEF(GNB_APP, log)        \
  COMP_DEF(NR_RRC, log)         \
  COMP_DEF(NR_MAC, log)         \
  COMP_DEF(NR_MAC_DCI, log)         \
  COMP_DEF(NR_PHY_DCI, log)         \
  COMP_DEF(NR_PHY, log)         \
  COMP_DEF(LOADER, log)         \
  COMP_DEF(ASN1, log)           \
  COMP_DEF(NFAPI_VNF, log)      \
  COMP_DEF(NFAPI_PNF, log)      \
  COMP_DEF(ITTI, log)           \
  COMP_DEF(UTIL, log)           \
  COMP_DEF(MAX_LOG_PREDEF_COMPONENTS, )

#define COMP_ENUM(comp, file_extension) comp,
typedef enum { FOREACH_COMP(COMP_ENUM) } comp_name_t;

#define COMP_TEXT(comp, file_extension) #comp,
static const char *const comp_name[] = {FOREACH_COMP(COMP_TEXT)};

#define COMP_EXTENSION(comp, file_extension) #file_extension,
static const char *const comp_extension[] = {FOREACH_COMP(COMP_EXTENSION)};

#define MAX_LOG_DYNALLOC_COMPONENTS 20
#define MAX_LOG_COMPONENTS (MAX_LOG_PREDEF_COMPONENTS + MAX_LOG_DYNALLOC_COMPONENTS)

typedef struct {
  char *name; /*!< \brief string name of item */
  int value;  /*!< \brief integer value of mapping */
} mapping;

typedef int(*log_vprint_func_t)(FILE *stream, const char *format, va_list ap );
typedef int(*log_print_func_t)(FILE *stream, const char *format, ... );
typedef struct {
  int savedlevel;
  char *filelog_name;
} log_component_back_t;

typedef struct  {
  const char        *name;
  int level;
  int filelog;
  FILE              *stream;
  log_vprint_func_t vprint;
  log_print_func_t print;
} log_component_t;


typedef struct {
  log_component_t         log_component[MAX_LOG_COMPONENTS];
  log_component_back_t log_rarely_used[MAX_LOG_COMPONENTS];
  char                    level2string[NUM_LOG_LEVEL];
  int                     flag;
  char                   *filelog_name;
  uint64_t                debug_mask;
  uint64_t                dump_mask;
} log_t;

#ifdef LOG_MAIN
log_t *g_log;
#else
#ifdef __cplusplus
extern "C" {
#endif
  extern log_t *g_log;
#ifdef __cplusplus
}
#endif
#endif

/*----------------------------------------------------------------------------*/
int  logInit (void);
void logTerm (void);
int  isLogInitDone (void);
void logRecord_mt(const char *file, const char *func, int line,int comp, int level, const char *format, ...) __attribute__ ((format (printf, 6, 7)));
void vlogRecord_mt(const char *file, const char *func, int line, int comp, int level, const char *format, va_list args );
#if ENABLE_LTTNG
void logRecord_lttng(const char *file, const char *func, int line, int comp, int level, const char *format, ...)
    __attribute__((format(printf, 6, 7)));
#endif
void log_dump(int component, void *buffer, int buffsize,int datatype, const char *format, ... );
int  set_log(int component, int level);
void set_glog(int level);

mapping * log_level_names_ptr(void);
mapping * log_option_names_ptr(void);
mapping * log_maskmap_ptr(void);
void set_glog_onlinelog(int enable);
void set_glog_filelog(int enable);
void set_component_filelog(int comp);
void close_component_filelog(int comp);
void set_component_consolelog(int comp);
int map_str_to_int(const mapping *map, const char *str);
char *map_int_to_str(const mapping *map, const int val);
void logClean (void);

int register_log_component(const char *name, const char *fext, int compidx);

int logInit_log_mem(char*);
void close_log_mem(void);

/** @}*/

/*!\fn int32_t write_file_matlab(const char *fname, const char *vname, void *data, int length, int dec, char format);
\brief Write output file from signal data
@param fname output file name
@param vname  output vector name (for MATLAB/OCTAVE)
@param data   point to data
@param length length of data vector to output
@param dec    decimation level
@param format data format (0 = real 16-bit, 1 = complex 16-bit,2 real 32-bit, 3 complex 32-bit,4 = real 8-bit, 5 = complex 8-bit)
@param multiVec create new file or append to existing (useful for writing multiple vectors to same file. Just call the function multiple times with same file name and with this parameter set to 1)
*/
#define MATLAB_RAW (1U<<31)
#define MATLAB_SHORT 0
#define MATLAB_CSHORT 1
#define MATLAB_INT 2
#define MATLAB_CINT 3
#define MATLAB_INT8 4
#define MATLAB_CINT8 5
#define MATLAB_LLONG 6
#define MATLAB_DOUBLE 7
#define MATLAB_CDOUBLE 8
#define MATLAB_UINT8 9
#define MATLEB_EREN1 10
#define MATLEB_EREN2 11
#define MATLEB_EREN3 12
#define MATLAB_CSHORT_BRACKET1 13
#define MATLAB_CSHORT_BRACKET2 14
#define MATLAB_CSHORT_BRACKET3 15
  
int32_t write_file_matlab(const char *fname, const char *vname, void *data, int length, int dec, unsigned int format, int multiVec);
#define write_output(a, b, c, d, e, f) write_file_matlab(a, b, c, d, e, f, 0)

/*----------------macro definitions for reading log configuration from the config module */
#define CONFIG_STRING_LOG_PREFIX                           "log_config"

#define LOG_CONFIG_STRING_GLOBAL_LOG_LEVEL                 "global_log_level"
#define LOG_CONFIG_STRING_GLOBAL_LOG_ONLINE                "global_log_online"
#define LOG_CONFIG_STRING_GLOBAL_LOG_INFILE                "global_log_infile"
#define LOG_CONFIG_STRING_GLOBAL_LOG_OPTIONS               "global_log_options"

#define LOG_CONFIG_LEVEL_FORMAT                            "%s_log_level"
#define LOG_CONFIG_LOGFILE_FORMAT                          "%s_log_infile"
#define LOG_CONFIG_DEBUG_FORMAT                            "%s_debug"
#define LOG_CONFIG_DUMP_FORMAT                             "%s_dump"

#define LOG_CONFIG_HELP_OPTIONS      " list of comma separated options to enable log module behavior. Available options: \n"\
  " nocolor:   disable color usage in log messages\n"\
  " level:     add log level indication in log messages\n"\
  " thread:    add threads names in log messages\n"

/*-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
/*   LOG global configuration parameters                                                                                                                                                */
/*   optname                               help                                          paramflags         XXXptr               defXXXval                          type        numelt */
/*-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------*/
// clang-format off
#define LOG_GLOBALPARAMS_DESC { \
  {LOG_CONFIG_STRING_GLOBAL_LOG_LEVEL,   "Default log level for all componemts\n",              0, .strptr=&gloglevel,         .defstrval=log_level_names[3].name, TYPE_STRING,     0}, \
  {LOG_CONFIG_STRING_GLOBAL_LOG_ONLINE,  "Default console output option, for all components\n", 0, .iptr=&(consolelog),        .defintval=1,                       TYPE_INT,        0}, \
  {LOG_CONFIG_STRING_GLOBAL_LOG_OPTIONS, LOG_CONFIG_HELP_OPTIONS,                               0, .strlistptr=NULL,           .defstrlistval=NULL,                TYPE_STRINGLIST, 0}, \
}
// clang-format on

#define LOG_OPTIONS_IDX   2


/*----------------------------------------------------------------------------------*/
/** @defgroup _debugging debugging macros
 *  @ingroup _macro
 *  @brief Macro used to call logIt function with different message levels
 * @{*/
#define LOG_DUMP_CHAR       0
#define LOG_DUMP_DOUBLE     1
// debugging macros
#define LOG_F  LOG_I           /* because  LOG_F was originaly to dump a message or buffer but is also used as a regular level...., to dump use LOG_DUMPMSG */

#  if T_TRACER
#include "T.h"
/* per component, level dependent macros */
// #define LOG_E(c, x...)                                                    \
//   do {                                                                    \
//     T(T_LEGACY_##c##_ERROR, T_PRINTF(x));                                 \
//     if (T_stdout) {                                                       \
//       if (g_log->log_component[c].level >= OAILOG_ERR)                    \
//         logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_ERR, x); \
//     }                                                                     \
//   } while (0)

// #define LOG_W(c, x...)                                                        \
//   do {                                                                        \
//     T(T_LEGACY_##c##_WARNING, T_PRINTF(x));                                   \
//     if (T_stdout) {                                                           \
//       if (g_log->log_component[c].level >= OAILOG_WARNING)                    \
//         logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_WARNING, x); \
//     }                                                                         \
//   } while (0)

// #define LOG_A(c, x...)                                                         \
//   do {                                                                         \
//     T(T_LEGACY_##c##_INFO, T_PRINTF(x));                                       \
//     if (T_stdout) {                                                            \
//       if (g_log->log_component[c].level >= OAILOG_ANALYSIS)                    \
//         logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_ANALYSIS, x); \
//     }                                                                          \
//   } while (0)

// #define LOG_I(c, x...)                                                     \
//   do {                                                                     \
//     T(T_LEGACY_##c##_INFO, T_PRINTF(x));                                   \
//     if (T_stdout) {                                                        \
//       if (g_log->log_component[c].level >= OAILOG_INFO)                    \
//         logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_INFO, x); \
//     }                                                                      \
//   } while (0)

// #define LOG_D(c, x...)                                                      \
//   do {                                                                      \
//     T(T_LEGACY_##c##_DEBUG, T_PRINTF(x));                                   \
//     if (T_stdout) {                                                         \
//       if (g_log->log_component[c].level >= OAILOG_DEBUG)                    \
//         logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_DEBUG, x); \
//     }                                                                       \
//   } while (0)

// #define LOG_DDUMP(c, b, s, f, x...)                      \
//   do {                                                   \
//     T(T_LEGACY_##c##_DEBUG, T_PRINTF(x));                \
//     if (T_stdout) {                                      \
//       if (g_log->log_component[c].level >= OAILOG_DEBUG) \
//         log_dump(c, b, s, f, x);                         \
//     }                                                    \
//   } while (0)

// #define LOG_T(c, x...)                                                      \
//   do {                                                                      \
//     T(T_LEGACY_##c##_TRACE, T_PRINTF(x));                                   \
//     if (T_stdout) {                                                         \
//       if (g_log->log_component[c].level >= OAILOG_TRACE)                    \
//         logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_TRACE, x); \
//     }                                                                       \
//   } while (0)

// #define VLOG(c, l, f, args)                                             \
//   do {                                                                  \
//     if (T_stdout) {                                                     \
//       if (g_log->log_component[c].level >= l)                           \
//         vlogRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, l, f, args); \
//     }                                                                   \
//   } while (0)

// /* macro used to dump a buffer or a message as in openair2/RRC/LTE/RRC_eNB.c, replaces LOG_F macro */
// #define LOG_DUMPMSG(c, f, b, s, x...)      \
//   do {                                     \
//     if (g_log->dump_mask & f)              \
//       log_dump(c, b, s, LOG_DUMP_CHAR, x); \
//   } while (0)


#define LOG_E(c, x...) do {} while (0)
#define LOG_W(c, x...) do {} while (0)
#define LOG_A(c, x...) do {} while (0)
#define LOG_I(c, x...) do {} while (0)
#define LOG_D(c, x...) do {} while (0)
#define LOG_DDUMP(c, b, s, f, x...) do {} while (0)
#define LOG_T(c, x...) do {} while (0)
#define VLOG(c, l, f, args) do {} while (0)
#define LOG_DUMPMSG(c, f, b, s, x...) do {} while (0)

/* bitmask dependent macros, to isolate debugging code */
#define LOG_DEBUGFLAG(D) (g_log->debug_mask & D)

/* bitmask dependent macros, to generate debug file such as matlab file or message dump */
#define LOG_DUMPFLAG(D) (g_log->dump_mask & D)

#define LOG_M(file, vector, data, len, dec, format)             \
  do {                                                          \
    write_file_matlab(file, vector, data, len, dec, format, 0); \
  } while (0)

/* define variable only used in LOG macro's */
#define LOG_VAR(A, B) A B
#else /* T_TRACER */
#if ENABLE_LTTNG
// #define LOG_E(c, x...)                                                     \
//   do {                                                                     \
//     if (g_log->log_component[c].level >= OAILOG_ERR) {                     \
//       logRecord_lttng(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_ERR, x); \
//     }                                                                      \
//   } while (0)
// #define LOG_W(c, x...)                                                         \
//   do {                                                                         \
//     if (g_log->log_component[c].level >= OAILOG_WARNING) {                     \
//       logRecord_lttng(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_WARNING, x); \
//     }                                                                          \
//   } while (0)
// #define LOG_A(c, x...)                                                          \
//   do {                                                                          \
//     if (g_log->log_component[c].level >= OAILOG_ANALYSIS) {                     \
//       logRecord_lttng(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_ANALYSIS, x); \
//     }                                                                           \
//   } while (0)
// #define LOG_I(c, x...)                                                      \
//   do {                                                                      \
//     if (g_log->log_component[c].level >= OAILOG_INFO) {                     \
//       logRecord_lttng(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_INFO, x); \
//     }                                                                       \
//   } while (0)
// #define LOG_D(c, x...)                                                       \
//   do {                                                                       \
//     if (g_log->log_component[c].level >= OAILOG_DEBUG) {                     \
//       logRecord_lttng(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_DEBUG, x); \
//     }                                                                        \
//   } while (0)
// #define LOG_T(c, x...)                                                       \
//   do {                                                                       \
//     if (g_log->log_component[c].level >= OAILOG_TRACE) {                     \
//       logRecord_lttng(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_TRACE, x); \
//     }                                                                        \
//   } while (0)
// #define LOG_DDUMP(c, b, s, f, x...)                    \
//   do {                                                 \
//     if (g_log->log_component[c].level >= OAILOG_DEBUG) \
//       log_dump(c, b, s, f, x);                         \
//   } while (0)

#define LOG_E(c, x...) do {} while (0)
#define LOG_W(c, x...) do {} while (0)
#define LOG_A(c, x...) do {} while (0)
#define LOG_I(c, x...) do {} while (0)
#define LOG_D(c, x...) do {} while (0)
#define LOG_DDUMP(c, b, s, f, x...) do {} while (0)
#define LOG_T(c, x...) do {} while (0)

#else
// #define LOG_E(c, x...)                                                  \
//   do {                                                                  \
//     if (g_log->log_component[c].level >= OAILOG_ERR)                    \
//       logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_ERR, x); \
//   } while (0)

// #define LOG_W(c, x...)                                                      \
//   do {                                                                      \
//     if (g_log->log_component[c].level >= OAILOG_WARNING)                    \
//       logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_WARNING, x); \
//   } while (0)

// #define LOG_A(c, x...)                                                       \
//   do {                                                                       \
//     if (g_log->log_component[c].level >= OAILOG_ANALYSIS)                    \
//       logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_ANALYSIS, x); \
//   } while (0)

// #define LOG_I(c, x...)                                                   \
//   do {                                                                   \
//     if (g_log->log_component[c].level >= OAILOG_INFO)                    \
//       logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_INFO, x); \
//   } while (0)

// #define LOG_D(c, x...)                                                    \
//   do {                                                                    \
//     if (g_log->log_component[c].level >= OAILOG_DEBUG)                    \
//       logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_DEBUG, x); \
//   } while (0)

// #define LOG_DDUMP(c, b, s, f, x...)                    \
//   do {                                                 \
//     if (g_log->log_component[c].level >= OAILOG_DEBUG) \
//       log_dump(c, b, s, f, x);                         \
//   } while (0)

// #define LOG_T(c, x...)                                                    \
//   do {                                                                    \
//     if (g_log->log_component[c].level >= OAILOG_TRACE)                    \
//       logRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, OAILOG_TRACE, x); \
//   } while (0)

#define LOG_E(c, x...) do {} while (0)
#define LOG_W(c, x...) do {} while (0)
#define LOG_A(c, x...) do {} while (0)
#define LOG_I(c, x...) do {} while (0)
#define LOG_D(c, x...) do {} while (0)
#define LOG_DDUMP(c, b, s, f, x...) do {} while (0)
#define LOG_T(c, x...) do {} while (0)

#endif

#define VLOG(c, l, f, args)                                           \
  do {                                                                \
    if (g_log->log_component[c].level >= l)                           \
      vlogRecord_mt(__FILE__, __FUNCTION__, __LINE__, c, l, f, args); \
  } while (0)

#define nfapi_log(FILE, FNC, LN, COMP, LVL, FMT...)
#define LOG_DEBUGFLAG(D) (g_log->debug_mask & D)
#define LOG_DUMPFLAG(D) (g_log->dump_mask & D)
#define LOG_DUMPMSG(c, f, b, s, x...)      \
  do {                                     \
    if (g_log->dump_mask & f)              \
      log_dump(c, b, s, LOG_DUMP_CHAR, x); \
  } while (0) /* */

#define LOG_M(file, vector, data, len, dec, format)             \
  do {                                                          \
    write_file_matlab(file, vector, data, len, dec, format, 0); \
  } while (0)

#define LOG_VAR(A, B) A B
#define T_ACTIVE(a) (0)

#endif /* T_TRACER */

/* avoid warnings for variables only used in LOG macro's but set outside debug section */
#define GCC_NOTUSED   __attribute__((unused))
#define LOG_USEDINLOG_VAR(A,B) GCC_NOTUSED A B

/* unfiltered macros, useful for simulators or messages at init time, before log is configured */
#define LOG_UM(file, vector, data, len, dec, format) do { write_file_matlab(file, vector, data, len, dec, format, 0);} while(0)
#define LOG_UI(c, x...) do {logRecord_mt(__FILE__, __FUNCTION__, __LINE__,c, OAILOG_INFO, x) ; } while(0)
#define LOG_UDUMPMSG(c, b, s, f, x...) do { log_dump(c, b, s, f, x)  ;}   while (0)  /* */
#    define LOG_MM(file, vector, data, len, dec, format) do { write_file_matlab(file, vector, data, len, dec, format, 1);} while(0)
/** @}*/

/** @defgroup _useful_functions useful functions in LOG
 *  @ingroup _macro
 *  @brief Macro of some useful functions defined by LOG
 * @{*/
#define LOG_ENTER(c) do {LOG_T(c, "Entering %s\n",__FUNCTION__);}while(0) /*!< \brief Macro to log a message with severity DEBUG when entering a function */
#define LOG_END(c) do {LOG_T(c, "End of  %s\n",__FUNCTION__);}while(0) /*!< \brief Macro to log a message with severity DEBUG when entering a function */
#define LOG_EXIT(c)  do { LOG_END(c); return;}while(0)  /*!< \brief Macro to log a message with severity TRACE when exiting a function */
#define LOG_RETURN(c,r) do {LOG_T(c,"Leaving %s (rc = %08lx)\n", __FUNCTION__ , (unsigned long)(r) );return(r);}while(0)  /*!< \brief Macro to log a function exit, including integer value, then to return a value to the calling function */

/** @}*/

#ifdef __cplusplus
}
#endif

#endif

// ============================================= Logging for Aria ==========================================



// macrso for different log lvls

#include <stdlib.h>

#include <string.h>

// #include "nfapi_vnf.c"









// p5_p7_cfg *xfapi_cfg_for_nfapi;

// macros for vertical logs


#define LOG_CRTERR 0

#define LOG_ERROR 1

#define LOG_NONE 2

#define LOG_WARN 3

#define LOG_INFO 4

#define LOG_DEBUG 5



// macros for horizontal logs

#define _SCTP_ 0

#define _F1AP_ 1

#define _RRC_ 2

#define _NGAP_ 3

#define _RLC_ 4

#define _GTPU_ 5

#define _PDCP_ 6

#define _MAC_ 7

#define _PHY_ 8

#define _ARIA_ 9

#define _E1AP_ 10

#define _X2AP_ 11

#define _GNB_APP_ 12

#define _EXTRA_ 13

#define _P5_INFO_LEVEL_ 14

#define _P7_INFO_LEVEL_ 15

#define _SDAP_ 16

#define _DTLS_ 17

// #define P5_LOG 4

// #define P7_LOG 5









#define coranLabs_MAX_TEXTS 100000

#define coranLabs_MAX_TEXT_LENGTH 1000



extern char coranLabs_pdu_list[coranLabs_MAX_TEXTS][coranLabs_MAX_TEXT_LENGTH];

extern int coranLabs_num_texts;

extern void save_logs_to_file();







// macros for ENABLE or DISABLE

#define ENABLE 1

#define DISABLE 0



//macros for colours :

#define YELLOW "\033[33m"
#define LIGHT_RED "\033[1;31m"
#define GREEN "\033[0;32m"
#define RESET "\033[0m"
#define RED "\033[31m"

#define VERTICAL_COLOR "\033[38;2;96;192;198m"

#define HORIZONTAL_COLOR "\033[38;2;166;96;198m"

#define RESET_COLOR "\033[0m"







void Display(int vert_lvl, int horzt_lvl, char *text_to_print,int date_time_flag);

void SM_Logs(int vert_lvl, int horzt_lvl,const char *text_to_print,...);
void SM_Logs_Buffer(int vert_lvl,int horzt_lvl,const char *log_prefix, const uint8_t *buffer, uint32_t length);
void SM_Log_Assert(int horzt_level, int condition, const char *format, ...);

#ifndef LOG_H

#define LOG_H







typedef struct _horizontal_levels {

    bool SCTP;

    bool F1AP;

    bool RRC;

    bool RLC;

    bool PDCP;

    bool DTLS;

    bool NGAP;

    bool GTPU;

    bool CU_APP;

    bool MAC;

    bool PHY;

    bool ARIA;

    bool E1AP;

    bool X2AP;

    bool GNB_APP;

    bool EXTRA;

    bool P5_INFO_LEVEL;

    bool P7_INFO_LEVEL;

    bool SDAP;

} horizontal_levels_t;



typedef struct _logs_configurations {

    uint8_t vertical_level;

    horizontal_levels_t horizontal_level;

    bool print_datetime;

    int print_configurations;

} logs_configurations_t;

typedef struct _logs_file_configurations {

    uint8_t vertical_level;

    horizontal_levels_t horizontal_level;

    bool print_datetime;

    int print_configurations;

    int generate_log_file;

} logs_file_configurations_t;


typedef struct _nr5g_fapi_cfg {

  logs_configurations_t logs_configurations;
  logs_file_configurations_t logs_file_configurations;
  int l1_stats_update_interval;
  char vfs_addr[2][10];

} nr5g_fapi_cfg_t;

// Define the structure for a logical channel (LCID)
typedef struct _lcid_info{
    uint8_t lcid;              // Logical Channel ID
    uint32_t tx_bytes;         // Bytes transmitted
    uint32_t rx_bytes;         // Bytes received
} lcid_info_t;

// Define the main structure for UE statistics
typedef struct _aria_ue_statistics{
    uint16_t ue_rnti;          // UE RNTI (Radio Network Temporary Identifier)
    uint16_t cu_ue_id;         // CU UE ID
    char sync_status[30];      // In-sync/Out-of-sync status
    float ph;                  // Pathloss in dB
    int pcmax;                 // Power control maximum (dBm)
    int avg_rsrp;              // Average RSRP (dBm)
    int num_meas;              // Number of measurements

    // CQI and related metrics
    uint8_t cqi;               // Channel Quality Indicator
    uint8_t ri;                // Rank Indicator
    uint8_t pmi[2];            // Precoding Matrix Indicator

    // Downlink statistics
    uint32_t dlsch_rounds[4];  // DLSCH rounds [0,1,2,3]
    uint32_t dlsch_errors;     // DLSCH errors
    uint32_t pucch0_dtx;       // PUCCH DTX occurrences
    float bler_dlsch;          // Block Error Rate (BLER) for DLSCH
    uint8_t mcs_dl;            // Downlink MCS
    float snr_dl;              // Downlink Signal-to-Noise Ratio (dB)
    uint64_t dl_total_bytes;
    uint32_t dl_current_bytes;
    uint32_t dl_total_rbs;
    uint32_t dl_total_rbs_retx;
    uint32_t dl_current_rbs;

    // Uplink statistics
    uint32_t ulsch_rounds[4];  // ULSCH rounds [0,1,2,3]
    uint32_t ulsch_errors;     // ULSCH errors
    uint32_t ulsch_dtx;        // ULSCH DTX occurrences
    float bler_ulsch;          // Block Error Rate (BLER) for ULSCH
    uint8_t mcs_ul;            // Uplink MCS
    uint8_t qm_ul;             // Modulation Order (Qm)
    float delta_mcs;           // Delta MCS (dB)
    uint8_t nprb_ul;           // Number of PRBs used in uplink
    uint64_t ul_total_bytes;
    uint32_t ul_current_bytes;
    uint32_t ul_total_rbs;
    uint32_t ul_total_rbs_retx;
    uint32_t ul_current_rbs;

    // MAC layer statistics
    uint32_t mac_tx_bytes;     // Total MAC bytes transmitted
    uint32_t mac_rx_bytes;     // Total MAC bytes received

    // Logical Channel Info
    uint8_t num_lcid;          // Number of LCIDs
    lcid_info_t lcids[16];     // Array of LCID information (max 16 LCIDs)
} aria_ue_statistics_t;


typedef struct _aria_counters_statistics {
    int stats_available;
    int xran_ports;
    int nb_rx;
    uint64_t Rx_on_time;           // Total Rx On-Time
    uint64_t Rx_early;             // Early Rx packets
    uint64_t Rx_late;              // Late Rx packets
    uint64_t Rx_corrupt;           // Corrupt packets
    uint64_t Rx_pkt_dupl;          // Duplicate packets
    uint64_t tx_bytes_counter;     // Total transmitted bytes
    uint64_t rx_bytes_counter;     // Total received bytes
    uint64_t tx_bytes_per_sec;     // Transmit bytes per second
    uint64_t rx_bytes_per_sec;     // Receive bytes per second
    uint64_t tx_counter;           // Transmitted packets per second
    uint64_t rx_counter;           // Received packets per second
    uint64_t Total_msgs_rcvd;      // Total received packets
    uint64_t per_value;            // Packet Error Rate (PER)
    uint64_t rx_pusch_packets[16]; // Number of PUSCH packets per antenna (max 16 antennas)
    uint64_t rx_prach_packets[16]; // Number of PRACH packets per antenna (max 16 antennas)
    uint64_t rx_srs_packets;       // Number of SRS packets
} aria_counters_statistics_t;

typedef struct _aria_ue_stats_outer{
  int num_of_ue;
  aria_ue_statistics_t stats[5];
  aria_counters_statistics_t counters_stats;
} aria_ue_stats_outer_t;


#endif





extern nr5g_fapi_cfg_t AriaGlobalConfigs;



// Declare the structure globally

#define ARIA_VERSION "1.0"
#define ARIA_NAME "  Q-RAN ARIA"
#define ARIA_L3_NAME "CoranLabs-CU"
#define ARIA_L2_NAME "CoranLabs-DU"
extern aria_ue_stats_outer_t aria_ue_stats;
extern  time_t app_start_time;
// UE RNTI 48cb CU-UE-ID 1 in-sync PH 48 dB PCMAX 21 dBm, average RSRP -69 (30 meas)
// UE 48cb: CQI 15, RI 2, PMI (0,1)
// UE 48cb: dlsch_rounds 1654/109/34/11, dlsch_errors 7, pucch0_DTX 161, BLER 0.00026 MCS (1) 9
// UE 48cb: ulsch_rounds 576/12/2/0, ulsch_errors 0, ulsch_DTX 1, BLER 0.00000 MCS (1) 9 (Qm 4 deltaMCS 0 dB) NPRB 5  SNR 16.0 dB
// UE 48cb: MAC:    TX        7120741 RX         270638 bytes
// UE 48cb: LCID 1: TX           1055 RX           1823 bytes
// UE 48cb: LCID 2: TX              0 RX              0 bytes
// UE 48cb: LCID 4: TX        6923555 RX         142830 bytes