#include <stdio.h>
#include "dtls.h"
#include "../../common/utils/LOG/log.h"

typedef enum { CONTEXT_TYPE_SERVER, CONTEXT_TYPE_CLIENT } context_type_t;

static FILE *keylog_file = NULL;

void keylog_callback(const SSL *ssl, const char *line)
{
  (void)ssl; // Avoid unused parameter warning
  if (keylog_file) {
    fprintf(keylog_file, "%s\n", line);
    fflush(keylog_file);
  }
}

// Function to set up key logging
void enable_key_logging(SSL_CTX *ctx, const char *filename)
{
  FILE *keylog_file = fopen(filename, "a"); // Change the path as needed
  if (!keylog_file) {
    perror("Failed to open keylog file");
    return;
  }

  SSL_CTX_set_keylog_callback(ctx, keylog_callback);
}

/* Loads the oqs-provider from a shared module (.so). */
OSSL_PROVIDER *load_oqs_provider(OSSL_LIB_CTX *libctx, const char *modulename, const char *configfile)
{
  OSSL_PROVIDER *provider;

  int ret = OSSL_PROVIDER_available(libctx, modulename);
  if (ret != 0) {
    fprintf(stderr, "OSSL_PROVIDER_available returned %i, but 0 was expected\n", ret);
    return NULL;
  }
  SM_Logs(LOG_INFO, _DTLS_, "OSSL_PROVIDER available\n");

  SM_Logs(LOG_INFO, _DTLS_, "Provider name: %s\n", modulename);

  provider = OSSL_PROVIDER_load(libctx, modulename);
  if (provider == NULL) {
    fprintf(stderr, "OSSL_PROVIDER_LOAD returned an error\n");
    ERR_print_errors_fp(stderr);
  }
  return provider; // same provider to be used across all ssl ctx's
}

// module provider & libctx should only be loaded once.

OSSL_LIB_CTX *load_ossl_libctx()
{
  OSSL_LIB_CTX *libctx = OSSL_LIB_CTX_new();
  if (libctx == NULL) {
    fprintf(stderr, "`OSSL_LIB_CTX_new` failed. Cannot initialize OpenSSL.\n");
    return NULL;
  }
  return libctx;
}

void print_private_key(const char *privkeyfile)
{
  FILE *fp;
  EVP_PKEY *pkey = NULL;

  // Open the private key file
  fp = fopen(privkeyfile, "r");
  if (fp == NULL) {
    perror("Error opening private key file");
    return;
  }

  // Load private key from file
  pkey = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
  if (pkey == NULL) {
    fprintf(stderr, "Error loading private key from file\n");
    ERR_print_errors_fp(stderr);
    fclose(fp);
    return;
  }

  fclose(fp);

  printf("Private Key:\n");
  PEM_write_PrivateKey(stdout, pkey, NULL, NULL, 0, NULL, NULL);

  // Free the EVP_PKEY structure
  EVP_PKEY_free(pkey);
}

void info_callback(const SSL *ssl, int where, int ret)
{
  if (where & SSL_CB_HANDSHAKE_START) {
    SM_Logs(LOG_INFO, _DTLS_, "SSL Handshake started\n");
  } else if (where & SSL_CB_HANDSHAKE_DONE) {
    SM_Logs(LOG_INFO, _DTLS_, "SSL Handshake finished\n");
  }
  if (where & SSL_CB_LOOP) {
    const char *str = SSL_state_string_long(ssl);
    SM_Logs(LOG_INFO, _DTLS_, "SSL state: %s", str);
  }

  if (where & SSL_CB_ALERT) {
    LOG_D(DTLS,
          "Alert: %s: %s\n",
          SSL_alert_type_string_long(SSL_get_error(ssl, ret)),
          SSL_alert_desc_string_long(SSL_get_error(ssl, ret)));
  }

  if (where & SSL_CB_EXIT) {
    // if (ret == 0) {
    //   fprintf(stderr, "SSL operation failed\n");
    //   handle_ssl_error(ssl, ret);
    // } else if (ret < 0) {
    //   fprintf(stderr, "SSL operation returned an error\n");
    //   handle_ssl_error(ssl, ret);
    //   ;
    // }
  }
}

void enable_openssl_trace()
{
  BIO *bio_trace = BIO_new_fp(stderr, BIO_NOCLOSE);
  if (!bio_trace) {
    fprintf(stderr, "Failed to create BIO for trace output\n");
    return;
  }

  OSSL_trace_set_channel(OSSL_TRACE_CATEGORY_TLS, bio_trace);
  OSSL_trace_set_channel(OSSL_TRACE_CATEGORY_CONF, bio_trace);
  OSSL_trace_set_channel(OSSL_TRACE_CATEGORY_X509V3_POLICY, bio_trace);

  fprintf(stderr, "OpenSSL tracing enabled\n");
}

