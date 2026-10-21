#include "screening.h"
#include "app.h"
#include "validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int screening_next_id(const ScreeningNode *h)
{
    int m = 0;
    for (; h; h = h->next) if (h->data.screening_id > m) m = h->data.screening_id;
    return m + 1;
}

bool screening_add(ScreeningNode **head, const ScreeningRecord *r)
{
    if (!head || !r || r->screening_id <= 0 || r->batch_id <= 0 || r->admin_id < 0 ||
        !validate_date(r->date) ||
        (r->outcome != SCREEN_PASSED && r->outcome != SCREEN_REJECTED) ||
        !validate_name(r->reference, SCREEN_REF_LEN))
        return false;
    for (ScreeningNode *n = *head; n; n = n->next)
        if (n->data.screening_id == r->screening_id) return false;
    ScreeningNode *node = calloc(1, sizeof *node);
    if (!node) return false;
    node->data = *r;
    ScreeningNode **pp = head;
    while (*pp) pp = &(*pp)->next;
    *pp = node;
    return true;
}

const ScreeningRecord *screening_latest(const ScreeningNode *h, int batch_id)
{
    const ScreeningRecord *last = NULL;
    for (; h; h = h->next) if (h->data.batch_id == batch_id) last = &h->data;
    return last;
}

/* Builds the record and appends it. The caller changes the batch status only
 * after this succeeds, so a failure here never leaves a half-done decision. */
static bool add_decision(AppData *app, const BloodBatch *b, int admin_id, Date today,
                         ScreeningOutcome outcome, const char *text, char *err, size_t cap)
{
    if (admin_id <= 0) { snprintf(err, cap, "Not authorized."); return false; }
    if (!validate_name(text, SCREEN_REF_LEN)) {
        snprintf(err, cap, "A reference/reason of 1-%d valid characters is required.", SCREEN_REF_LEN - 1);
        return false;
    }
    if (date_compare(today, b->collection_date) < 0) {
        snprintf(err, cap, "Decision date is before the collection date.");
        return false;
    }
    ScreeningRecord rec;
    memset(&rec, 0, sizeof rec);
    rec.screening_id = screening_next_id(app->screenings);
    rec.batch_id = b->batch_id;
    rec.admin_id = admin_id;
    rec.date = today;
    rec.outcome = outcome;
    snprintf(rec.reference, sizeof rec.reference, "%.*s", SCREEN_REF_LEN - 1, text);
    if (!screening_add(&app->screenings, &rec)) { snprintf(err, cap, "Could not record the decision."); return false; }
    return true;
}

bool screening_release_batch(AppData *app, int batch_id, int admin_id, Date today,
                             const char *reference, char *err, size_t cap)
{
    BatchNode *n = inventory_find(app->batches, batch_id);
    if (!n) { snprintf(err, cap, "Batch %d not found.", batch_id); return false; }
    if (n->data.status != BATCH_QUARANTINED) {
        snprintf(err, cap, "Batch %d is %s, not quarantined.", batch_id, batch_status_str(n->data.status));
        return false;
    }
    if (date_compare(n->data.expiry_date, today) < 0) {
        n->data.status = BATCH_EXPIRED;
        snprintf(err, cap, "Batch %d has already expired.", batch_id);
        return false;
    }
    if (!add_decision(app, &n->data, admin_id, today, SCREEN_PASSED, reference, err, cap)) return false;
    n->data.status = BATCH_AVAILABLE;
    return true;
}

bool screening_reject_batch(AppData *app, int batch_id, int admin_id, Date today,
                            const char *reason, char *err, size_t cap)
{
    BatchNode *n = inventory_find(app->batches, batch_id);
    if (!n) { snprintf(err, cap, "Batch %d not found.", batch_id); return false; }
    if (n->data.status == BATCH_DISCARDED) { snprintf(err, cap, "Batch %d is already discarded.", batch_id); return false; }
    if (n->data.reserved_ml > 0) {
        snprintf(err, cap, "Batch %d has reservations; reject those requests first.", batch_id);
        return false;
    }
    if (!add_decision(app, &n->data, admin_id, today, SCREEN_REJECTED, reason, err, cap)) return false;
    n->data.status = BATCH_DISCARDED;
    n->data.available_ml = 0;
    return true;
}

void screening_print(const ScreeningNode *h)
{
    printf("\n%-6s %-6s %-11s %-9s %-6s %s\n", "ID", "Batch", "Date", "Outcome", "Admin", "Reference / reason");
    if (!h) { puts("(no screening records)"); return; }
    for (; h; h = h->next) {
        char d[16];
        date_format(h->data.date, d, sizeof d);
        printf("%-6d %-6d %-11s %-9s %-6d %s\n", h->data.screening_id, h->data.batch_id, d,
               h->data.outcome == SCREEN_PASSED ? "PASSED" : "REJECTED",
               h->data.admin_id, h->data.reference);
    }
}

void screening_free_all(ScreeningNode **head)
{
    while (*head) { ScreeningNode *n = *head; *head = n->next; free(n); }
}
