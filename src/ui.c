#include "ui.h"
#include "admin.h"
#include "reports.h"
#include "security.h"
#include "storage.h"
#include "validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Persist the in-memory state. If saving fails, discard the in-memory
 * changes by reloading the last good copy from disk. */
static bool commit(AppData *app)
{
    if (storage_save_all(app)) return true;
    fprintf(stderr, "ERROR: could not save data. The last operation was rolled back.\n");
    storage_cleanup_all(app);
    if (!storage_load_all(app)) {
        fprintf(stderr, "FATAL: could not reload data. Exiting.\n");
        exit(1);
    }
    return false;
}

static void audit(const AdminSession *s, const char *event, const char *fmt, int v)
{
    char detail[64];
    snprintf(detail, sizeof detail, fmt, v);
    security_audit_event(s->admin_id, event, detail);
}

static void sweep_expiry(AppData *app)
{
    int n = inventory_release_expired(app->batches, date_today());
    if (n > 0) {
        printf("Note: %d batch(es) expired and were removed from issuable stock.\n", n);
        (void)commit(app);
    }
}

/* ---------------- donors ---------------- */

static void donor_menu(AppData *app, const AdminSession *s)
{
    for (;;) {
        puts("\n-- Donors --\n1) Add  2) View  3) Update contact/name  4) Deactivate  5) List  0) Back");
        int c;
        if (!input_int("> ", &c, 0, 5) || c == 0) return;
        int id;
        if (c == 1) {
            Donor d;
            memset(&d, 0, sizeof d);
            if (!input_name("Full name: ", d.name, sizeof d.name)) return;
            if (!input_contact("Contact number: ", d.contact, sizeof d.contact)) return;
            if (!input_blood_group("Blood group (A+ A- B+ B- AB+ AB- O+ O-): ", &d.blood_group)) return;
            if (!input_date("Date of birth (YYYY-MM-DD): ", &d.date_of_birth)) return;
            d.donor_id = donor_next_id(app->donors);
            d.active = true;
            if (!donor_add(&app->donors, &d)) puts("Could not add donor (invalid data).");
            else if (commit(app)) { printf("Donor added with ID %d.\n", d.donor_id); audit(s, "DONOR_ADD", "id=%d", d.donor_id); }
        } else if (c == 5) {
            donor_list(app->donors);
        } else {
            if (!input_int("Donor ID: ", &id, 1, 2000000000)) return;
            Donor *d = donor_find(app->donors, id);
            if (!d) { puts("No such donor."); continue; }
            if (c == 2) {
                donor_list(&(DonorNode){ *d, NULL });
                donation_history(app->donations, id);
            } else if (c == 3) {
                Donor u = *d;
                char tmp[NAME_LEN];
                input_line("New name (blank = keep): ", tmp, sizeof tmp);
                trim_in_place(tmp);
                if (tmp[0]) {
                    if (!validate_name(tmp, sizeof tmp)) { puts("Invalid name."); continue; }
                    snprintf(u.name, sizeof u.name, "%s", tmp);
                }
                char ct[CONTACT_LEN + 8];
                input_line("New contact (blank = keep): ", ct, sizeof ct);
                trim_in_place(ct);
                if (ct[0]) {
                    if (!validate_contact(ct)) { puts("Invalid contact."); continue; }
                    snprintf(u.contact, sizeof u.contact, "%.*s", (int)sizeof u.contact - 1, ct);
                }
                if (!donor_update(app->donors, &u)) puts("Update rejected.");
                else if (commit(app)) { puts("Donor updated."); audit(s, "DONOR_UPDATE", "id=%d", id); }
            } else if (c == 4) {
                if (donor_deactivate(app->donors, id) && commit(app)) {
                    puts("Donor deactivated.");
                    audit(s, "DONOR_DEACTIVATE", "id=%d", id);
                }
            }
        }
    }
}

/* ---------------- donations ---------------- */