// void print_repeated_char(char ch, int count) {
//     for (int i = 0; i < count; i++) {
//         fprintf(stdout,ch);
//     }
// }

void print_centered_text(const char *text, int width)
{
  int len = strlen(text);
  int padding = (width - len) / 2; // Calculate spaces for centering

  // Print spaces for padding
  for (int i = 0; i < padding; i++) {
    fputc(' ', stdout);
  }

  // Print the text
  fprintf(stdout, "%s\n", text);
}

void print_ca_certificate_details(const char *ca_cert_path)
{
  FILE *fp = fopen(ca_cert_path, "r");
  if (!fp) {
    perror("Error opening CA certificate file");
    return;
  }

  X509 *ca_cert = PEM_read_X509(fp, NULL, NULL, NULL);
  fclose(fp);

  if (!ca_cert) {
    fprintf(stderr, "Error reading CA certificate\n");
    return;
  }

  char *issuer = X509_NAME_oneline(X509_get_issuer_name(ca_cert), NULL, 0);
  char *subject = X509_NAME_oneline(X509_get_subject_name(ca_cert), NULL, 0);

  BIO *bio = BIO_new(BIO_s_mem());
  ASN1_TIME_print(bio, X509_get_notBefore(ca_cert));
  char not_before[64];
  BIO_read(bio, not_before, sizeof(not_before) - 1);
  not_before[sizeof(not_before) - 1] = '\0';

  ASN1_TIME_print(bio, X509_get_notAfter(ca_cert));
  char not_after[64];
  BIO_read(bio, not_after, sizeof(not_after) - 1);
  not_after[sizeof(not_after) - 1] = '\0';
  BIO_free(bio);

  ASN1_INTEGER *serial = X509_get_serialNumber(ca_cert);
  BIGNUM *bn_serial = ASN1_INTEGER_to_BN(serial, NULL);
  char *serial_str = BN_bn2hex(bn_serial);

  int sig_nid = X509_get_signature_nid(ca_cert);

  const char *sig_algo = (sig_nid != NID_undef) ? OBJ_nid2ln(sig_nid) : "Unknown";

  unsigned char fingerprint[EVP_MAX_MD_SIZE];
  unsigned int fingerprint_len;
  X509_digest(ca_cert, EVP_sha256(), fingerprint, &fingerprint_len);

  printf("\n-------------------CA CERTIFICATE-------------------------\n");
  printf("Issuer: %s\n", issuer);
  printf("Subject: %s\n", subject);
  printf("Validity:\n");
  printf("  Not Before: %s\n", not_before);
  printf("  Not After : %s\n", not_after);
  printf("Serial Number: %s\n", serial_str);
  printf("Signature Algorithm: %s\n", sig_algo);

  printf("SHA-256 Fingerprint: ");
  for (unsigned int i = 0; i < fingerprint_len; i++) {
    printf("%02X", fingerprint[i]);
    if (i < fingerprint_len - 1)
      printf(":");
  }
  printf("\n");

  // Print X509v3 Extensions used: extKeyUsage for example
  STACK_OF(X509_EXTENSION) *exts = X509_get0_extensions(ca_cert);
  int num_exts = sk_X509_EXTENSION_num(exts);
  printf("X509v3 Extensions:\n");
  for (int i = 0; i < num_exts; i++) {
    X509_EXTENSION *ext = sk_X509_EXTENSION_value(exts, i);
    ASN1_OBJECT *obj = X509_EXTENSION_get_object(ext);
    BIO *bio_ext = BIO_new(BIO_s_mem());
    if (!X509V3_EXT_print(bio_ext, ext, 0, 0)) {
      i2a_ASN1_OBJECT(bio_ext, obj);
    }
    char *ext_data;
    long ext_len = BIO_get_mem_data(bio_ext, &ext_data);
    printf("  %s: %.*s\n", OBJ_nid2ln(OBJ_obj2nid(obj)), (int)ext_len, ext_data);
    BIO_free(bio_ext);
  }

  BN_free(bn_serial);
  OPENSSL_free(serial_str);
  OPENSSL_free(issuer);
  OPENSSL_free(subject);
  X509_free(ca_cert);
}

