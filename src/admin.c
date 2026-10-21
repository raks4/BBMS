#include "admin.h"
#include "storage.h"
#include "validation.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- list helpers ---------------- */

int admin_next_id(const AdminNode *h)
{
    int m = 0;
    for (; h; h = h->next) if (h->data.admin_id > m) m = h->data.admin_id;
    return m + 1;
}

bool admin_list_add(AdminNode **head, const AdminAccount *a)
{
    if (!head || !a || a->admin_id <= 0 || !validate_username(a->username)) return false;
    if (a->status != STATUS_ACTIVE && a->status != STATUS_INACTIVE) return false;
    if (a->kdf_iterations < KDF_ITERATIONS_MIN || a->kdf_iterations > KDF_ITERATIONS_MAX)
        return false;
    for (AdminNode *n = *head; n; n = n->next)
        if (n->data.admin_id == a->admin_id ||
            strcmp(n->data.username, a->username) == 0) return false;
    AdminNode *node = calloc(1, sizeof *node);
    if (!node) return false;
    node->data = *a;
    AdminNode **pp = head;
    while (*pp) pp = &(*pp)->next;
    *pp = node;
    return true;
}

static void remove_node(AdminNode **head, AdminNode *target)
{
    for (AdminNode **pp = head; *pp; pp = &(*pp)->next)
        if (*pp == target) { *pp = target->next; free(target); return; }
}

void admin_free_all(AdminNode **head)
{
    while (*head) { AdminNode *n = *head; *head = n->next; free(n); }
}

/* ---------------- helpers ---------------- */

static bool contains_ci(const char *hay, const char *needle)
{
    size_t hl = strlen(hay), nl = strlen(needle);
    if (nl == 0 || nl > hl) return false;
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t j = 0;
        while (j < nl && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) j++;
        if (j == nl) return true;
    }
    return false;
}

static bool prompt_username(const AdminNode *admins, char *out)
{
    for (int tries = 0; tries < 3; tries++) {
        char u[USERNAME_LEN + 8];
        if (!input_line("New username (3-31 chars: letters, digits, _ . -): ", u, sizeof u))
            return false;
        trim_in_place(u);
        if (!validate_username(u)) { puts("Invalid username."); continue; }
        if (auth_find_admin((AdminNode *)admins, u)) { puts("Username already exists."); continue; }
        snprintf(out, USERNAME_LEN, "%.*s", USERNAME_LEN - 1, u);
        return true;
    }
    return false;
}

static bool prompt_new_password(const char *username, const AdminAccount *current,
                                char *pw, size_t cap)
{
    char confirm[PASSWORD_MAX_LEN + 2];
    for (int tries = 0; tries < 3; tries++) {
        if (!security_read_password("New password (12+ chars with upper, lower, digit, symbol): ", pw, cap))
            return false;
        if (!validate_password_policy(pw)) { puts("Password does not meet the policy."); continue; }
        if (contains_ci(pw, username)) { puts("Password must not contain the username."); continue; }
        if (current && auth_verify_password(current, pw)) {
            puts("The new password must differ from the current one.");
            continue;
        }
        if (!security_read_password("Confirm password: ", confirm, sizeof confirm)) return false;
        bool same = security_constant_time_equals(pw, confirm);
        security_wipe(confirm, sizeof confirm);
        if (!same) { puts("Passwords do not match."); continue; }
        return true;
    }
    return false;
}

/* Adds the account, saves, and rolls back in memory if saving fails. */
static bool create_and_persist(AdminNode **admins, const char *username, const char *pw)
{
    AdminAccount a;
    memset(&a, 0, sizeof a);
    a.admin_id = admin_next_id(*admins);
    snprintf(a.username, sizeof a.username, "%s", username);
    a.kdf_iterations = KDF_ITERATIONS_DEFAULT;
    a.status = STATUS_ACTIVE;
    if (!security_hash_password(pw, a.salt, a.password_hash, a.kdf_iterations)) {
        puts("ERROR: password hashing failed.");
        return false;
    }
    if (!admin_list_add(admins, &a)) { puts("ERROR: could not add account."); return false; }
    AdminNode *added = auth_find_admin(*admins, username);
    if (!storage_save_admins(*admins)) {
        remove_node(admins, added);
        puts("ERROR: could not save the account; nothing was created.");
        return false;
    }
    return true;
}

