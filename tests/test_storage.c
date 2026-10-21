#include "test_util.h"
#include "app.h"
#include "admin.h"
#include "storage.h"
#include "validation.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[64];

static void fresh(void)
{
    strcpy(root, "/tmp/bbms_test_XXXXXX");
    CHECK(mkdtemp(root) != NULL);
    CHECK(chdir(root) == 0);
    CHECK(storage_init());
}

static void drop(void)
{
    char cmd[128];
    CHECK(chdir("/") == 0);
    snprintf(cmd, sizeof cmd, "rm -rf %s", root);
    int rc = system(cmd);
    (void)rc;
}

static bool exists(const char *p) { FILE *f = fopen(p, "rb"); if (!f) return false; fclose(f); return true; }

static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) { fputs(text, f); fclose(f); }
}

/* donor + donation + released batch + approved request (300 of 450 ml reserved) */
static void sample(AppData *a)
{
    Date today = date_today();
    Donor d = { 1, "Sample Donor", "5551234567", BLOOD_O_NEG, {1990, 1, 1}, true };
    char err[ERR_LEN];
    int batch = 0;
    CHECK(donor_add(&a->donors, &d));
    CHECK(donation_register(a, 1, today, 450, &batch, err, sizeof err));
    CHECK(screening_release_batch(a, batch, 1, today, "LAB-1", err, sizeof err));
    BloodRequest r = { 1, "City Hospital", BLOOD_O_NEG, 300, today, REQUEST_PENDING };
    CHECK(request_create(&a->requests, &r));
    CHECK(request_approve(a, 1, today, err, sizeof err));
}

static void expect_load_fails(void)
{
    AppData a;
    memset(&a, 0, sizeof a);
    CHECK(!storage_load_all(&a));
    CHECK(a.donors == NULL && a.batches == NULL);        /* nothing half-loaded */
}

int main(void)
{
    /* --- round trip, generations, pruning --- */
    fresh();
    AppData a;
    memset(&a, 0, sizeof a);
    CHECK(storage_load_all(&a));
    sample(&a);
    CHECK(storage_save_all(&a));
    CHECK(exists("data/CURRENT") && exists("data/snapshots/00000001/screenings.dat"));
    storage_cleanup_all(&a);

    AppData b;
    memset(&b, 0, sizeof b);
    CHECK(storage_load_all(&b));
    CHECK(b.donors && b.screenings && b.reservations);
    CHECK(b.batches->data.status == BATCH_AVAILABLE && b.batches->data.reserved_ml == 300);
    CHECK(b.requests->data.status == REQUEST_APPROVED);
    for (int i = 0; i < 5; i++) CHECK(storage_save_all(&b));          /* generations 2..6 */
    CHECK(!exists("data/snapshots/00000001/batches.dat") && !exists("data/snapshots/00000003/batches.dat"));
    CHECK(exists("data/snapshots/00000004/batches.dat") && exists("data/snapshots/00000006/batches.dat"));

    /* --- a crash mid-save must not affect the committed state --- */
    storage_cleanup_all(&b);
    CHECK(mkdir("data/snapshots/00000007", 0700) == 0);          /* half-written new snapshot */
    write_text("data/snapshots/00000007/donors.dat", "garbage that was never committed\n");
    AppData c;
    memset(&c, 0, sizeof c);
    CHECK(storage_load_all(&c));                                 /* still loads generation 6 */
    CHECK(c.requests && c.requests->data.status == REQUEST_APPROVED);
    CHECK(storage_save_all(&c));                                 /* cleans leftovers, commits 7 */
    FILE *f = fopen("data/snapshots/00000007/donors.dat", "rb");
    char first[16] = "";
    CHECK(f != NULL);
    if (f) { CHECK(fgets(first, sizeof first, f) != NULL); fclose(f); }
    CHECK(strncmp(first, "BBMS1", 5) == 0);
    storage_cleanup_all(&c);

    /* --- damaged pointer / missing pointer fail safe --- */
    write_text("data/CURRENT", "abc\n");
    expect_load_fails();
    CHECK(remove("data/CURRENT") == 0);
    expect_load_fails();                                         /* snapshots exist, CURRENT gone */
    write_text("data/CURRENT", "7\n");
    AppData d;
    memset(&d, 0, sizeof d);
    CHECK(storage_load_all(&d));
    storage_cleanup_all(&d);

    /* --- tampering is detected by the cross-file checks --- */
    write_text("data/snapshots/00000007/reservations.dat", "BBMS1\n");   /* reserved_ml no longer adds up */
    expect_load_fails();
    drop();

    /* --- an AVAILABLE batch must be backed by a PASSED screening record --- */
    fresh();
    memset(&a, 0, sizeof a);
    CHECK(storage_load_all(&a));
    sample(&a);
    CHECK(storage_save_all(&a));
    storage_cleanup_all(&a);
    write_text("data/snapshots/00000001/screenings.dat", "BBMS1\n");
    expect_load_fails();
    drop();

    /* --- migration from the earlier flat-file layout --- */
    fresh();
    write_text("data/donors.dat", "BBMS1\n1|Old Donor|5551234567|6|1990-01-01|1\n");
    write_text("data/donations.dat", "BBMS1\n1|1|2026-01-01|450\n");
    write_text("data/batches.dat", "BBMS1\n1|1|6|2026-01-01|2026-02-05|450|0|1\n");
    memset(&a, 0, sizeof a);
    CHECK(storage_load_all(&a));
    const ScreeningRecord *lr = a.screenings ? screening_latest(a.screenings, 1) : NULL;
    CHECK(lr && lr->admin_id == 0 && lr->outcome == SCREEN_PASSED);   /* labelled legacy record */
    CHECK(storage_save_all(&a));
    CHECK(exists("data/CURRENT") && !exists("data/donors.dat") && !exists("data/batches.dat"));
    storage_cleanup_all(&a);
    memset(&a, 0, sizeof a);
    CHECK(storage_load_all(&a) && a.batches && a.screenings);
    storage_cleanup_all(&a);
    drop();

    /* --- admins file round trip --- */
    fresh();
    AdminAccount acc;
    memset(&acc, 0, sizeof acc);
    acc.admin_id = 1;
    strcpy(acc.username, "root_admin");
    acc.kdf_iterations = KDF_ITERATIONS_DEFAULT;
    acc.status = STATUS_ACTIVE;
    CHECK(security_hash_password("Str0ng!Passw0rd", acc.salt, acc.password_hash, acc.kdf_iterations));
    AdminNode *admins = NULL;
    CHECK(admin_list_add(&admins, &acc));
    CHECK(storage_save_admins(admins));
    admin_free_all(&admins);
    memset(&a, 0, sizeof a);
    CHECK(storage_load_all(&a));
    CHECK(a.admins && auth_verify_password(&a.admins->data, "Str0ng!Passw0rd"));
    storage_cleanup_all(&a);
    drop();

    TEST_DONE();
}
