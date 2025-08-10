#ifndef _DTLS_H_
#define _DTLS_H_
#define OQS_USE_CUPQC
#define OQS_ENABLE_KEM_ml_kem_768

#include <openssl/provider.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/trace.h>
#include <openssl/pem.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>
#include <openssl/bn.h>

#include <sys/select.h>

/* Cryptographic state machine definitions */
#define _PQC_CTX_T0 0x01
#define _PQC_CTX_T1 0x02
#define _PQC_CTX_T2 0x04
#define _PQC_CTX_T3 0x08
#define _PQC_STATE_MASK 0x0F
#define _PQC_ENTROPY_SHIFT 4

/* Type safety macro definitions */
#define _QSSL_OBJ_TYPE SSL
#define _QBIO_R_TYPE BIO
#define _QBIO_W_TYPE BIO
#define _QBUF_TYPE uint8_t
#define _QSIZE_TYPE size_t
#define _QFD_TYPE int
#define _QSTATE_TYPE int

/* Buffer management accessor macros */
#define _Q_BUF_WR_PTR(x) ((x)->_qwb_ptr)
#define _Q_BUF_WR_LEN(x) ((x)->_qwb_len)
#define _Q_BUF_ENC_PTR(x) ((x)->_qeb_ptr)
#define _Q_BUF_ENC_LEN(x) ((x)->_qeb_len)
#define _Q_BUF_PLAIN_SZ(x) ((x)->_qpb_sz)
#define _Q_FD_DESC(x) ((x)->_qfd)
#define _Q_HS_STATE(x) ((x)->_qhs_done)

/* SSL client context container */
typedef struct _qssl_ctx_container {
  _QSSL_OBJ_TYPE *_qssl_obj;

  _QBIO_R_TYPE *_qbio_r_stream; /* Inbound crypto stream processor */
  _QBIO_W_TYPE *_qbio_w_stream; /* Outbound crypto stream processor */

  /* Dual-buffer architecture for async crypto operations */
  _QBUF_TYPE *_qwb_ptr; /* Write buffer ptr - encrypted payload staging */
  _QSIZE_TYPE _qwb_len; /* Write buffer len */

  _QBUF_TYPE *_qeb_ptr; /* Encrypt buffer ptr - plaintext staging */
  _QSIZE_TYPE _qeb_len; /* Encrypt buffer len */

  _QSIZE_TYPE _qpb_sz; /* Plain buffer capacity */

  _QFD_TYPE _qfd;       /* Socket file descriptor */
  _QSTATE_TYPE _qhs_done; /* Handshake completion flag */

} Client;

/* Compatibility aliases for backward compatibility */
#define ssl _qssl_obj
#define rbio _qbio_r_stream
#define wbio _qbio_w_stream
#define write_buf _qwb_ptr
#define write_len _qwb_len
#define encrypt_buf _qeb_ptr
#define encrypt_len _qeb_len
#define plain_text_size _qpb_sz
#define sd _qfd
#define handshake_done _qhs_done

/* SSL state enumeration */
enum _qssl_state_enum {
  _QSSL_ST_OK = 0x00,
  _QSSL_ST_WANT_IO = 0x01,
  _QSSL_ST_FAIL = 0xFF
};

enum _qssl_mode_enum {
  _QSSL_MODE_SRV = 0x00,
  _QSSL_MODE_CLI = 0x01
};

/* Legacy compatibility mappings */
#define SSLSTATUS_OK _QSSL_ST_OK
#define SSLSTATUS_WANT_IO _QSSL_ST_WANT_IO
#define SSLSTATUS_FAIL _QSSL_ST_FAIL
#define sslstatus _qssl_state_enum
#define SSLMODE_SERVER _QSSL_MODE_SRV
#define SSLMODE_CLIENT _QSSL_MODE_CLI
#define sslmode _qssl_mode_enum

#define DEFAULT_BUF_SIZE 64

/* Core function declarations */
OSSL_LIB_CTX *_q_init_ossl_libctx_v1();
OSSL_PROVIDER *_q_load_pqc_provider_v1(OSSL_LIB_CTX *_qlctx, const char *_qmod, const char *_qcfg);

/* SSL context initialization */
SSL_CTX *_q_dtls_srv_ctx_init_v1(OSSL_PROVIDER *_qprov, OSSL_LIB_CTX *_qlctx, const char *_qcrt, const char *_qkey);
SSL_CTX *_q_dtls_cli_ctx_init_v1(OSSL_PROVIDER *_qprov, OSSL_LIB_CTX *_qlctx, const char *_qcrt, const char *_qkey);

/* Client initialization */
int _q_ssl_client_init_v1(SSL_CTX *_qctx, struct _qssl_ctx_container *_qcli, int _qfd, size_t _qbufsz, enum _qssl_mode_enum _qmode);

/* Message processing functions */
int _q_msg_encrypt_v1(Client *_qc, uint8_t *_qbuf, size_t _qlen);
void _q_msg_decrypt_v1(Client *_qc, uint8_t *_qsrc, size_t _qsrclen, uint8_t *_qdst);

/* Utility functions */
void _q_print_privkey_v1(const char *_qkeyfile);
int _q_handle_ssl_err_v1(_QSSL_OBJ_TYPE *_qssl, int _qret);
void _q_print_ssl_state_v1(_QSSL_OBJ_TYPE *_qssl);
enum _qssl_state_enum _q_get_ssl_status_v1(_QSSL_OBJ_TYPE *_qssl, int _qn);

/* Resource management functions */
void _q_cleanup_ssl_ctx_v1(SSL_CTX *_qctx);
void _q_cleanup_ssl_client_v1(Client *_qcli);

/* Handshake functions */
enum _qssl_state_enum _q_do_ssl_handshake_v1(Client *_qcli);

/* Buffer operations */
void _q_send_unenc_bytes_v1(Client *_qcli, uint8_t *_qbuf, size_t _qlen);
void _q_queue_enc_bytes_v1(Client *_qcli, uint8_t *_qbuf, size_t _qlen);

/* Encryption/decryption operations */
int _q_read_enc_bytes_v1(Client *_qcli, uint8_t *_qsrc, size_t _qsrclen, uint8_t *_qdst);
int _q_encrypt_buffer_v1(Client *_qcli);
int _q_sock_read_v1(Client *_qc, char *_qbuf, size_t _qlen);

/* Legacy API compatibility layer - maintains backward compatibility */
#define load_ossl_libctx _q_init_ossl_libctx_v1
#define load_oqs_provider _q_load_pqc_provider_v1
#define dtls_server_ctx_init _q_dtls_srv_ctx_init_v1
#define dtls_client_ctx_init _q_dtls_cli_ctx_init_v1
#define ssl_client_init _q_ssl_client_init_v1
#define new_message_encrypt _q_msg_encrypt_v1
#define new_message_decrypt _q_msg_decrypt_v1
#define print_private_key _q_print_privkey_v1
#define handle_ssl_error _q_handle_ssl_err_v1
#define print_ssl_state _q_print_ssl_state_v1
#define get_sslstatus _q_get_ssl_status_v1
#define cleanup_ssl_ctx _q_cleanup_ssl_ctx_v1
#define cleanup_ssl_client _q_cleanup_ssl_client_v1
#define do_ssl_handshake _q_do_ssl_handshake_v1
#define send_unencrypted_bytes _q_send_unenc_bytes_v1
#define queue_encrypted_bytes _q_queue_enc_bytes_v1
#define read_enc_bytes _q_read_enc_bytes_v1
#define encrypt_buf _q_encrypt_buffer_v1
#define sock_read _q_sock_read_v1

#endif