int load_ca_certificate(SSL_CTX *ctx, char *ca_cert_file, char *ca_cert_dir)
{
  if (!SSL_CTX_load_verify_locations(ctx, ca_cert_file, ca_cert_dir)) {
    ERR_print_errors_fp(stderr);
    SSL_CTX_free(ctx);
    return -1;
  }
  return 0;
}

// should only be called once.
SSL_CTX *dtls_server_ctx_init(OSSL_PROVIDER *provider, OSSL_LIB_CTX *libctx, const char *cert_file, const char *key_file)
{
  SSL_library_init();
  SSL_load_error_strings();

  OpenSSL_add_all_algorithms();
  // enable_openssl_trace();

  SSL_CTX *ssl_ctx;
  uint64_t ssl_opts;

  ssl_ctx = SSL_CTX_new_ex(libctx, NULL, DTLS_server_method());
  if (!ssl_ctx) {
    SM_Logs(LOG_ERROR, _DTLS_, "Could not create SSL/TLS context: %s", ERR_error_string(ERR_get_error(), NULL));
    return NULL;
  }

  if (ssl_ctx == NULL) {
    goto err;
  }

  // ssl_opts = (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) | SSL_OP_NO_COMPRESSION | SSL_OP_SINGLE_ECDH_USE
  //            | SSL_OP_SINGLE_DH_USE | SSL_OP_CIPHER_SERVER_PREFERENCE;

  ssl_opts = (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) | SSL_OP_NO_COMPRESSION | SSL_OP_CIPHER_SERVER_PREFERENCE
             | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION | SSL_OP_NO_TICKET | SSL_OP_NO_RENEGOTIATION;

  // Removed: SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION
  // currently peer cert auth not added. Update: now added.
  SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);

  SSL_CTX_set_options(ssl_ctx, ssl_opts);

  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);
  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_RELEASE_BUFFERS);
  SSL_CTX_set_min_proto_version(ssl_ctx, DTLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx, DTLS1_3_VERSION);

  enable_key_logging(ssl_ctx, "../../../openair3/SCTP/cudtlskeylog.txt");

  SSL_CTX_set_num_tickets(ssl_ctx, 0); // Completely disable NewSessionTicket

  long opts = SSL_CTX_get_options(ssl_ctx);
  if (opts & SSL_OP_NO_TICKET) {
    printf("✅ SSL_OP_NO_TICKET is set\n");
  } else {
    printf("❌ SSL_OP_NO_TICKET is NOT set\n");
  }

  // SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);

  // if (SSL_CTX_set_cipher_list(ssl_ctx, DEFAULT_CIPHER_LIST) == 0) {
  //   SM_Logs(LOG_ERROR, _DTLS_, "Error setting cipher list: %s \n", ERR_error_string(ERR_get_error(), NULL));
  //   return NULL;
  // }

  if (load_ca_certificate(ssl_ctx,
                          "../../../openair3/SCTP/ca.crt",
                          "../../../openair3/SCTP")
      < 0) {
    SM_Logs(LOG_ERROR, _DTLS_, "Error loading CA certificate file");
    return NULL;
  } else {
    print_ca_certificate_details("../../../openair3/SCTP/ca.crt");
    SM_Logs(LOG_DEBUG, _DTLS_, "CA Certificate loaded.\n");
  }

  if (SSL_CTX_use_certificate_file(ssl_ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
    ERR_print_errors_fp(stderr);
    return NULL;
  }

  if (SSL_CTX_use_PrivateKey_file(ssl_ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
    ERR_print_errors_fp(stderr);
    return NULL;
  }

  if (!SSL_CTX_check_private_key(ssl_ctx)) {
    SM_Logs(LOG_ERROR, _DTLS_, "Private key does not match the certificate public key\n");
    return NULL;
  }

  // Print certificate details
  X509 *cert = SSL_CTX_get0_certificate(ssl_ctx);
  // print_repeated_char('-',30);
  fprintf(stdout, "\n-------------------CU CERTIFICATE-------------------------\n");

  // const char *text = "CERTIFICATE";
  // int width = 80;

  // print_centered_text(text, width);

  if (cert) {
    fprintf(stdout, "\nCertificate Information:\n");

    char *subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
    if (subject) {
      fprintf(stdout, "  Subject: %s\n", subject);
      OPENSSL_free(subject);
    }

    char *issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
    if (issuer) {
      fprintf(stdout, "  Issuer: %s\n", issuer);
      OPENSSL_free(issuer);
    }

    // char *cert_type = NULL;
    // size_t len;
    // SSL_CTX_get0_server_cert_type(ssl_ctx,cert_type,len);
    // fprintf(stdout,"    Key type: %s\n",cert_type);

    BIO *bp = BIO_new_fp(stdout, BIO_NOCLOSE);

    EVP_PKEY *pkey = X509_get_pubkey(cert);
    if (pkey) {
      fprintf(stdout, "  Public Key: ");
      EVP_PKEY_print_public(bp, pkey, 0, NULL);
      EVP_PKEY_free(pkey);
      BIO_free(bp);
    }

    const ASN1_BIT_STRING *signature = NULL;
    X509_get0_signature(&signature, NULL, cert);

    if (signature) {
      BIO *bio = BIO_new_fp(stdout, BIO_NOCLOSE);
      BIO_puts(bio, "   Certificate Signature:\n");
      BIO_dump_indent(bio, (const char *)signature->data, signature->length, 4);
      BIO_free(bio);
    }
  }
  printf("\n");
  // print_repeated_char('-',30);
  fprintf(stdout, "-------------------END CERTIFICATE----------------------\n\n");

  SSL_CTX_set_info_callback(ssl_ctx, info_callback);

  SM_Logs(LOG_INFO, _DTLS_, "SSL_CTX created for the listener (SERVER)");
  return ssl_ctx;

err:
  SSL_CTX_free(ssl_ctx);
  SM_Logs(LOG_ERROR, _DTLS_, "Failed to create ssl ctx");
  return NULL;
}

SSL_CTX *dtls_client_ctx_init(OSSL_PROVIDER *provider, OSSL_LIB_CTX *libctx, const char *cert_file, const char *key_file)
{
  SSL_library_init();
  SSL_load_error_strings();

  // enable_openssl_trace();
  OpenSSL_add_all_algorithms();

  SSL_CTX *ssl_ctx;
  uint64_t ssl_opts;

  ssl_ctx = SSL_CTX_new_ex(libctx, NULL, DTLS_client_method());
  if (!ssl_ctx) {
    SM_Logs(LOG_ERROR, _DTLS_, "Could not create SSL/TLS context: %s", ERR_error_string(ERR_get_error(), NULL));
    return NULL;
  }

  if (ssl_ctx == NULL) {
    goto err;
  }

  ssl_opts = (SSL_OP_ALL & ~SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) | SSL_OP_NO_COMPRESSION | SSL_VERIFY_NONE
             | SSL_OP_CIPHER_SERVER_PREFERENCE | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION | SSL_OP_NO_TICKET
             | SSL_OP_NO_RENEGOTIATION;



  SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);

  SSL_CTX_set_options(ssl_ctx, ssl_opts);

  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);
  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_RELEASE_BUFFERS);
  SSL_CTX_set_min_proto_version(ssl_ctx, DTLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx, DTLS1_3_VERSION);



  if (load_ca_certificate(ssl_ctx,
                          "../../../openair3/SCTP/ca.crt",
                          "../../../openair3/SCTP")
      < 0) {
    SM_Logs(LOG_ERROR, _DTLS_, "Error loading CA certificate file");
    return NULL;
  } else {
    SM_Logs(LOG_DEBUG, _DTLS_, "CA Certificate loaded.");
  }

  if (SSL_CTX_use_certificate_file(ssl_ctx, cert_file, SSL_FILETYPE_PEM) <= 0) {
    ERR_print_errors_fp(stderr);
    return NULL;
  }

  if (SSL_CTX_use_PrivateKey_file(ssl_ctx, key_file, SSL_FILETYPE_PEM) <= 0) {
    ERR_print_errors_fp(stderr);
    return NULL;
  }

  X509 *cert = SSL_CTX_get0_certificate(ssl_ctx);
  // print_repeated_char('-',30);
  fprintf(stdout, "\n-------------------DU CERTIFICATE-------------------------\n");



  if (cert) {
    fprintf(stdout, "\nCertificate Information:\n");

    char *subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
    if (subject) {
      fprintf(stdout, "  Subject: %s\n", subject);
      OPENSSL_free(subject);
    }

    char *issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
    if (issuer) {
      fprintf(stdout, "  Issuer: %s\n", issuer);
      OPENSSL_free(issuer);
    }


    BIO *bp = BIO_new_fp(stdout, BIO_NOCLOSE);

    EVP_PKEY *pkey = X509_get_pubkey(cert);
    if (pkey) {
      fprintf(stdout, "  Public Key: ");
      EVP_PKEY_print_public(bp, pkey, 0, NULL);
      EVP_PKEY_free(pkey);
      BIO_free(bp);
    }

    const ASN1_BIT_STRING *signature = NULL;
    X509_get0_signature(&signature, NULL, cert);

    if (signature) {
      BIO *bio = BIO_new_fp(stdout, BIO_NOCLOSE);
      BIO_puts(bio, "   Certificate Signature:\n");
      BIO_dump_indent(bio, (const char *)signature->data, signature->length, 4);
      BIO_free(bio);
    }
  }
  printf("\n");
  fprintf(stdout, "-------------------END CERTIFICATE----------------------\n\n");

  SSL_CTX_set_info_callback(ssl_ctx, info_callback);


  return ssl_ctx;

