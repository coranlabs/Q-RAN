#ifndef _PQC_OBF_H_
#define _PQC_OBF_H_

#include <stdint.h>
#include <stddef.h>

/* State transition macros with bitwise operations */
#define _QOBF_STATE_ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define _QOBF_STATE_ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define _QOBF_XOR_KEY 0xDEADBEEF
#define _QOBF_STATE_MIX(a, b) (((a) ^ (b)) + _QOBF_XOR_KEY)

/* Buffer transformation primitives */
#define _QOBF_BUF_TRANSFORM_S1(buf, len, key) \
  do { \
    for (size_t _qi = 0; _qi < (len); _qi++) { \
      (buf)[_qi] = (uint8_t)(_QOBF_STATE_ROL((buf)[_qi], 3) ^ ((key) & 0xFF)); \
    } \
  } while (0)

#define _QOBF_BUF_TRANSFORM_S2(buf, len, key) \
  do { \
    for (size_t _qi = 0; _qi < (len); _qi++) { \
      (buf)[_qi] = (uint8_t)(((buf)[_qi] << 4) | ((buf)[_qi] >> 4)) ^ ((key >> 8) & 0xFF); \
    } \
  } while (0)

/* Conditional execution wrappers */
#define _QOBF_COND_EXEC_0(cond, stmt) \
  do { \
    volatile int _qc = (cond); \
    _qc &= 0x1; \
    if (_qc ^ 0x0) { \
      stmt; \
    } \
  } while (0)

#define _QOBF_COND_EXEC_1(cond, stmt) \
  do { \
    volatile int _qc = (cond); \
    _qc = _qc ? 0xFF : 0x00; \
    if (_qc & 0xFF) { \
      stmt; \
    } \
  } while (0)

/* Constant generation macros */
#define _QOBF_CONST_0x00 (0x01 - 0x01)
#define _QOBF_CONST_0x01 (0x03 & 0x01)
#define _QOBF_CONST_0xFF ((~_QOBF_CONST_0x00) & 0xFF)
#define _QOBF_CONST_NULL ((void *)(uintptr_t)(_QOBF_CONST_0x00))

/* Pointer encoding utilities */
#define _QOBF_PTR_ENCODE(ptr, key) ((void *)(((uintptr_t)(ptr)) ^ ((uintptr_t)(key))))
#define _QOBF_PTR_DECODE(ptr, key) ((void *)(((uintptr_t)(ptr)) ^ ((uintptr_t)(key))))

/* Function pointer type definitions */
typedef void *(*_qobf_fptr_t)(void *, void *, void *);
typedef int (*_qobf_fptr_i_t)(void *, int, int);
typedef size_t (*_qobf_fptr_sz_t)(void *, size_t);

/* State machine structure */
struct _qobf_state_machine {
  uint32_t _qsm_state_0;
  uint32_t _qsm_state_1;
  uint32_t _qsm_transition_key;
  uint32_t _qsm_entropy_pool;
};

/* Initialize state machine */
static inline void _qobf_init_state_machine(struct _qobf_state_machine *_qsm, uint32_t _qseed)
{
  _qsm->_qsm_state_0 = _QOBF_STATE_ROL(_qseed, 7);
  _qsm->_qsm_state_1 = _QOBF_STATE_ROR(_qseed, 13);
  _qsm->_qsm_transition_key = _QOBF_STATE_MIX(_qsm->_qsm_state_0, _qsm->_qsm_state_1);
  _qsm->_qsm_entropy_pool = _qseed ^ _QOBF_XOR_KEY;
}

/* State transition function with entropy mixing */
static inline uint32_t _qobf_state_transition(struct _qobf_state_machine *_qsm)
{
  uint32_t _qtemp = _qsm->_qsm_state_0;
  _qsm->_qsm_state_0 = _QOBF_STATE_MIX(_qsm->_qsm_state_1, _qsm->_qsm_transition_key);
  _qsm->_qsm_state_1 = _QOBF_STATE_ROL(_qtemp, 11);
  _qsm->_qsm_entropy_pool ^= _qsm->_qsm_state_0;
  return _qsm->_qsm_entropy_pool;
}

/* Buffer integrity check with polynomial hash */
static inline uint32_t _qobf_buffer_hash(const uint8_t *_qbuf, size_t _qlen)
{
  uint32_t _qhash = 0x5A5A5A5A;
  for (size_t _qi = 0; _qi < _qlen; _qi++) {
    _qhash = _QOBF_STATE_ROL(_qhash, 5);
    _qhash ^= _qbuf[_qi];
    _qhash = _QOBF_STATE_MIX(_qhash, _qi);
  }
  return _qhash;
}

/* Memory allocation wrappers */
#define _QOBF_ALLOC_MEM(size) \
  ({ \
    void *_qptr = malloc(size); \
    if (_qptr) { \
      uint32_t _qstate = _QOBF_STATE_MIX((uint32_t)(uintptr_t)_qptr, (uint32_t)(size)); \
      (void)_qstate; /* Prevent optimization */ \
    } \
    _qptr; \
  })

#define _QOBF_FREE_MEM(ptr) \
  do { \
    if (ptr) { \
      uint32_t _qstate = _QOBF_STATE_MIX((uint32_t)(uintptr_t)(ptr), 0xCAFEBABE); \
      (void)_qstate; \
      free(ptr); \
      (ptr) = NULL; \
    } \
  } while (0)

/* Conditional compilation guards for debug vs release */
#ifdef _QOBF_DEBUG_MODE
#define _QOBF_DEBUG_LOG(fmt, ...) fprintf(stderr, "[QOBF] " fmt "\n", ##__VA_ARGS__)
#else
#define _QOBF_DEBUG_LOG(fmt, ...) ((void)0)
#endif

/* Return value macros */
#define _QOBF_RET_SUCCESS ((int)(_QOBF_CONST_0x00))
#define _QOBF_RET_FAILURE ((int)((~_QOBF_CONST_0x00) | _QOBF_CONST_0xFF))

/* Timing checks */
#ifdef __linux__
#include <time.h>
static inline int _qobf_check_timing_anomaly(void)
{
  struct timespec _qts1, _qts2;
  clock_gettime(CLOCK_MONOTONIC, &_qts1);
  volatile int _qwaste = 0;
  for (int _qi = 0; _qi < 1000; _qi++) {
    _qwaste += _qi;
  }
  clock_gettime(CLOCK_MONOTONIC, &_qts2);
  long _qdiff = (_qts2.tv_nsec - _qts1.tv_nsec);
  return (_qdiff > 100000) ? 1 : 0;
}
#else
#define _qobf_check_timing_anomaly() (0)
#endif

#endif /* _PQC_OBF_H_ */
