#ifndef AUTH_H
#define AUTH_H

#include "common.h"
#include "security.h"

#define MAX_LOGIN_ATTEMPTS 3

typedef struct {
    int admin_id;
    char username[USERNAME_LEN];
    unsigned char salt[SALT_SIZE];
    unsigned char password_hash[HASH_SIZE];
    unsigned int kdf_iterations;           /* PBKDF2-HMAC-SHA-256 */
    AccountStatus status;
} AdminAccount;

typedef struct AdminNode {
    AdminAccount data;
    struct AdminNode *next;
} AdminNode;

typedef struct {
    bool authenticated;
    int admin_id;
    char username[USERNAME_LEN];
} AdminSession;

bool auth_login(AdminNode *admins, AdminSession *session);
void auth_logout(AdminSession *session);
bool auth_verify_password(const AdminAccount *account, const char *password);

/* True only on a genuine first run: no accounts AND no setup marker. */
bool auth_bootstrap_required(const AdminNode *admins, bool setup_marker_exists);
bool auth_has_active_admin(const AdminNode *admins);
AdminNode *auth_find_admin(AdminNode *admins, const char *username);

/* The single authorization check used by every protected operation. */
bool auth_is_authorized(const AdminSession *session);

#endif
