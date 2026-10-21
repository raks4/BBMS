#include "test_util.h"
#include "app.h"
#include <string.h>

int main(void)
{
    AppData app;
    memset(&app, 0, sizeof app);
    Date today = {2026, 5, 1};
    BloodBatch b1 = { 1, 1, BLOOD_B_NEG, {2026, 4, 1}, {2026, 5, 10}, 300, 0, BATCH_AVAILABLE };
    BloodBatch b2 = { 2, 2, BLOOD_B_NEG, {2026, 4, 5}, {2026, 5, 20}, 300, 0, BATCH_AVAILABLE };
    CHECK(inventory_add_batch(&app.batches, &b1));
    CHECK(inventory_add_batch(&app.batches, &b2));
    BloodRequest r = { 1, "General Hospital", BLOOD_B_NEG, 400, today, REQUEST_PENDING };
    CHECK(request_create(&app.requests, &r));

    char err[ERR_LEN];
    CHECK(!distribution_issue(&app, 1, today, err, sizeof err));       /* not approved yet */
    CHECK(request_approve(&app, 1, today, err, sizeof err));

    /* Expired reservation must block issuing and change nothing. */
    CHECK(!distribution_issue(&app, 1, (Date){2026, 5, 15}, err, sizeof err));
    CHECK(app.issues == NULL && app.requests->data.status == REQUEST_APPROVED);
    CHECK(inventory_find(app.batches, 2)->data.reserved_ml == 100);

    /* Rebuild the scenario with a fresh app for the happy path. */
    AppData ok;
    memset(&ok, 0, sizeof ok);
    b1.status = b2.status = BATCH_AVAILABLE;
    CHECK(inventory_add_batch(&ok.batches, &b1));
    CHECK(inventory_add_batch(&ok.batches, &b2));
    CHECK(request_create(&ok.requests, &r));
    CHECK(request_approve(&ok, 1, today, err, sizeof err));
    CHECK(distribution_issue(&ok, 1, today, err, sizeof err));
    CHECK(ok.requests->data.status == REQUEST_FULFILLED);
    CHECK(ok.reservations == NULL);
    CHECK(ok.issues && ok.issues->next && ok.issues->next->next == NULL);
    CHECK(ok.issues->data.quantity_ml + ok.issues->next->data.quantity_ml == 400);
    CHECK(inventory_find(ok.batches, 1)->data.available_ml == 0);
    CHECK(inventory_find(ok.batches, 2)->data.available_ml == 200);
    CHECK(!distribution_issue(&ok, 1, today, err, sizeof err));        /* cannot issue twice */

    AppData *all[2] = { &app, &ok };
    for (int i = 0; i < 2; i++) {
        request_free_all(&all[i]->requests);
        reservation_free_all(&all[i]->reservations);
        inventory_free_all(&all[i]->batches);
        distribution_free_all(&all[i]->issues);
    }
    TEST_DONE();
}
