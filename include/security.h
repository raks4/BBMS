#ifndef SECURITY_H
#define SECURITY_H

#include "common.h"

#define SALT_SIZE               16
#define HASH_SIZE               32
#define KDF_ITERATIONS_DEFAULT  600000u   /* PBKDF2-HMAC-SHA-256 */
#define KDF_ITERATIONS_MIN      100000u
#define KDF_ITERATIONS_MAX      50000000u
#define PASSWORD_MAX_LEN        128

/* Generates a random salt (written to salt) and derives the hash. */
bool security_hash_password(const char *password, unsigned char *salt,
                            unsigned char *hash, unsigned int iterations);
bool security_verify_password(const char *password,
                              const unsigned char *salt,
                              const unsigned char *expected_hash,
                              unsigned int iterations);

/* Reads a password without echo. buf must hold PASSWORD_MAX_LEN + 2. */
bool security_read_password(const char *prompt, char *buf, size_t cap);
bool security_constant_time_equals(const char *a, const char *b);
void security_wipe(void *p, size_t n);

/* Appends one sanitized line to data/audit.log. Never pass passwords. */
void security_audit_event(int admin_id, const char *event_type,
                          const char *result);

#endif