static void donation_menu(AppData *app, const AdminSession *s)
{
    for (;;) {
        puts("\n-- Donations --\n1) Record donation  2) Donor history  3) List all  0) Back");
        int c;
        if (!input_int("> ", &c, 0, 3) || c == 0) return;
        if (c == 3) { report_donations(app->donations); continue; }
        int id;
        if (!input_int("Donor ID: ", &id, 1, 2000000000)) return;
        if (c == 2) { donation_history(app->donations, id); continue; }

        int ml, batch;
        char err[ERR_LEN];
        if (!input_int("Volume collected (ml): ", &ml, DONATION_MIN_ML, DONATION_MAX_ML)) return;
        if (!donation_register(app, id, date_today(), ml, &batch, err, sizeof err)) {
            printf("Rejected: %s\n", err);
            continue;
        }
        if (commit(app)) {
            printf("Donation recorded. Batch %d created and QUARANTINED pending screening.\n", batch);
            audit(s, "DONATION", "batch=%d", batch);
        }
    }
}

/* ---------------- inventory ---------------- */

static void inventory_menu(AppData *app, const AdminSession *s)
{
    for (;;) {
        sweep_expiry(app);
        puts("\n-- Inventory --\n1) Stock summary  2) List batches  3) Release batch after screening\n"
             "4) Reject/discard batch  5) Screening records  0) Back");
        int c, id;
        char err[ERR_LEN], text[SCREEN_REF_LEN];
        if (!input_int("> ", &c, 0, 5) || c == 0) return;
        if (c == 1) { report_inventory(app->batches, date_today()); continue; }
        if (c == 2) { inventory_print(app->batches); continue; }
        if (c == 5) { screening_print(app->screenings); continue; }
        if (!input_int("Batch ID: ", &id, 1, 2000000000)) return;
        if (c == 3) {
            if (!input_name("Screening / lab reference ID: ", text, sizeof text)) return;
            if (!input_confirm("Confirm ALL required screening is complete and PASSED (y/N): ")) { puts("Cancelled."); continue; }
            if (!screening_release_batch(app, id, s->admin_id, date_today(), text, err, sizeof err)) { printf("Rejected: %s\n", err); (void)commit(app); }
            else if (commit(app)) { puts("Batch released to available stock (decision recorded)."); audit(s, "BATCH_RELEASE", "batch=%d", id); }
        } else {
            if (!input_name("Reason (e.g. failed screening, damaged bag, recall): ", text, sizeof text)) return;
            if (!input_confirm("Reject this batch permanently (y/N): ")) { puts("Cancelled."); continue; }
            if (!screening_reject_batch(app, id, s->admin_id, date_today(), text, err, sizeof err)) printf("Rejected: %s\n", err);
            else if (commit(app)) { puts("Batch rejected (decision recorded)."); audit(s, "BATCH_REJECT", "batch=%d", id); }
        }
    }
}

/* ---------------- requests ---------------- */

static void request_menu(AppData *app, const AdminSession *s)
{
    for (;;) {
        sweep_expiry(app);
        puts("\n-- Blood requests --\n1) Create  2) Approve (reserve stock)  3) Reject  4) List  0) Back");
        int c, id;
        char err[ERR_LEN];
        if (!input_int("> ", &c, 0, 4) || c == 0) return;
        if (c == 4) { request_list(app->requests); continue; }
        if (c == 1) {
            BloodRequest r;
            memset(&r, 0, sizeof r);
            if (!input_name("Hospital: ", r.hospital, sizeof r.hospital)) return;
            if (!input_blood_group("Blood group: ", &r.blood_group)) return;
            if (!input_int("Quantity (ml): ", &r.quantity_ml, REQUEST_MIN_ML, REQUEST_MAX_ML)) return;
            r.request_id = request_next_id(app->requests);
            r.request_date = date_today();
            r.status = REQUEST_PENDING;
            if (!request_create(&app->requests, &r)) puts("Could not create request.");
            else if (commit(app)) { printf("Request %d created.\n", r.request_id); audit(s, "REQUEST_CREATE", "id=%d", r.request_id); }
            continue;
        }
        if (!input_int("Request ID: ", &id, 1, 2000000000)) return;
        if (c == 2) {
            if (!request_approve(app, id, date_today(), err, sizeof err)) { printf("Rejected: %s\n", err); (void)commit(app); }
            else if (commit(app)) { puts("Approved; stock reserved."); audit(s, "REQUEST_APPROVE", "id=%d", id); }
        } else {
            if (!request_reject(app, id, err, sizeof err)) printf("Rejected: %s\n", err);
            else if (commit(app)) { puts("Request rejected; any reservations released."); audit(s, "REQUEST_REJECT", "id=%d", id); }
        }
    }
}

