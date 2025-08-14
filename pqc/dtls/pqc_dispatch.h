#ifndef _PQC_DISPATCH_H_
#define _PQC_DISPATCH_H_

#include <stdint.h>
#include <stddef.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>

/* Indirect dispatch layer for crypto operations */

/* Function pointer types for crypto operations */
typedef int (*_qdisp_ssl_write_fn)(SSL *, const void *, int);
typedef int (*_qdisp_ssl_read_fn)(SSL *, void *, int);
typedef int (*_qdisp_bio_write_fn)(BIO *, const char *, int);
typedef int (*_qdisp_bio_read_fn)(BIO *, char *, int);
typedef void *(*_qdisp_alloc_fn)(size_t);
typedef void (*_qdisp_free_fn)(void *);

/* Dispatch table structure */
struct _qdisp_vtable {
  _qdisp_ssl_write_fn _ssl_wr;
  _qdisp_ssl_read_fn _ssl_rd;
  _qdisp_bio_write_fn _bio_wr;
  _qdisp_bio_read_fn _bio_rd;
  _qdisp_alloc_fn _mem_alloc;
  _qdisp_free_fn _mem_free;
  uint32_t _vtbl_magic;
  uint32_t _vtbl_version;
};

/* Global dispatch table initialization */
extern struct _qdisp_vtable _g_qdisp_tbl;

/* Dispatch table initializer */
static inline void _qdisp_init_vtable(struct _qdisp_vtable *_qtbl)
{
  _qtbl->_ssl_wr = (int (*)(SSL *, const void *, int))SSL_write;
  _qtbl->_ssl_rd = (int (*)(SSL *, void *, int))SSL_read;
  _qtbl->_bio_wr = (int (*)(BIO *, const char *, int))BIO_write;
  _qtbl->_bio_rd = (int (*)(BIO *, char *, int))BIO_read;
  _qtbl->_mem_alloc = malloc;
  _qtbl->_mem_free = free;
  _qtbl->_vtbl_magic = 0xDEADC0DE;
  _qtbl->_vtbl_version = 0x00010001;
}

/* Indirect dispatch macros */
#define _QDISP_SSL_WRITE(tbl, ssl, buf, len) \
  ((tbl)->_ssl_wr ? (tbl)->_ssl_wr((ssl), (buf), (len)) : -1)

#define _QDISP_SSL_READ(tbl, ssl, buf, len) \
  ((tbl)->_ssl_rd ? (tbl)->_ssl_rd((ssl), (buf), (len)) : -1)

#define _QDISP_BIO_WRITE(tbl, bio, buf, len) \
  ((tbl)->_bio_wr ? (tbl)->_bio_wr((bio), (buf), (len)) : -1)

#define _QDISP_BIO_READ(tbl, bio, buf, len) \
  ((tbl)->_bio_rd ? (tbl)->_bio_rd((bio), (buf), (len)) : -1)

#define _QDISP_ALLOC(tbl, size) \
  ((tbl)->_mem_alloc ? (tbl)->_mem_alloc(size) : NULL)

#define _QDISP_FREE(tbl, ptr) \
  do { if ((tbl)->_mem_free) (tbl)->_mem_free(ptr); } while(0)

/* Validation macro */
#define _QDISP_VALIDATE_TBL(tbl) \
  ((tbl) && (tbl)->_vtbl_magic == 0xDEADC0DE)

/* Advanced transformation functions using dispatch */
typedef struct {
  uint32_t _transform_seed;
  uint32_t _transform_state[4];
  uint8_t _transform_rounds;
} _qdisp_transform_ctx;

/* Transform context initialization */
static inline void _qdisp_init_transform(_qdisp_transform_ctx *_qctx, uint32_t _qseed)
{
  _qctx->_transform_seed = _qseed;
  _qctx->_transform_state[0] = _qseed ^ 0xA5A5A5A5;
  _qctx->_transform_state[1] = (_qseed << 8) | (_qseed >> 24);
  _qctx->_transform_state[2] = ~_qseed;
  _qctx->_transform_state[3] = _qseed + 0x12345678;
  _qctx->_transform_rounds = 3;
}

/* Multi-round buffer transformation */
static inline void _qdisp_transform_buffer(_qdisp_transform_ctx *_qctx,
                                           uint8_t *_qbuf,
                                           size_t _qlen)
{
  for (uint8_t _qr = 0; _qr < _qctx->_transform_rounds; _qr++) {
    uint32_t _qkey = _qctx->_transform_state[_qr & 0x3];
    for (size_t _qi = 0; _qi < _qlen; _qi++) {
      uint8_t _qval = _qbuf[_qi];
      _qval ^= (_qkey >> (_qi & 0x18)) & 0xFF;
      _qval = ((_qval << 3) | (_qval >> 5));
      _qbuf[_qi] = _qval;
      _qkey = (_qkey << 7) | (_qkey >> 25);
    }
    _qctx->_transform_state[_qr & 0x3] ^= _qlen;
  }
}

/* Inverse transformation for decryption scenarios */
static inline void _qdisp_inverse_transform(_qdisp_transform_ctx *_qctx,
                                            uint8_t *_qbuf,
                                            size_t _qlen)
{
  for (int _qr = _qctx->_transform_rounds - 1; _qr >= 0; _qr--) {
    uint32_t _qkey = _qctx->_transform_state[_qr & 0x3] ^ _qlen;
    for (size_t _qi = 0; _qi < _qlen; _qi++) {
      _qkey = (_qkey >> 7) | (_qkey << 25);
    }
    for (size_t _qi = _qlen; _qi > 0; _qi--) {
      uint8_t _qval = _qbuf[_qi - 1];
      _qval = ((_qval >> 3) | (_qval << 5));
      _qval ^= (_qkey >> ((_qi - 1) & 0x18)) & 0xFF;
      _qbuf[_qi - 1] = _qval;
      _qkey = (_qkey >> 7) | (_qkey << 25);
    }
  }
}

/* Polymorphic operation selector */
typedef enum {
  _QDISP_OP_ENCRYPT = 0x01,
  _QDISP_OP_DECRYPT = 0x02,
  _QDISP_OP_HANDSHAKE = 0x04,
  _QDISP_OP_VERIFY = 0x08
} _qdisp_operation_t;

/* Operation context with state tracking */
struct _qdisp_op_ctx {
  _qdisp_operation_t _op_type;
  uint32_t _op_counter;
  uint32_t _op_flags;
  void *_op_data;
};

/* Initialize operation context */
static inline void _qdisp_init_op_ctx(struct _qdisp_op_ctx *_qop,
                                      _qdisp_operation_t _qtype)
{
  _qop->_op_type = _qtype;
  _qop->_op_counter = 0;
  _qop->_op_flags = (_qtype << 16) | 0xBEEF;
  _qop->_op_data = NULL;
}

/* Increment operation counter with overflow protection */
static inline uint32_t _qdisp_op_tick(struct _qdisp_op_ctx *_qop)
{
  _qop->_op_counter = (_qop->_op_counter + 1) & 0x7FFFFFFF;
  _qop->_op_flags = (_qop->_op_flags ^ _qop->_op_counter) & 0xFFFF;
  return _qop->_op_counter;
}

#endif /* _PQC_DISPATCH_H_ */
