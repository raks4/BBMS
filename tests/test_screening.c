#include "test_util.h"
#include "app.h"
#include <string.h>

static void add(AppData *a, int id, BatchStatus st, Date exp)
{
    BloodBatch b = { id, id, BLOOD_A_POS, {2026, 4, 1}, exp, 450, 0, st };
    CHECK(inventory_add_batch(&a->batches, &b));
}

int main(void)
{
    AppData app;
    memset(&app, 0, sizeof app);
    Date today = {2026, 4, 10};
    add(&app, 1, BATCH_QUARANTINED, (Date){2026, 5, 6});
    add(&app, 2, BATCH_QUARANTINED, (Date){2026, 4, 5});     /* already expired */
    add(&app, 3, BATCH_AVAILABLE, (Date){2026, 5, 6});
    char err[ERR_LEN];

    CHECK(!screening_release_batch(&app, 1, 0, today, "LAB-1", err, sizeof err));      /* no admin */
    CHECK(!screening_release_batch(&app, 1, 1, today, "bad|ref", err, sizeof err));    /* invalid text */
    CHECK(!screening_release_batch(&app, 1, 1, today, "", err, sizeof err));           /* empty */
    CHECK(!screening_release_batch(&app, 1, 1, (Date){2026, 3, 1}, "LAB-1", err, sizeof err)); /* pre-collection */
    CHECK(app.screenings == NULL && app.batches->data.status == BATCH_QUARANTINED);    /* nothing changed */

    CHECK(screening_release_batch(&app, 1, 7, today, "LAB-2026-001", err, sizeof err));
    CHECK(app.batches->data.status == BATCH_AVAILABLE);
    const ScreeningRecord *r = screening_latest(app.screenings, 1);
    CHECK(r && r->admin_id == 7 && r->outcome == SCREEN_PASSED && strcmp(r->reference, "LAB-2026-001") == 0);
    CHECK(!screening_release_batch(&app, 1, 7, today, "LAB-X", err, sizeof err));      /* not quarantined */

    CHECK(!screening_release_batch(&app, 2, 7, today, "LAB-3", err, sizeof err));      /* expired */
    CHECK(inventory_find(app.batches, 2)->data.status == BATCH_EXPIRED);
    CHECK(screening_latest(app.screenings, 2) == NULL);

    inventory_find(app.batches, 3)->data.reserved_ml = 100;
    CHECK(!screening_reject_batch(&app, 3, 7, today, "recall", err, sizeof err));      /* reserved */
    inventory_find(app.batches, 3)->data.reserved_ml = 0;
    CHECK(screening_reject_batch(&app, 3, 7, today, "recall notice 12", err, sizeof err));
    BloodBatch *b3 = &inventory_find(app.batches, 3)->data;
    CHECK(b3->status == BATCH_DISCARDED && b3->available_ml == 0);
    CHECK(screening_latest(app.screenings, 3)->outcome == SCREEN_REJECTED);
    CHECK(!screening_reject_batch(&app, 3, 7, today, "again", err, sizeof err));       /* already discarded */

    /* history is append-only and ids are unique */
    CHECK(screening_next_id(app.screenings) == 3);
    ScreeningRecord dup = *screening_latest(app.screenings, 1);
    CHECK(!screening_add(&app.screenings, &dup));

    inventory_free_all(&app.batches);
    screening_free_all(&app.screenings);
    TEST_DONE();
}
