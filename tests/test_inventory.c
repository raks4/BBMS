#include "test_util.h"
#include "app.h"
#include <string.h>

int main(void)
{
    AppData app;
    memset(&app, 0, sizeof app);
    BatchNode *list = NULL;
    Date today = {2026, 5, 1};
    BloodBatch b = { 1, 1, BLOOD_A_POS, {2026, 4, 20}, {2026, 5, 25}, 450, 0, BATCH_QUARANTINED };
    CHECK(inventory_add_batch(&list, &b));
    CHECK(!inventory_add_batch(&list, &b));                          /* duplicate */
    CHECK(inventory_available_ml(list, BLOOD_A_POS, today) == 0);    /* quarantined */

    char err[ERR_LEN];
    app.batches = list;
    CHECK(screening_release_batch(&app, 1, 1, today, "LAB-1", err, sizeof err));
    CHECK(inventory_available_ml(list, BLOOD_A_POS, today) == 450);
    CHECK(inventory_reserve(list, 1, 300, today));
    CHECK(inventory_available_ml(list, BLOOD_A_POS, today) == 150);  /* reserved excluded */
    CHECK(!inventory_reserve(list, 1, 200, today));                  /* over-reserve */
    CHECK(inventory_unreserve(list, 1, 100));
    CHECK(inventory_consume(list, 1, 200));
    CHECK(list->data.available_ml == 250 && list->data.reserved_ml == 0);
    CHECK(!inventory_consume(list, 1, 1));                           /* nothing reserved */

    CHECK(inventory_release_expired(list, (Date){2026, 5, 25}) == 0);   /* last valid day */
    CHECK(inventory_release_expired(list, (Date){2026, 5, 26}) == 1);
    CHECK(inventory_available_ml(list, BLOOD_A_POS, today) == 0);
    CHECK(!inventory_reserve(list, 1, 10, today));
    inventory_free_all(&list);
    screening_free_all(&app.screenings);
    TEST_DONE();
}