err:
  SSL_CTX_free(ssl_ctx);
  SM_Logs(LOG_ERROR, _DTLS_, "Failed to create ssl ctx");
  return NULL;
}

// Initialize a new SSL client
int ssl_client_init(SSL_CTX *ssl_ctx, struct ssl_client *client, int fd, size_t plain_text_size, enum sslmode mode)
{
  memset(client, 0, sizeof(Client));

  fprintf(stdout, "Initializing client\n");

  SSL *ssl;
  ssl = SSL_new(ssl_ctx);
  if (ssl == NULL) {
    SM_Logs(LOG_ERROR, _DTLS_, "Couldn't create ssl client");
    return -1;
  }

  SSL_set1_groups_list(ssl, "X25519MLKEM768");

  SM_Logs(LOG_DEBUG, _DTLS_, "DTLS version: %s", SSL_get_version(ssl));


  client->ssl = ssl;

  client->rbio = NULL;
  client->wbio = NULL;

  client->write_buf = NULL;
  client->encrypt_buf = NULL;
  client->plain_text_size = plain_text_size;
  client->sd = fd;
  client->handshake_done = 0;

  if (mode == SSLMODE_SERVER)
    SSL_set_accept_state(client->ssl); /* ssl server mode */
  else if (mode == SSLMODE_CLIENT)
    SSL_set_connect_state(client->ssl); /* ssl client mode */

  return 0;
}

