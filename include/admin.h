#ifndef ADMIN_H
#define ADMIN_H

#include "auth.h"

bool admin_create_initial_account(AdminNode **admins);
bool admin_create_account(const AdminSession *session, AdminNode **admins);
bool admin_deactivate_account(const AdminSession *session, AdminNode *admins,
                              int target_admin_id);
void admin_list_accounts(const AdminSession *session, const AdminNode *admins);

/* Both re-verify the caller's current password before doing anything. */
bool admin_change_password(const AdminSession *session, AdminNode *admins);
bool admin_reset_password(const AdminSession *session, AdminNode *admins,
                          int target_admin_id);

/* Used by the storage layer; validates and rejects duplicates. */
bool admin_list_add(AdminNode **head, const AdminAccount *account);
int  admin_next_id(const AdminNode *head);
void admin_free_all(AdminNode **head);

#endif
