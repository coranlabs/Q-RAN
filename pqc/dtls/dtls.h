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

/* SSL client struct: */
typedef struct ssl_client {
  SSL *ssl;

  BIO *rbio; /* SSL reads from (and gives unencrypted bytes), we write to. */
  BIO *wbio; /* SSL writes to (and gives encrypted bytes), we read from. */

  /* Bytes waiting to be written to socket. This is data that has been generated
   * by the SSL object, either due to encryption of user input, or, writes
   * requires due to peer-requested SSL renegotiation. */
  uint8_t *write_buf;
  size_t write_len;

  /* Bytes waiting to be fed into the SSL object for encryption. */
  uint8_t *encrypt_buf;
  size_t encrypt_len;

  /* plain text buffer size */
  size_t plain_text_size;

  /* socket descriptor of the client */
  int sd;
  int handshake_done;

} Client;

enum sslstatus { SSLSTATUS_OK, SSLSTATUS_WANT_IO, SSLSTATUS_FAIL };
enum sslmode { SSLMODE_SERVER, SSLMODE_CLIENT };

#define DEFAULT_BUF_SIZE 64

/* OSSL LIBCTX & Provider */
OSSL_LIB_CTX *load_ossl_libctx();
OSSL_PROVIDER *load_oqs_provider(OSSL_LIB_CTX *libctx, const char *modulename, const char *configfile);

/* SSL CTX gen */
SSL_CTX *dtls_server_ctx_init(OSSL_PROVIDER *provider, OSSL_LIB_CTX *libctx, const char *cert_file, const char *key_file);
SSL_CTX *dtls_client_ctx_init(OSSL_PROVIDER *provider, OSSL_LIB_CTX *libctx, const char *cert_file, const char *key_file);

/* SSL client init */
int ssl_client_init(SSL_CTX *ssl_ctx, struct ssl_client *client, int fd, size_t plain_text_size, enum sslmode mode);

/* Message enc/dec */

int new_message_encrypt(Client *c, uint8_t *buf, size_t buf_len);
void new_message_decrypt(Client *c, uint8_t *src, size_t src_len, uint8_t *buf);

/* SSL utilities */

void print_private_key(const char *privkeyfile);

int handle_ssl_error(SSL *ssl, int retval);
void print_ssl_state(SSL *ssl);
enum sslstatus get_sslstatus(SSL *ssl, int n);

void cleanup_ssl_ctx(SSL_CTX *ssl_ctx);
void cleanup_ssl_client(Client *client);

/* SSL handshake */
enum sslstatus do_ssl_handshake(Client *client);

void send_unencrypted_bytes(Client *client, uint8_t *buf, size_t buf_len);
void queue_encrypted_bytes(Client *client, uint8_t *buf, size_t buf_len);

int read_enc_bytes(Client *client, uint8_t *src, size_t src_len, uint8_t *buf);
int encrypt_buf(Client *client);

int sock_read(Client *c, char *buf, size_t buf_len);

#endif
