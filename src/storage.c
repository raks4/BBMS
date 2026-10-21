#include "storage.h"
#include "admin.h"
#include "screening.h"
#include "validation.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#define FILE_MAGIC      "BBMS1"
#define LINE_MAX_LEN    512
#define MARKER_FILE     DATA_DIR "/.setup_complete"
#define CURRENT_FILE    DATA_DIR "/CURRENT"
#define SNAP_ROOT       DATA_DIR "/snapshots"
#define ADMINS_FILE     DATA_DIR "/admins.dat"
#define SNAPSHOTS_KEPT  3
#define MAX_GENERATION  99999999UL

static const char *const SNAP_FILES[] = {
    "donors.dat", "donations.dat", "batches.dat", "requests.dat",
    "reservations.dat", "issues.dat", "screenings.dat"
};
#define SNAP_FILE_COUNT (sizeof SNAP_FILES / sizeof SNAP_FILES[0])

static unsigned long g_gen = 0;      /* committed generation (0 = none yet) */
static bool g_legacy = true;         /* true until a CURRENT pointer exists */

/* ------------------------------------------------------------------ */
/* Filesystem helpers                                                  */
/* ------------------------------------------------------------------ */

static bool make_dir(const char *p)
{
#ifdef _WIN32
    return _mkdir(p) == 0 || errno == EEXIST;
#else
    return mkdir(p, 0700) == 0 || errno == EEXIST;
#endif
}

static void remove_dir(const char *p)
{
#ifdef _WIN32
    (void)_rmdir(p);
#else
    (void)rmdir(p);
#endif
}

/* Best effort: persists directory entries (new files / renames) on POSIX. */
static void sync_dir(const char *path)
{
#ifndef _WIN32
    int fd = open(path, O_RDONLY);
    if (fd >= 0) { (void)fsync(fd); close(fd); }
#else
    (void)path;
#endif
}

static bool dir_has_entries(const char *path)
{
#ifndef _WIN32
    DIR *d = opendir(path);
    if (!d) return false;
    bool any = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0) { any = true; break; }
    closedir(d);
    return any;
#else
    (void)path;
    return false;       /* not checked on Windows */
#endif
}

static void snap_path(unsigned long gen, char *out, size_t cap)
{
    if (gen == 0) snprintf(out, cap, "%s", DATA_DIR);
    else snprintf(out, cap, "%s/%08lu", SNAP_ROOT, gen);
}

static void remove_snapshot_dir(const char *dir)
{
    char p[320];
    for (size_t i = 0; i < SNAP_FILE_COUNT; i++) {
        snprintf(p, sizeof p, "%s/%s", dir, SNAP_FILES[i]);
        (void)remove(p);
    }
    remove_dir(dir);
}

bool storage_init(void)
{
    return make_dir(DATA_DIR) && make_dir(SNAP_ROOT);
}

