#include "test_util.h"
#include "app.h"
#include "validation.h"
#include <string.h>

int main(void)
{
    AppData app;
    memset(&app, 0, sizeof app);
    Donor d = { 1, "Test Donor", "5551234567", BLOOD_B_POS, {1990, 1, 1}, true };
    CHECK(donor_add(&app.donors, &d));
    Donor kid = { 2, "Young Donor", "5551234568", BLOOD_B_POS, {2015, 1, 1}, true };
    CHECK(donor_add(&app.donors, &kid));

    char err[ERR_LEN];
    int batch = 0;
    Date day1 = {2026, 1, 10};
    CHECK(donation_register(&app, 1, day1, 450, &batch, err, sizeof err));
    BatchNode *b = inventory_find(app.batches, batch);
    CHECK(b && b->data.status == BATCH_QUARANTINED && b->data.available_ml == 450);
    CHECK(date_diff_days(b->data.expiry_date, day1) == BATCH_SHELF_LIFE_DAYS);
    CHECK(inventory_available_ml(app.batches, BLOOD_B_POS, day1) == 0);   /* quarantined */

    CHECK(!donation_register(&app, 1, (Date){2026, 2, 1}, 450, &batch, err, sizeof err)); /* too soon */
    CHECK(donation_register(&app, 1, (Date){2026, 3, 10}, 450, &batch, err, sizeof err));
    CHECK(!donation_register(&app, 2, day1, 450, &batch, err, sizeof err));               /* under age */
    CHECK(!donation_register(&app, 99, day1, 450, &batch, err, sizeof err));              /* unknown */
    CHECK(!donation_register(&app, 1, (Date){2027, 1, 1}, 5000, &batch, err, sizeof err)); /* bad volume */

    donor_deactivate(app.donors, 1);
    CHECK(!donation_register(&app, 1, (Date){2027, 1, 1}, 450, &batch, err, sizeof err));  /* inactive */

    donation_free_all(&app.donations);
    inventory_free_all(&app.batches);
    donor_free_all(&app.donors);
    TEST_DONE();
}