void cleanup_ssl_ctx(SSL_CTX *ssl_ctx)
{
  SSL_CTX_free(ssl_ctx);
}

void cleanup_ssl_client(Client *client)
{
  SSL_free(client->ssl); // frees both the ssl object and the associated read/write BIOs.
  free(client->write_buf);
  free(client->encrypt_buf);

  free(client);
  client = NULL;
}

enum sslstatus get_sslstatus(SSL *ssl, int n)
{
  switch (SSL_get_error(ssl, n)) {
    case SSL_ERROR_NONE:
      return SSLSTATUS_OK;

    case SSL_ERROR_WANT_WRITE:
    case SSL_ERROR_WANT_READ:
      return SSLSTATUS_WANT_IO;

    case SSL_ERROR_ZERO_RETURN:
    case SSL_ERROR_SYSCALL:
    default:
      return SSLSTATUS_FAIL;
  }
}

void print_ssl_state(SSL *ssl)
{
  const char *current_state = SSL_state_string_long(ssl);
  SM_Logs(LOG_INFO, _DTLS_, "SSL state: %s", current_state);
}

void print_session_info(const SSL_SESSION *session)
{
  BIO *bio = BIO_new_fp(stdout, BIO_NOCLOSE);

  fprintf(stdout, "-------------------DTLS SESSION Details-------------------------\n");
  if (!SSL_SESSION_print(bio, session)) {
    SM_Logs(LOG_ERROR, _DTLS_, "Failed to print SSL session information.\n");
  }
  fprintf(stdout, "-----------------------------------------------------------------\n");

  const unsigned char *ticket;
  size_t ticket_len;

  SSL_SESSION_get0_ticket(session, &ticket, &ticket_len);

  if (ticket && ticket_len > 0) {
    BIO_puts(bio, "Session Ticket used for resumption: ");
    BIO_dump_indent(bio, (const char *)ticket, ticket_len, 4);

  } else {
    SM_Logs(LOG_DEBUG, _DTLS_, "No session ticket available.");
  }

  // Print the hostname associated with the session if set
  const char *hostname = SSL_SESSION_get0_hostname(session);
  if (hostname != NULL) {
    // BIO_printf(bio, "Hostname: %s\n", hostname);
    SM_Logs(LOG_INFO, _DTLS_, "Hostname: %s\n", hostname);
  } else {
    BIO_puts(bio, "Hostname: None\n");
    SM_Logs(LOG_INFO, _DTLS_, "Hostname: %s\n", "None");
  }

  BIO_free(bio);
}