/* ---------------- distribution ---------------- */

static void distribution_menu(AppData *app, const AdminSession *s)
{
    for (;;) {
        sweep_expiry(app);
        puts("\n-- Distribution --\n1) Issue approved request  2) Issue history  0) Back");
        int c, id;
        char err[ERR_LEN];
        if (!input_int("> ", &c, 0, 2) || c == 0) return;
        if (c == 2) { distribution_history(app->issues); continue; }
        if (!input_int("Request ID: ", &id, 1, 2000000000)) return;
        if (!distribution_issue(app, id, date_today(), err, sizeof err)) { printf("Rejected: %s\n", err); (void)commit(app); }
        else if (commit(app)) { puts("Blood issued; request fulfilled."); audit(s, "ISSUE", "request=%d", id); }
    }
}

/* ---------------- reports & admin ---------------- */

static void reports_menu(AppData *app)
{
    for (;;) {
        puts("\n-- Reports --\n1) Donors  2) Donations  3) Inventory  4) Requests  5) Issues\n"
             "6) Set low-stock threshold  0) Back");
        int c;
        if (!input_int("> ", &c, 0, 6) || c == 0) return;
        switch (c) {
        case 1: report_donors(app->donors); break;
        case 2: report_donations(app->donations); break;
        case 3: sweep_expiry(app); report_inventory(app->batches, date_today()); break;
        case 4: report_requests(app->requests); break;
        case 5: report_issues(app->issues); break;
        case 6: {
            BloodGroup g;
            int ml;
            if (!input_blood_group("Blood group: ", &g)) return;
            if (!input_int("Threshold (ml): ", &ml, 0, 1000000)) return;
            report_set_low_stock_threshold(g, ml);
            printf("Threshold for %s set to %d ml (this session only).\n", blood_group_str(g), ml);
            break;
        }
        }
    }
}

static void admin_menu(AppData *app, const AdminSession *s)
{
    for (;;) {
        puts("\n-- Administrators --\n1) List  2) Create  3) Deactivate\n"
             "4) Change my password  5) Reset another admin's password  0) Back");
        int c, id;
        if (!input_int("> ", &c, 0, 5) || c == 0) return;
        if (c == 1) admin_list_accounts(s, app->admins);
        else if (c == 2) (void)admin_create_account(s, &app->admins);
        else if (c == 4) (void)admin_change_password(s, app->admins);
        else {
            if (!input_int("Admin ID: ", &id, 1, 2000000000)) return;
            if (c == 3) (void)admin_deactivate_account(s, app->admins, id);
            else (void)admin_reset_password(s, app->admins, id);
        }
    }
}

void ui_run_menus(AppData *app, const AdminSession *s)
{
    if (!auth_is_authorized(s)) { puts("Not authorized."); return; }
    printf("\nWelcome, %s.\n", s->username);
    for (;;) {
        puts("\n===== Blood Bank Management =====\n"
             "1) Donors  2) Donations  3) Inventory  4) Requests\n"
             "5) Distribution  6) Reports  7) Administrators  0) Logout");
        int c;
        if (!input_int("> ", &c, 0, 7) || c == 0) return;
        switch (c) {
        case 1: donor_menu(app, s); break;
        case 2: donation_menu(app, s); break;
        case 3: inventory_menu(app, s); break;
        case 4: request_menu(app, s); break;
        case 5: distribution_menu(app, s); break;
        case 6: reports_menu(app); break;
        case 7: admin_menu(app, s); break;
        }
    }
}