/* ---------------- public API ---------------- */

bool admin_create_initial_account(AdminNode **admins)
{
    if (!auth_bootstrap_required(*admins, storage_setup_marker_exists())) {
        puts("Initial setup is already complete.");
        return false;
    }
    const char *secret = getenv("BBMS_SETUP_SECRET");
    if (secret && *secret) {
        char in[PASSWORD_MAX_LEN + 2];
        if (!security_read_password("Setup secret: ", in, sizeof in)) return false;
        bool ok = security_constant_time_equals(in, secret);
        security_wipe(in, sizeof in);
        if (!ok) {
            security_audit_event(0, "BOOTSTRAP", "BAD_SETUP_SECRET");
            puts("Wrong setup secret.");
            return false;
        }
    } else {
        puts("WARNING: BBMS_SETUP_SECRET is not set. Anyone with console access can");
        puts("         create the first administrator. Set it for real deployments.");
    }

    char username[USERNAME_LEN];
    char pw[PASSWORD_MAX_LEN + 2];
    if (!prompt_username(*admins, username)) return false;
    if (!prompt_new_password(username, NULL, pw, sizeof pw)) return false;
    bool ok = create_and_persist(admins, username, pw);
    security_wipe(pw, sizeof pw);
    if (!ok) return false;

    if (!storage_write_setup_marker())
        puts("WARNING: could not write the setup marker file.");
    security_audit_event(1, "BOOTSTRAP", "INITIAL_ADMIN_CREATED");
    puts("Initial administrator created.");
    return true;
}

bool admin_create_account(const AdminSession *session, AdminNode **admins)
{
    if (!auth_is_authorized(session)) { puts("Not authorized."); return false; }
    char username[USERNAME_LEN];
    char pw[PASSWORD_MAX_LEN + 2];
    if (!prompt_username(*admins, username)) return false;
    if (!prompt_new_password(username, NULL, pw, sizeof pw)) return false;
    bool ok = create_and_persist(admins, username, pw);
    security_wipe(pw, sizeof pw);
    char detail[96];
    snprintf(detail, sizeof detail, "%s user=%.40s", ok ? "OK" : "FAIL", username);
    security_audit_event(session->admin_id, "ADMIN_CREATE", detail);
    if (ok) puts("Administrator account created.");
    return ok;
}

bool admin_deactivate_account(const AdminSession *session, AdminNode *admins,
                              int target_id)
{
    if (!auth_is_authorized(session)) { puts("Not authorized."); return false; }
    AdminNode *t = NULL;
    int active = 0;
    for (AdminNode *n = admins; n; n = n->next) {
        if (n->data.status == STATUS_ACTIVE) active++;
        if (n->data.admin_id == target_id) t = n;
    }
    if (!t) { puts("No such account."); return false; }
    if (t->data.status != STATUS_ACTIVE) { puts("Account is already inactive."); return false; }
    if (target_id == session->admin_id) { puts("You cannot deactivate your own account."); return false; }
    if (active <= 1) { puts("Cannot deactivate the last active administrator."); return false; }

    t->data.status = STATUS_INACTIVE;
    if (!storage_save_admins(admins)) {
        t->data.status = STATUS_ACTIVE;
        puts("ERROR: could not save; account left active.");
        return false;
    }
    char detail[48];
    snprintf(detail, sizeof detail, "OK target=%d", target_id);
    security_audit_event(session->admin_id, "ADMIN_DEACTIVATE", detail);
    puts("Account deactivated.");
    return true;
}