enum sslstatus do_ssl_handshake(Client *client)
{


  SSL_set_fd(client->ssl, client->sd);

  int retval;
  retval = SSL_do_handshake(client->ssl);


  if (retval <= 0) {
    int err_code = SSL_get_error(client->ssl, retval);
    switch (err_code) {
      case SSL_ERROR_WANT_READ:
        break;
      case SSL_ERROR_WANT_WRITE: {
        break;
      }
      case SSL_ERROR_ZERO_RETURN:
        SM_Logs(LOG_ERROR, _DTLS_, "SSL connection closed");
        SSL_free(client->ssl);
        return SSLSTATUS_FAIL;
      case SSL_ERROR_SYSCALL:
        SM_Logs(LOG_ERROR, _DTLS_, "SSL syscall error: %s", strerror(errno));
        SSL_free(client->ssl);
        return SSLSTATUS_FAIL;
      case SSL_ERROR_SSL:
        SM_Logs(LOG_ERROR, _DTLS_, "SSL library error");
        ERR_print_errors_fp(stderr);
        SSL_free(client->ssl);
        return SSLSTATUS_FAIL;
      default:
        SM_Logs(LOG_ERROR, _DTLS_, "Unexpected SSL error");
        SSL_free(client->ssl);
        return SSLSTATUS_FAIL;
    }
  } else {
    SSL_SESSION *ssl_session = SSL_get_session(client->ssl); // Get session ID

    print_session_info(ssl_session);

    X509 *peer_cert = SSL_get_peer_certificate(client->ssl);
    if (peer_cert == NULL) {
      fprintf(stderr, "No peer certificate.\n");
    }

    // Check the certificate validity
    long verify_result = SSL_get_verify_result(client->ssl);
    if (verify_result != X509_V_OK) {
      fprintf(stderr, "Certificate verification error: %s\n", X509_verify_cert_error_string(verify_result));
      X509_free(peer_cert);
      return SSLSTATUS_FAIL;
    } else {
      SM_Logs(LOG_DEBUG, _DTLS_, "Peer certificate validated against Private CA:");
      X509_NAME *subject_name = X509_get_subject_name(peer_cert);
      char *subject_name_str = X509_NAME_oneline(subject_name, NULL, 0);
      SM_Logs(LOG_INFO, _DTLS_, "Subject Name: %s", subject_name_str);
      OPENSSL_free(subject_name_str);

      // Print the issuer name
      X509_NAME *issuer_name = X509_get_issuer_name(peer_cert);
      char *issuer_name_str = X509_NAME_oneline(issuer_name, NULL, 0);
      SM_Logs(LOG_INFO, _DTLS_, "Issuer Name: %s", issuer_name_str);
      OPENSSL_free(issuer_name_str);

      int sig_nid = X509_get_signature_nid(peer_cert);
      SM_Logs(LOG_INFO, _DTLS_, "Signature Algorithm: %s", OBJ_nid2ln(sig_nid)); 

      // Get raw signature
      ASN1_BIT_STRING *sig = NULL;
      X509_ALGOR *sig_alg = NULL;
      X509_get0_signature(&sig, &sig_alg, peer_cert);

      if (sig) {
        BIO *bio = BIO_new(BIO_s_mem());
        for (int i = 0; i < sig->length; i++) {
          BIO_printf(bio, "%02X", sig->data[i]);
        }

        char *sig_str;
        long sig_len = BIO_get_mem_data(bio, &sig_str);
        printf("     CA Signature:\n");
        SM_Logs(LOG_INFO, _DTLS_, " CA Signature :  (Length %ld bytes)", sig_len);
        for (int i = 0; i < 8 && i < sig->length; i++) {
          printf("%02X", sig->data[i]);
        }

        printf("...");

        // Print last 8 bytes
        for (int i = sig->length - 8; i < sig->length && i >= 0; i++) {
          printf("%02X", sig->data[i]);
        }

        printf("\n");

        BIO_free(bio);
      }
      EVP_PKEY *pubkey = X509_get_pubkey(peer_cert);
      if (pubkey) {
        BIO *bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PUBKEY(bio, pubkey);

        char *pubkey_str;
        long pubkey_len = BIO_get_mem_data(bio, &pubkey_str);
        printf("      Public Key:\n%.*s\n", (int)pubkey_len, pubkey_str);

        BIO_free(bio);
        EVP_PKEY_free(pubkey);
      }
    }
    X509_free(peer_cert);
    printf("\n");

    BIO *rbio = BIO_new(BIO_s_mem());
    // we write encrypted data from the socket to rbio using BIO write and then read from using read
    BIO *wbio = BIO_new(BIO_s_mem());

    if (rbio == NULL || wbio == NULL) {
      SM_Logs(LOG_ERROR, _DTLS_, "Couldn't set up the Basic I/O");
      SSL_free(client->ssl);
      return -1;
    }

    SSL_set_bio(client->ssl, rbio, wbio);
    client->rbio = rbio;
    client->wbio = wbio;

    return SSLSTATUS_OK;
  }
  return SSLSTATUS_WANT_IO;
}

