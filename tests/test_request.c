#include "test_util.h"
#include "app.h"
#include <string.h>

static void add_batch(AppData *a, int id, BloodGroup g, Date exp, int ml)
{
    BloodBatch b = { id, id, g, {2026, 4, 1}, exp, ml, 0, BATCH_AVAILABLE };
    CHECK(inventory_add_batch(&a->batches, &b));
}

int main(void)
{
    AppData app;
    memset(&app, 0, sizeof app);
    Date today = {2026, 5, 1};
    add_batch(&app, 1, BLOOD_O_NEG, (Date){2026, 5, 20}, 300);   /* expires later */
    add_batch(&app, 2, BLOOD_O_NEG, (Date){2026, 5, 10}, 300);   /* expires first */
    add_batch(&app, 3, BLOOD_A_POS, (Date){2026, 5, 10}, 500);

    BloodRequest r = { 1, "City Hospital", BLOOD_O_NEG, 400, today, REQUEST_PENDING };
    CHECK(request_create(&app.requests, &r));
    char err[ERR_LEN];
    CHECK(request_approve(&app, 1, today, err, sizeof err));
    CHECK(app.requests->data.status == REQUEST_APPROVED);
    CHECK(inventory_find(app.batches, 2)->data.reserved_ml == 300);   /* FEFO: batch 2 first */
    CHECK(inventory_find(app.batches, 1)->data.reserved_ml == 100);
    CHECK(reservation_total_for_request(app.reservations, 1) == 400);
    CHECK(!request_approve(&app, 1, today, err, sizeof err));          /* not pending */

    BloodRequest big = { 2, "City Hospital", BLOOD_O_NEG, 300, today, REQUEST_PENDING };
    CHECK(request_create(&app.requests, &big));
    CHECK(!request_approve(&app, 2, today, err, sizeof err));          /* only 200 left */
    CHECK(request_find(app.requests, 2)->data.status == REQUEST_PENDING);
    CHECK(reservation_total_for_request(app.reservations, 2) == 0);

    CHECK(request_reject(&app, 1, err, sizeof err));                   /* releases stock */
    CHECK(inventory_find(app.batches, 1)->data.reserved_ml == 0);
    CHECK(inventory_find(app.batches, 2)->data.reserved_ml == 0);
    CHECK(app.reservations == NULL);
    CHECK(!request_reject(&app, 1, err, sizeof err));                  /* already rejected */

    BloodRequest bad = { 3, "Bad|Name", BLOOD_O_NEG, 100, today, REQUEST_PENDING };
    CHECK(!request_create(&app.requests, &bad));

    request_free_all(&app.requests);
    reservation_free_all(&app.reservations);
    inventory_free_all(&app.batches);
    TEST_DONE();
}
