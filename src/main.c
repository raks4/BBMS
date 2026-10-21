#include "admin.h"
#include "app.h"
#include "auth.h"
#include "storage.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    AppData app;
    AdminSession session;
    int rc = 1;
    memset(&app, 0, sizeof app);
    memset(&session, 0, sizeof session);

    /* 1-2. Initialize storage, load and validate records. */
    if (!storage_init()) { fprintf(stderr, "Cannot create the %s directory.\n", DATA_DIR); return 1; }
    if (!storage_load_all(&app)) {
        fprintf(stderr, "Startup aborted: data could not be loaded safely.\n");
        return 1;
    }

    /* 3. One-time admin setup, or fail safe if credentials vanished. */
    if (auth_bootstrap_required(app.admins, storage_setup_marker_exists())) {
        puts("No administrator exists yet: first-time setup.");
        if (!admin_create_initial_account(&app.admins)) goto done;
    } else if (!auth_has_active_admin(app.admins)) {
        fprintf(stderr, "No active administrator account found, but setup was completed earlier.\n"
                        "The credential store is missing or damaged. A controlled recovery is\n"
                        "required; the first administrator will NOT be recreated automatically.\n");
        goto done;
    }

    /* 4. Mandatory login. */
    puts("\nBlood Bank Management System - administrator login");
    if (!auth_login(app.admins, &session)) goto done;

    /* 5-6. Menus. */
    ui_run_menus(&app, &session);

    /* 7. Final save, checked. */
    if (!storage_save_all(&app)) { fprintf(stderr, "ERROR: final save failed.\n"); goto done; }
    rc = 0;

done:
    /* 8. End session and free memory. */
    auth_logout(&session);
    storage_cleanup_all(&app);
    return rc;
}