// Helper function to allocate memory and copy data
void allocate_and_copy(uint8_t **dst, const uint8_t *src, size_t len)
{
  if (*dst != NULL) {
    free(*dst);
  }
  *dst = calloc(len, 1);
  if (*dst == NULL) {
    // Handle memory allocation error
    perror("calloc");
    exit(EXIT_FAILURE);
  }
  // fprintf(stdout, "Space allocated.\n");
  memcpy(*dst, src, len);
}

void send_unencrypted_bytes(Client *client, uint8_t *buf, size_t buf_len)
{
  // allocate_and_copy(&client->encrypt_buf, buf, buf_len);
  client->encrypt_buf = calloc(buf_len, sizeof(uint8_t));
  if (client->encrypt_buf == NULL) {
    perror("calloc failed in send_unencrypted_bytes");
    exit(EXIT_FAILURE);
  }
  client->encrypt_len = buf_len;
  memcpy(client->encrypt_buf, buf, buf_len);
}

void queue_encrypted_bytes(Client *client, uint8_t *buf, size_t buf_len)
{
  allocate_and_copy(&client->write_buf, buf, buf_len);
  client->write_len = buf_len;
}

// Send the buffer data for encryption.
// void send_unencrypted_bytes(Client *client, const char *buf, size_t buf_len)
// {
//   if (client->encrypt_buf != NULL) {
//     free(client->encrypt_buf);
//   }
//   client->encrypt_buf = (char *)malloc(buf_len);
//   if (client->encrypt_buf == NULL) {
//     perror("malloc");
//     exit(EXIT_FAILURE);
//   }
//   memcpy(client->encrypt_buf, buf, buf_len);
// }

// /* Encrypted bytes from the underlying BIO get queued at the write buf, ready for socket write. */
// void queue_encrypted_bytes(Client *client, const char *buf, size_t buf_len)
// {
//   /* clear the prev data, we are sending all the buffer at once */
//   if (client->write_buf != NULL) {
//     free(client->write_buf);
//   }

//   client->write_buf = (char *)malloc(buf_len);
//   if (client->write_buf == NULL) {
//     perror("malloc");
//     exit(EXIT_FAILURE);
//   }

//   // Copy the new data into the buffer
//   memcpy(client->write_buf, buf, buf_len);

//   // Update the write length to the new buffer length
//   client->write_len = buf_len;
// }

/* encrypts the buffer data and stores it in write buf (which is the final payload to be sent) */
int encrypt_buf(Client *client)
{
  uint8_t *buf = (uint8_t *)malloc(client->plain_text_size);
  enum sslstatus status;

  // if (!SSL_is_init_finished(client->ssl))
  //   return 0;

  SM_Logs(LOG_INFO, _DTLS_, "Encrypted buffer: %lu bytes", client->encrypt_len);

  while (client->encrypt_len > 0) {
    /* Reads the un-encrypted bytes from the enc-buf, & writes the encrypted data to the underlying write BIO. */
    int n = SSL_write(client->ssl, client->encrypt_buf, client->encrypt_len); // n : no of bytes written.
    status = get_sslstatus(client->ssl, n);

    SM_Logs(LOG_INFO, _DTLS_, "SSL Write: %d bytes", n);

    /* write op. successful */
    if (n > 0) {
      /* consume the waiting bytes that have been used by SSL  (if new bytes have arrived.)*/
      if ((size_t)n < client->encrypt_len) {
        fprintf(stdout, "More data\n");
        memmove(client->encrypt_buf, client->encrypt_buf + n, client->encrypt_len - n);
      }

      client->encrypt_len -= n;
      if (client->encrypt_len != 0) {
        fprintf(stdout, "more bytes\n");
        /* if there are still bytes left in the buffer, we need to encrypt them */
        client->encrypt_buf = (uint8_t *)realloc(client->encrypt_buf, client->encrypt_len);
        if (client->encrypt_buf == NULL) {
          fprintf(stderr, " encrypt buffer realloc errors\n");
          return -1;
        }
      }

      /* take the output of the SSL object and queue it for socket write */
      int bytes_read;

      // do {
      bytes_read = BIO_read(client->wbio, buf, client->plain_text_size); // n -> number of encrypted bytes that have been read.
      // fprintf(stdout, "Number of bytes read: %d\n", bytes_read);
      if (bytes_read > 0)
        queue_encrypted_bytes(client, (uint8_t *)buf, bytes_read); // to be written to the socket.
      else if (!BIO_should_retry(client->wbio)) {
        continue;
      } else {
        unsigned long err = ERR_get_error();
        fprintf(stderr, "BIO read failed with error code: %lu\n", err);
        ERR_print_errors_fp(stderr);

        return -1;
      }
      // } while (bytes_read > 0);
    } else {
      handle_ssl_error(client->ssl, n);
    }

    if (status == SSLSTATUS_FAIL)
      return -1;

    if (n == 0)
      fprintf(stdout, "n=0\n");
    break;
  }
  return 0;
}
/* High level function to do the underlying low level tasks of encrypting & queueing*/
int new_message_encrypt(Client *c, uint8_t *buf, size_t buf_len)
{
  send_unencrypted_bytes(c, buf, buf_len);

  int res = encrypt_buf(c);
  return res;
}

