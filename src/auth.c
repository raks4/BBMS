#include "auth.h"
#include "validation.h"

#include <stdio.h>
#include <string.h>

bool auth_verify_password(const AdminAccount *a, const char *password)
{
    if (!a || !password) return false;
    return security_verify_password(password, a->salt, a->password_hash,
                                    a->kdf_iterations);
}

AdminNode *auth_find_admin(AdminNode *admins, const char *username)
{
    for (; admins; admins = admins->next)
        if (strcmp(admins->data.username, username) == 0) return admins;
    return NULL;
}

bool auth_has_active_admin(const AdminNode *admins)
{
    for (; admins; admins = admins->next)
        if (admins->data.status == STATUS_ACTIVE) return true;
    return false;
}

bool auth_bootstrap_required(const AdminNode *admins, bool marker)
{
    return admins == NULL && !marker;
}

bool auth_is_authorized(const AdminSession *s)
{
    return s && s->authenticated && s->admin_id > 0;
}

bool auth_login(AdminNode *admins, AdminSession *session)
{
    static const unsigned char dummy_salt[SALT_SIZE] = {0};
    static const unsigned char dummy_hash[HASH_SIZE] = {0};

    memset(session, 0, sizeof *session);
    for (int attempt = 0; attempt < MAX_LOGIN_ATTEMPTS; attempt++) {
        char user[USERNAME_LEN + 8];
        char pw[PASSWORD_MAX_LEN + 2];
        if (!input_line("Username: ", user, sizeof user)) return false;
        trim_in_place(user);
        if (!security_read_password("Password: ", pw, sizeof pw)) return false;

        AdminNode *n = validate_username(user) ? auth_find_admin(admins, user) : NULL;
        bool ok;
        if (n) {
            ok = auth_verify_password(&n->data, pw) && n->data.status == STATUS_ACTIVE;
        } else {
            /* Same work for unknown users so timing does not reveal them. */
            (void)security_verify_password(pw, dummy_salt, dummy_hash,
                                           KDF_ITERATIONS_DEFAULT);
            ok = false;
        }
        security_wipe(pw, sizeof pw);

        if (ok) {
            session->authenticated = true;
            session->admin_id = n->data.admin_id;
            snprintf(session->username, sizeof session->username, "%s", n->data.username);
            security_audit_event(session->admin_id, "LOGIN", "OK");
            return true;
        }
        char detail[96];
        snprintf(detail, sizeof detail, "FAIL user=%.40s", user);
        security_audit_event(n ? n->data.admin_id : 0, "LOGIN", detail);
        puts("Invalid credentials or inactive account.");
    }
    security_audit_event(0, "LOGIN", "TOO_MANY_FAILURES");
    puts("Too many failed attempts.");
    return false;
}

void auth_logout(AdminSession *session)
{
    if (session && session->authenticated)
        security_audit_event(session->admin_id, "LOGOUT", "OK");
    if (session) memset(session, 0, sizeof *session);
}
