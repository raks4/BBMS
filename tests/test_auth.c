#include "test_util.h"
#include "admin.h"
#include "auth.h"
#include <string.h>

int main(void)
{
    AdminAccount a;
    memset(&a, 0, sizeof a);
    a.admin_id = 1;
    strcpy(a.username, "root_admin");
    a.kdf_iterations = KDF_ITERATIONS_DEFAULT;
    CHECK(security_hash_password("Correct-Horse-9", a.salt, a.password_hash, a.kdf_iterations));
    CHECK(auth_verify_password(&a, "Correct-Horse-9"));
    CHECK(!auth_verify_password(&a, "correct-horse-9"));

    AdminNode *list = NULL;
    CHECK(auth_bootstrap_required(list, false));
    CHECK(!auth_bootstrap_required(list, true));        /* marker => fail safe, no re-bootstrap */
    CHECK(admin_list_add(&list, &a));
    CHECK(!admin_list_add(&list, &a));                  /* duplicate id/username */
    CHECK(!auth_bootstrap_required(list, false));
    CHECK(auth_has_active_admin(list));
    list->data.status = STATUS_INACTIVE;
    CHECK(!auth_has_active_admin(list));

    AdminSession none = {0};
    CHECK(!auth_is_authorized(&none));
    CHECK(!admin_create_account(&none, &list));
    CHECK(!admin_deactivate_account(&none, list, 1));
    CHECK(!admin_change_password(&none, list));
    CHECK(!admin_reset_password(&none, list, 1));
    AdminSession ok = { true, 1, "root_admin" };
    CHECK(auth_is_authorized(&ok));
    admin_free_all(&list);
    TEST_DONE();
}