void new_message_decrypt(Client *c, uint8_t *src, size_t src_len, uint8_t *buf)
{
  read_enc_bytes(c, src, src_len, buf);
}

/* since this is over a non blocking socket, bytes will keep on coming. */

/* Read bytes coming from the socket (via src), write them to rbio, decrypt them via SSL read & copy to  */

int sock_read(Client *c, char *buf, size_t buf_len)
{
  int bytes_read;
  bytes_read = BIO_write(c->rbio, buf, buf_len);
  if (bytes_read == -1) {
    fprintf(stderr, "error writing to BIO.");
    return -1;
  }
  return 0;
}

int read_enc_bytes(Client *client,
                   uint8_t *src,
                   size_t src_len,
                   uint8_t *buf) // src_len =  length of enc bytes =/= length of un-enc bytes = 8192.
{
  uint8_t buf_copy[client->plain_text_size];

  enum sslstatus status;
  int n;

  while (src_len > 0) {
    n = BIO_write(client->rbio, src, src_len);

    // fprintf(stdout, "Bytes written to rbio: %d\n", n);

    if (n <= 0)
      return -1; /* if BIO write fails, assume unrecoverable */

    src += n;
    src_len -= n;

    n = SSL_read(client->ssl, buf_copy, sizeof(buf_copy)); // read into copy, and then use memcpy if successful.
    if (n > 0) {
      SM_Logs(LOG_INFO, _DTLS_, "SSL read: %d bytes\n", n);
      memcpy(buf, buf_copy, sizeof(buf_copy));
    } else {
      int i;
      i = handle_ssl_error(client->ssl, n);
    }
    // }

    return 0;
  }
  return 0;
}

int handle_ssl_error(SSL *ssl, int retval)
{
  int err_val = SSL_get_error(ssl, retval);
  switch (err_val) {
    case SSL_ERROR_ZERO_RETURN:
      SM_Logs(LOG_ERROR, _DTLS_, "SSL failed with SSL_ERROR_ZERO_RETURN");
      break;
    case SSL_ERROR_WANT_READ:
      break;
    case SSL_ERROR_WANT_WRITE:
      break;
    case SSL_ERROR_WANT_CONNECT:
      SM_Logs(LOG_ERROR, _DTLS_, "SSL failed with SSL_ERROR_WANT_CONNECT");
      break;
    case SSL_ERROR_WANT_ACCEPT:
      SM_Logs(LOG_ERROR, _DTLS_, "SSL failed with SSL_ERROR_WANT_ACCEPT");
      break;
    case SSL_ERROR_WANT_X509_LOOKUP:
      SM_Logs(LOG_ERROR, _DTLS_, "SSL failed with SSL_ERROR_WANT_X509_LOOKUP");
      break;
    case SSL_ERROR_SYSCALL:
      SM_Logs(LOG_ERROR, _DTLS_, "SSL failed with SSL_ERROR_SYSCALL");
      break;
    case SSL_ERROR_SSL:
      SM_Logs(LOG_ERROR, _DTLS_, "SSL failed with SSL_ERROR_SSL");
      break;
    default:
      SM_Logs(LOG_ERROR, _DTLS_, "SSL failed with unknown error");
      break;
  }
  return err_val;
}