bool storage_setup_marker_exists(void)
{
    FILE *f = fopen(MARKER_FILE, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

bool storage_write_setup_marker(void)
{
    FILE *f = fopen(MARKER_FILE, "wb");
    if (!f) return false;
    fputs("initialized\n", f);
    bool ok = fclose(f) == 0;
    sync_dir(DATA_DIR);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Reading and writing single files                                    */
/* ------------------------------------------------------------------ */

typedef bool (*ParseFn)(char *line, void *ctx);
typedef bool (*WriteFn)(FILE *f, const void *ctx);

/* `required` false: a missing file is an empty table (fresh/legacy install). */
static bool load_file(const char *dir, const char *name, bool required, ParseFn parse, void *ctx)
{
    char path[320], line[LINE_MAX_LEN];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (errno == ENOENT && !required) return true;
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    unsigned long ln = 0;
    bool ok = true;
    while (ok && fgets(line, sizeof line, f)) {
        ln++;
        size_t n = strlen(line);
        if (n == 0 || line[n - 1] != '\n') {
            if (!feof(f)) { ok = false; break; }          /* line too long */
        }
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (ln == 1) { ok = strcmp(line, FILE_MAGIC) == 0; continue; }
        if (n == 0) continue;
        ok = parse(line, ctx);
    }
    if (ln == 0) ok = false;                              /* empty file: no header */
    if (ferror(f)) ok = false;
    fclose(f);
    if (!ok) fprintf(stderr, "Corrupt or invalid data in %s (line %lu).\n", path, ln);
    return ok;
}

/* Writes and fsyncs one file in place (used for not-yet-visible snapshots). */
static bool write_synced(const char *path, WriteFn writer, const void *ctx)
{
    FILE *f = fopen(path, "wb");
    if (!f) return false;
#ifndef _WIN32
    (void)chmod(path, 0600);
#endif
    bool ok = fprintf(f, "%s\n", FILE_MAGIC) > 0 && writer(f, ctx) && fflush(f) == 0;
#ifdef _WIN32
    ok = ok && _commit(_fileno(f)) == 0;
#else
    ok = ok && fsync(fileno(f)) == 0;
#endif
    if (fclose(f) != 0) ok = false;
    return ok;
}

/* tmp file + fsync + rename: atomically replaces `path`. */
static bool replace_atomic(const char *path, WriteFn writer, const void *ctx, const char *dir)
{
    char tmp[330];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    bool ok = write_synced(tmp, writer, ctx);
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(tmp, path) == 0;
#endif
    }
    if (!ok) (void)remove(tmp);
    else sync_dir(dir);
    return ok;
}

static int split(char *line, char **f, int max)
{
    int n = 0;
    f[n++] = line;
    for (char *p = line; *p; p++) {
        if (*p == '|') {
            if (n == max) return -1;
            *p = '\0';
            f[n++] = p + 1;
        }
    }
    return n;
}

static bool parse_int(const char *s, int *out, long lo, long hi)
{
    if (!s || !*s) return false;
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || errno != 0 || v < lo || v > hi) return false;
    *out = (int)v;
    return true;
}

static bool copy_str(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) return false;
    memcpy(dst, src, n + 1);
    return true;
}

