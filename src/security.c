#include "security.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

static bool derive(const char *password, const unsigned char *salt,
                   unsigned int iterations, unsigned char *out)
{
    if (!password || !salt || !out) return false;
    if (iterations < KDF_ITERATIONS_MIN || iterations > KDF_ITERATIONS_MAX) return false;
    return PKCS5_PBKDF2_HMAC(password, (int)strlen(password), salt, SALT_SIZE,
                             (int)iterations, EVP_sha256(), HASH_SIZE, out) == 1;
}

bool security_hash_password(const char *password, unsigned char *salt,
                            unsigned char *hash, unsigned int iterations)
{
    if (!salt || !hash) return false;
    if (RAND_bytes(salt, SALT_SIZE) != 1) return false;      /* CSPRNG */
    return derive(password, salt, iterations, hash);
}

bool security_verify_password(const char *password, const unsigned char *salt,
                              const unsigned char *expected_hash,
                              unsigned int iterations)
{
    unsigned char tmp[HASH_SIZE];
    if (!expected_hash || !derive(password, salt, iterations, tmp)) return false;
    bool ok = CRYPTO_memcmp(tmp, expected_hash, HASH_SIZE) == 0;
    security_wipe(tmp, sizeof tmp);
    return ok;
}

void security_wipe(void *p, size_t n)
{
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) *v++ = 0;
}

bool security_constant_time_equals(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (unsigned char)(la != lb);
    size_t n = la < lb ? la : lb;
    for (size_t i = 0; i < n; i++) diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

bool security_read_password(const char *prompt, char *buf, size_t cap)
{
    size_t n = 0;
    fputs(prompt, stdout);
    fflush(stdout);
#ifdef _WIN32
    int c;
    while ((c = _getch()) != '\r' && c != '\n' && c != EOF) {
        if (c == '\b') { if (n) n--; continue; }
        if (n + 1 < cap) buf[n++] = (char)c;   /* extra chars are dropped */
    }
    putchar('\n');
#else
    struct termios oldt, newt;
    bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0;
    if (tty) {
        newt = oldt;
        newt.c_lflag &= (tcflag_t)~ECHO;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt);
    }
    int c;
    bool got_any = false;
    while ((c = getchar()) != '\n' && c != EOF) {
        got_any = true;
        if (n + 1 < cap) buf[n++] = (char)c;
    }
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
        putchar('\n');
    }
    if (c == EOF && !got_any) { buf[0] = '\0'; return false; }
#endif
    buf[n] = '\0';
    return true;
}

static void put_sanitized(FILE *f, const char *s)
{
    size_t i = 0;
    for (; s && s[i] && i < 200; i++) {
        unsigned char c = (unsigned char)s[i];
        fputc((c < 0x20 || c == 0x7f || c == '|') ? '?' : c, f);   /* no log injection */
    }
}

void security_audit_event(int admin_id, const char *event_type, const char *result)
{
    FILE *f = fopen(DATA_DIR "/audit.log", "a");
    if (!f) { fprintf(stderr, "WARNING: audit log unavailable.\n"); return; }
    time_t t = time(NULL);
    char ts[32] = "unknown";
    struct tm *tp = localtime(&t);
    if (tp) strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%S", tp);
    fprintf(f, "%s|%d|", ts, admin_id);
    put_sanitized(f, event_type);
    fputc('|', f);
    put_sanitized(f, result);
    fputc('\n', f);
    if (fclose(f) != 0) fprintf(stderr, "WARNING: audit write failed.\n");
}