void admin_list_accounts(const AdminSession *session, const AdminNode *admins)
{
    if (!auth_is_authorized(session)) { puts("Not authorized."); return; }
    printf("\n%-5s %-24s %s\n", "ID", "Username", "Status");
    for (; admins; admins = admins->next)
        printf("%-5d %-24s %s\n", admins->data.admin_id, admins->data.username,
               admins->data.status == STATUS_ACTIVE ? "active" : "inactive");
}

/* ---------------- password change / reset ---------------- */

static AdminNode *find_by_id(AdminNode *admins, int id)
{
    for (; admins; admins = admins->next) if (admins->data.admin_id == id) return admins;
    return NULL;
}

/* Confirms the person at the keyboard still knows their own password. */
static bool reauthenticate(const AdminSession *s, AdminNode *admins)
{
    AdminNode *me = find_by_id(admins, s->admin_id);
    if (!me || me->data.status != STATUS_ACTIVE || strcmp(me->data.username, s->username) != 0) {
        puts("Session does not match an active account.");
        return false;
    }
    for (int tries = 0; tries < MAX_LOGIN_ATTEMPTS; tries++) {
        char pw[PASSWORD_MAX_LEN + 2];
        if (!security_read_password("Your current password: ", pw, sizeof pw)) return false;
        bool ok = auth_verify_password(&me->data, pw);
        security_wipe(pw, sizeof pw);
        if (ok) return true;
        security_audit_event(s->admin_id, "PASSWORD_REAUTH", "FAIL");
        puts("Incorrect password.");
    }
    return false;
}

/* New salt + hash with current parameters; restores the old values if the
 * save fails so memory never disagrees with disk. */
static bool apply_new_password(AdminNode *admins, AdminNode *target, const char *pw)
{
    AdminAccount backup = target->data;
    if (!security_hash_password(pw, target->data.salt, target->data.password_hash,
                                KDF_ITERATIONS_DEFAULT)) {
        target->data = backup;
        puts("ERROR: password hashing failed.");
        return false;
    }
    target->data.kdf_iterations = KDF_ITERATIONS_DEFAULT;
    if (!storage_save_admins(admins)) {
        target->data = backup;
        puts("ERROR: could not save; the old password is still in effect.");
        return false;
    }
    return true;
}

bool admin_change_password(const AdminSession *session, AdminNode *admins)
{
    if (!auth_is_authorized(session)) { puts("Not authorized."); return false; }
    if (!reauthenticate(session, admins)) return false;
    AdminNode *me = find_by_id(admins, session->admin_id);

    char pw[PASSWORD_MAX_LEN + 2];
    if (!prompt_new_password(me->data.username, &me->data, pw, sizeof pw)) return false;
    bool ok = apply_new_password(admins, me, pw);
    security_wipe(pw, sizeof pw);
    security_audit_event(session->admin_id, "PASSWORD_CHANGE", ok ? "OK" : "FAIL");
    if (ok) puts("Password changed.");
    return ok;
}

bool admin_reset_password(const AdminSession *session, AdminNode *admins, int target_id)
{
    if (!auth_is_authorized(session)) { puts("Not authorized."); return false; }
    if (target_id == session->admin_id) { puts("Use 'change my password' for your own account."); return false; }
    AdminNode *t = find_by_id(admins, target_id);
    if (!t) { puts("No such account."); return false; }
    if (!reauthenticate(session, admins)) return false;

    printf("Setting a new password for '%s'. Give it to them securely.\n", t->data.username);
    char pw[PASSWORD_MAX_LEN + 2];
    if (!prompt_new_password(t->data.username, &t->data, pw, sizeof pw)) return false;
    bool ok = apply_new_password(admins, t, pw);
    security_wipe(pw, sizeof pw);
    char detail[48];
    snprintf(detail, sizeof detail, "%s target=%d", ok ? "OK" : "FAIL", target_id);
    security_audit_event(session->admin_id, "PASSWORD_RESET", detail);
    if (ok) puts("Password reset.");
    return ok;
}