static void hex_out(FILE *f, const unsigned char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) fprintf(f, "%02x", b[i]);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hex_in(const char *s, unsigned char *out, size_t n)
{
    if (strlen(s) != n * 2) return false;
    for (size_t i = 0; i < n; i++) {
        int hi = hexval(s[2 * i]), lo = hexval(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (unsigned char)(hi * 16 + lo);
    }
    return true;
}

static void put_date(FILE *f, Date d) { fprintf(f, "%04d-%02d-%02d", d.year, d.month, d.day); }

/* ------------------------------------------------------------------ */
/* Record formats                                                      */
/* ------------------------------------------------------------------ */

/* admins: id|username|salt|hash|iterations|status */
static bool parse_admin(char *line, void *ctx)
{
    char *f[6];
    AdminAccount a;
    int id, iters, st;
    memset(&a, 0, sizeof a);
    if (split(line, f, 6) != 6) return false;
    if (!parse_int(f[0], &id, 1, INT_MAX) || !copy_str(a.username, sizeof a.username, f[1]) ||
        !hex_in(f[2], a.salt, SALT_SIZE) || !hex_in(f[3], a.password_hash, HASH_SIZE) ||
        !parse_int(f[4], &iters, KDF_ITERATIONS_MIN, KDF_ITERATIONS_MAX) ||
        !parse_int(f[5], &st, 0, 1))
        return false;
    a.admin_id = id;
    a.kdf_iterations = (unsigned int)iters;
    a.status = (AccountStatus)st;
    return admin_list_add((AdminNode **)ctx, &a);
}

static bool write_admins(FILE *f, const void *ctx)
{
    for (const AdminNode *n = ctx; n; n = n->next) {
        const AdminAccount *a = &n->data;
        fprintf(f, "%d|%s|", a->admin_id, a->username);
        hex_out(f, a->salt, SALT_SIZE);
        fputc('|', f);
        hex_out(f, a->password_hash, HASH_SIZE);
        fprintf(f, "|%u|%d\n", a->kdf_iterations, (int)a->status);
    }
    return !ferror(f);
}

/* donors: id|name|contact|blood|dob|active */
static bool parse_donor(char *line, void *ctx)
{
    char *f[6];
    Donor d;
    int id, bg, active;
    memset(&d, 0, sizeof d);
    if (split(line, f, 6) != 6) return false;
    if (!parse_int(f[0], &id, 1, INT_MAX) || !copy_str(d.name, sizeof d.name, f[1]) ||
        !copy_str(d.contact, sizeof d.contact, f[2]) ||
        !parse_int(f[3], &bg, 0, BLOOD_GROUP_COUNT - 1) ||
        !date_parse(f[4], &d.date_of_birth) || !parse_int(f[5], &active, 0, 1))
        return false;
    d.donor_id = id;
    d.blood_group = (BloodGroup)bg;
    d.active = active != 0;
    return donor_add((DonorNode **)ctx, &d);
}

static bool write_donors(FILE *f, const void *ctx)
{
    for (const DonorNode *n = ctx; n; n = n->next) {
        const Donor *d = &n->data;
        fprintf(f, "%d|%s|%s|%d|", d->donor_id, d->name, d->contact, (int)d->blood_group);
        put_date(f, d->date_of_birth);
        fprintf(f, "|%d\n", d->active ? 1 : 0);
    }
    return !ferror(f);
}

/* donations: id|donor_id|date|ml */
static bool parse_donation(char *line, void *ctx)
{
    char *f[4];
    Donation d;
    memset(&d, 0, sizeof d);
    if (split(line, f, 4) != 4) return false;
    if (!parse_int(f[0], &d.donation_id, 1, INT_MAX) || !parse_int(f[1], &d.donor_id, 1, INT_MAX) ||
        !date_parse(f[2], &d.donation_date) || !parse_int(f[3], &d.quantity_ml, 1, INT_MAX))
        return false;
    return donation_record((DonationNode **)ctx, &d);
}

static bool write_donations(FILE *f, const void *ctx)
{
    for (const DonationNode *n = ctx; n; n = n->next) {
        fprintf(f, "%d|%d|", n->data.donation_id, n->data.donor_id);
        put_date(f, n->data.donation_date);
        fprintf(f, "|%d\n", n->data.quantity_ml);
    }
    return !ferror(f);
}

/* batches: id|donation|blood|collected|expiry|available|reserved|status */
static bool parse_batch(char *line, void *ctx)
{
    char *f[8];
    BloodBatch b;
    int bg, st;
    memset(&b, 0, sizeof b);
    if (split(line, f, 8) != 8) return false;
    if (!parse_int(f[0], &b.batch_id, 1, INT_MAX) || !parse_int(f[1], &b.donation_id, 1, INT_MAX) ||
        !parse_int(f[2], &bg, 0, BLOOD_GROUP_COUNT - 1) ||
        !date_parse(f[3], &b.collection_date) || !date_parse(f[4], &b.expiry_date) ||
        !parse_int(f[5], &b.available_ml, 0, INT_MAX) ||
        !parse_int(f[6], &b.reserved_ml, 0, INT_MAX) ||
        !parse_int(f[7], &st, BATCH_QUARANTINED, BATCH_DISCARDED))
        return false;
    b.blood_group = (BloodGroup)bg;
    b.status = (BatchStatus)st;
    return inventory_add_batch((BatchNode **)ctx, &b);
}

static bool write_batches(FILE *f, const void *ctx)
{
    for (const BatchNode *n = ctx; n; n = n->next) {
        const BloodBatch *b = &n->data;
        fprintf(f, "%d|%d|%d|", b->batch_id, b->donation_id, (int)b->blood_group);
        put_date(f, b->collection_date);
        fputc('|', f);
        put_date(f, b->expiry_date);
        fprintf(f, "|%d|%d|%d\n", b->available_ml, b->reserved_ml, (int)b->status);
    }
    return !ferror(f);
}

/* requests: id|hospital|blood|ml|date|status */
static bool parse_request(char *line, void *ctx)
{
    char *f[6];
    BloodRequest r;
    int bg, st;
    memset(&r, 0, sizeof r);
    if (split(line, f, 6) != 6) return false;
    if (!parse_int(f[0], &r.request_id, 1, INT_MAX) || !copy_str(r.hospital, sizeof r.hospital, f[1]) ||
        !parse_int(f[2], &bg, 0, BLOOD_GROUP_COUNT - 1) ||
        !parse_int(f[3], &r.quantity_ml, 1, INT_MAX) || !date_parse(f[4], &r.request_date) ||
        !parse_int(f[5], &st, REQUEST_PENDING, REQUEST_FULFILLED))
        return false;
    r.blood_group = (BloodGroup)bg;
    r.status = (RequestStatus)st;
    return request_create((RequestNode **)ctx, &r);
}

static bool write_requests(FILE *f, const void *ctx)
{
    for (const RequestNode *n = ctx; n; n = n->next) {
        const BloodRequest *r = &n->data;
        fprintf(f, "%d|%s|%d|%d|", r->request_id, r->hospital, (int)r->blood_group, r->quantity_ml);
        put_date(f, r->request_date);
        fprintf(f, "|%d\n", (int)r->status);
    }
    return !ferror(f);
}

/* reservations: request_id|batch_id|ml */
static bool parse_reservation(char *line, void *ctx)
{
    char *f[3];
    Reservation r;
    if (split(line, f, 3) != 3) return false;
    if (!parse_int(f[0], &r.request_id, 1, INT_MAX) || !parse_int(f[1], &r.batch_id, 1, INT_MAX) ||
        !parse_int(f[2], &r.quantity_ml, 1, INT_MAX))
        return false;
    return reservation_add((ReservationNode **)ctx, &r);
}

static bool write_reservations(FILE *f, const void *ctx)
{
    for (const ReservationNode *n = ctx; n; n = n->next)
        fprintf(f, "%d|%d|%d\n", n->data.request_id, n->data.batch_id, n->data.quantity_ml);
    return !ferror(f);
}

/* issues: id|request|batch|ml|date */
static bool parse_issue(char *line, void *ctx)
{
    char *f[5];
    IssueRecord r;
    memset(&r, 0, sizeof r);
    if (split(line, f, 5) != 5) return false;
    if (!parse_int(f[0], &r.issue_id, 1, INT_MAX) || !parse_int(f[1], &r.request_id, 1, INT_MAX) ||
        !parse_int(f[2], &r.batch_id, 1, INT_MAX) || !parse_int(f[3], &r.quantity_ml, 1, INT_MAX) ||
        !date_parse(f[4], &r.issue_date))
        return false;
    return issue_add((IssueNode **)ctx, &r);
}

static bool write_issues(FILE *f, const void *ctx)
{
    for (const IssueNode *n = ctx; n; n = n->next) {
        fprintf(f, "%d|%d|%d|%d|", n->data.issue_id, n->data.request_id, n->data.batch_id,
                n->data.quantity_ml);
        put_date(f, n->data.issue_date);
        fputc('\n', f);
    }
    return !ferror(f);
}

/* screenings: id|batch|admin|date|outcome|reference */
static bool parse_screening(char *line, void *ctx)
{
    char *f[6];
    ScreeningRecord r;
    int outcome;
    memset(&r, 0, sizeof r);
    if (split(line, f, 6) != 6) return false;
    if (!parse_int(f[0], &r.screening_id, 1, INT_MAX) || !parse_int(f[1], &r.batch_id, 1, INT_MAX) ||
        !parse_int(f[2], &r.admin_id, 0, INT_MAX) || !date_parse(f[3], &r.date) ||
        !parse_int(f[4], &outcome, 0, 1) || !copy_str(r.reference, sizeof r.reference, f[5]))
        return false;
    r.outcome = (ScreeningOutcome)outcome;
    return screening_add((ScreeningNode **)ctx, &r);
}

static bool write_screenings(FILE *f, const void *ctx)
{
    for (const ScreeningNode *n = ctx; n; n = n->next) {
        const ScreeningRecord *r = &n->data;
        fprintf(f, "%d|%d|%d|", r->screening_id, r->batch_id, r->admin_id);
        put_date(f, r->date);
        fprintf(f, "|%d|%s\n", (int)r->outcome, r->reference);
    }
    return !ferror(f);
}

/* ------------------------------------------------------------------ */
/* Admins (separate file, atomically replaced on its own)              */
/* ------------------------------------------------------------------ */

bool storage_load_admins(AdminNode **head)
{
    return load_file(DATA_DIR, "admins.dat", false, parse_admin, head);
}

bool storage_save_admins(const AdminNode *head)
{
    return replace_atomic(ADMINS_FILE, write_admins, head, DATA_DIR);
}

/* ------------------------------------------------------------------ */
/* Snapshots                                                           */
/* ------------------------------------------------------------------ */

/* 1 = CURRENT found, 0 = absent, -1 = unreadable/corrupt. */
static int read_current(unsigned long *gen)
{
    FILE *f = fopen(CURRENT_FILE, "rb");
    if (!f) return errno == ENOENT ? 0 : -1;
    char buf[32];
    if (!fgets(buf, sizeof buf, f)) { fclose(f); return -1; }
    fclose(f);
    char *end;
    errno = 0;
    unsigned long v = strtoul(buf, &end, 10);
    if (end == buf || (*end != '\n' && *end != '\0') || errno != 0 || v == 0 || v > MAX_GENERATION)
        return -1;
    *gen = v;
    return 1;
}

static void write_current_body(FILE *f, unsigned long gen) { fprintf(f, "%lu\n", gen); }

/* Atomically points CURRENT at `gen`. Returns false only if the rename did
 * NOT happen, so the caller knows whether the old state is still live. */
static bool commit_current(unsigned long gen)
{
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%s.tmp", CURRENT_FILE);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    write_current_body(f, gen);
    bool ok = fflush(f) == 0;
#ifdef _WIN32
    ok = ok && _commit(_fileno(f)) == 0;
#else
    ok = ok && fsync(fileno(f)) == 0;
#endif
    if (fclose(f) != 0) ok = false;
    if (ok) {
#ifdef _WIN32
        ok = MoveFileExA(tmp, CURRENT_FILE, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = rename(tmp, CURRENT_FILE) == 0;               /* <- the commit point */
#endif
    }
    if (!ok) { (void)remove(tmp); return false; }
    sync_dir(DATA_DIR);
    return true;
}

static bool write_named(const char *dir, const char *name, WriteFn w, const void *ctx)
{
    char path[320];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    return write_synced(path, w, ctx);
}

bool storage_save_all(const AppData *a)
{
    if (g_gen >= MAX_GENERATION) return false;
    unsigned long next = g_gen + 1;
    char dir[64];
    snap_path(next, dir, sizeof dir);

    remove_snapshot_dir(dir);                    /* leftovers from a crashed save */
    if (!make_dir(dir)) return false;

    bool ok = write_named(dir, "donors.dat", write_donors, a->donors) &&
              write_named(dir, "donations.dat", write_donations, a->donations) &&
              write_named(dir, "batches.dat", write_batches, a->batches) &&
              write_named(dir, "requests.dat", write_requests, a->requests) &&
              write_named(dir, "reservations.dat", write_reservations, a->reservations) &&
              write_named(dir, "issues.dat", write_issues, a->issues) &&
              write_named(dir, "screenings.dat", write_screenings, a->screenings);
    if (ok) { sync_dir(dir); sync_dir(SNAP_ROOT); }
    if (!ok || !commit_current(next)) {          /* old snapshot is still the live one */
        remove_snapshot_dir(dir);
        return false;
    }

    /* Committed. Everything below is best-effort tidying. */
    g_gen = next;
    if (g_legacy) {                              /* migrated from the flat layout */
        for (size_t i = 0; i < SNAP_FILE_COUNT; i++) {
            char p[320];
            snprintf(p, sizeof p, "%s/%s", DATA_DIR, SNAP_FILES[i]);
            (void)remove(p);
        }
        g_legacy = false;
    }
    if (next > SNAPSHOTS_KEPT) {
        char old[64];
        snap_path(next - SNAPSHOTS_KEPT, old, sizeof old);
        remove_snapshot_dir(old);
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* Whole-database load                                                 */
/* ------------------------------------------------------------------ */

void storage_cleanup_all(AppData *a)
{
    admin_free_all(&a->admins);
    donor_free_all(&a->donors);
    donation_free_all(&a->donations);
    inventory_free_all(&a->batches);
    request_free_all(&a->requests);
    reservation_free_all(&a->reservations);
    distribution_free_all(&a->issues);
    screening_free_all(&a->screenings);
}

static bool resolve_layout(void)
{
    unsigned long gen = 0;
    int rc = read_current(&gen);
    if (rc < 0) {
        fprintf(stderr, "%s is unreadable or corrupt. Restore it with the number of the newest\n"
                        "directory under %s/ (after checking that directory is intact).\n",
                CURRENT_FILE, SNAP_ROOT);
        return false;
    }
    if (rc == 1) { g_gen = gen; g_legacy = false; return true; }
    if (dir_has_entries(SNAP_ROOT)) {
        fprintf(stderr, "%s is missing but %s/ contains snapshots. Refusing to start with\n"
                        "empty data; restore CURRENT (newest snapshot number) to recover.\n",
                CURRENT_FILE, SNAP_ROOT);
        return false;
    }
    g_gen = 0;
    g_legacy = true;            /* fresh install, or the earlier flat-file layout */
    return true;
}

/* Legacy data predates screening records: give released/discarded batches a
 * clearly labelled, admin-less record so the integrity rules still hold. */
static bool synthesize_legacy_screenings(AppData *a)
{
    for (BatchNode *b = a->batches; b; b = b->next) {
        if (screening_latest(a->screenings, b->data.batch_id)) continue;
        if (b->data.status != BATCH_AVAILABLE && b->data.status != BATCH_DISCARDED) continue;
        ScreeningRecord r;
        memset(&r, 0, sizeof r);
        r.screening_id = screening_next_id(a->screenings);
        r.batch_id = b->data.batch_id;
        r.admin_id = 0;
        r.date = b->data.collection_date;
        r.outcome = b->data.status == BATCH_AVAILABLE ? SCREEN_PASSED : SCREEN_REJECTED;
        snprintf(r.reference, sizeof r.reference, "LEGACY-IMPORT");
        if (!screening_add(&a->screenings, &r)) return false;
    }
    return true;
}

/* Cross-file integrity: every reference must resolve and totals must agree. */
static bool validate_relationships(AppData *a)
{
    for (DonationNode *n = a->donations; n; n = n->next)
        if (!donor_find(a->donors, n->data.donor_id)) return false;

    for (BatchNode *n = a->batches; n; n = n->next) {
        bool found = false;
        for (DonationNode *d = a->donations; d; d = d->next)
            if (d->data.donation_id == n->data.donation_id) { found = true; break; }
        if (!found) return false;

        int sum = 0;
        for (ReservationNode *r = a->reservations; r; r = r->next)
            if (r->data.batch_id == n->data.batch_id) sum += r->data.quantity_ml;
        if (sum != n->data.reserved_ml) return false;

        /* A batch's status must be backed by its latest screening decision. */
        const ScreeningRecord *s = screening_latest(a->screenings, n->data.batch_id);
        switch (n->data.status) {
        case BATCH_QUARANTINED: if (s) return false; break;
        case BATCH_AVAILABLE:   if (!s || s->outcome != SCREEN_PASSED) return false; break;
        case BATCH_EXPIRED:     if (s && s->outcome != SCREEN_PASSED) return false; break;
        case BATCH_DISCARDED:   if (!s || s->outcome != SCREEN_REJECTED) return false; break;
        }
    }

    for (ScreeningNode *s = a->screenings; s; s = s->next) {
        BatchNode *b = inventory_find(a->batches, s->data.batch_id);
        if (!b || date_compare(s->data.date, b->data.collection_date) < 0) return false;
    }
    for (ReservationNode *r = a->reservations; r; r = r->next) {
        RequestNode *q = request_find(a->requests, r->data.request_id);
        if (!q || q->data.status != REQUEST_APPROVED) return false;
        if (!inventory_find(a->batches, r->data.batch_id)) return false;
    }
    for (RequestNode *q = a->requests; q; q = q->next) {
        int total = reservation_total_for_request(a->reservations, q->data.request_id);
        if (q->data.status == REQUEST_APPROVED ? total != q->data.quantity_ml : total != 0)
            return false;
    }
    for (IssueNode *i = a->issues; i; i = i->next)
        if (!request_find(a->requests, i->data.request_id) ||
            !inventory_find(a->batches, i->data.batch_id))
            return false;
    return true;
}

bool storage_load_all(AppData *a)
{
    if (!resolve_layout()) return false;
    char dir[64];
    snap_path(g_gen, dir, sizeof dir);
    bool req = !g_legacy;        /* inside a committed snapshot every file must exist */

    bool ok = storage_load_admins(&a->admins) &&
              load_file(dir, "donors.dat", req, parse_donor, &a->donors) &&
              load_file(dir, "donations.dat", req, parse_donation, &a->donations) &&
              load_file(dir, "batches.dat", req, parse_batch, &a->batches) &&
              load_file(dir, "requests.dat", req, parse_request, &a->requests) &&
              load_file(dir, "reservations.dat", req, parse_reservation, &a->reservations) &&
              load_file(dir, "issues.dat", req, parse_issue, &a->issues) &&
              load_file(dir, "screenings.dat", req, parse_screening, &a->screenings);
    if (ok && g_legacy) ok = synthesize_legacy_screenings(a);
    if (ok && !validate_relationships(a)) {
        fprintf(stderr, "Data files are inconsistent with each other.\n");
        ok = false;
    }
    if (!ok) storage_cleanup_all(a);
    return ok;
}
