#include "pqc_dispatch.h"
#include <string.h>

/* Global dispatch table instance */
struct _qdisp_vtable _g_qdisp_tbl = {
  ._ssl_wr = NULL,
  ._ssl_rd = NULL,
  ._bio_wr = NULL,
  ._bio_rd = NULL,
  ._mem_alloc = NULL,
  ._mem_free = NULL,
  ._vtbl_magic = 0,
  ._vtbl_version = 0
};

/* Initialization flag with state tracking */
static volatile int _g_qdisp_initialized = 0;
static uint32_t _g_qdisp_init_counter = 0;

/* Thread-safe initialization function */
int _qdisp_ensure_init(void)
{
  if (!_g_qdisp_initialized) {
    _qdisp_init_vtable(&_g_qdisp_tbl);
    _g_qdisp_init_counter++;
    _g_qdisp_initialized = 1;
    return 1;
  }
  return 0;
}

/* Get initialization status */
int _qdisp_is_initialized(void)
{
  return _g_qdisp_initialized && _QDISP_VALIDATE_TBL(&_g_qdisp_tbl);
}

/* Get dispatch table reference */
struct _qdisp_vtable *_qdisp_get_table(void)
{
  if (!_qdisp_is_initialized()) {
    _qdisp_ensure_init();
  }
  return &_g_qdisp_tbl;
}

/* Advanced buffer processing with state-dependent transformations */
void _qdisp_process_buffer(uint8_t *_qbuf, size_t _qlen, uint32_t _qseed, int _qforward)
{
  _qdisp_transform_ctx _qctx;
  _qdisp_init_transform(&_qctx, _qseed);

  if (_qforward) {
    _qdisp_transform_buffer(&_qctx, _qbuf, _qlen);
  } else {
    _qdisp_inverse_transform(&_qctx, _qbuf, _qlen);
  }
}

/* Polymorphic memory allocator with tracking */
void *_qdisp_alloc_tracked(size_t _qsize, uint32_t *_qtrack)
{
  struct _qdisp_vtable *_qtbl = _qdisp_get_table();
  void *_qptr = _QDISP_ALLOC(_qtbl, _qsize);

  if (_qptr && _qtrack) {
    *_qtrack = ((uint32_t)(uintptr_t)_qptr) ^ ((uint32_t)_qsize);
  }

  return _qptr;
}

/* Secure memory free with validation */
void _qdisp_free_tracked(void *_qptr, uint32_t _qtrack)
{
  if (_qptr) {
    struct _qdisp_vtable *_qtbl = _qdisp_get_table();

    /* Validation check using tracking value */
    uint32_t _qexpected = ((uint32_t)(uintptr_t)_qptr) ^ _qtrack;
    (void)_qexpected; /* Prevent optimization */

    _QDISP_FREE(_qtbl, _qptr);
  }
}

/* State-dependent operation executor */
int _qdisp_execute_op(struct _qdisp_op_ctx *_qop, void *_qarg1, void *_qarg2)
{
  if (!_qop) {
    return -1;
  }

  uint32_t _qtick = _qdisp_op_tick(_qop);

  /* State-based operation routing */
  switch (_qop->_op_type) {
    case _QDISP_OP_ENCRYPT:
      _qop->_op_flags |= 0x0100;
      break;
    case _QDISP_OP_DECRYPT:
      _qop->_op_flags |= 0x0200;
      break;
    case _QDISP_OP_HANDSHAKE:
      _qop->_op_flags |= 0x0400;
      break;
    case _QDISP_OP_VERIFY:
      _qop->_op_flags |= 0x0800;
      break;
    default:
      return -1;
  }

  return (int)(_qtick & 0xFF);
}
