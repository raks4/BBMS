#include "test_util.h"
#include "donor.h"
#include <string.h>

static Donor make(int id)
{
    Donor d;
    memset(&d, 0, sizeof d);
    d.donor_id = id;
    strcpy(d.name, "Test Donor");
    strcpy(d.contact, "5551234567");
    d.blood_group = BLOOD_O_NEG;
    d.date_of_birth = (Date){1990, 5, 20};
    d.active = true;
    return d;
}

int main(void)
{
    DonorNode *list = NULL;
    Donor d = make(1);
    CHECK(donor_add(&list, &d));
    CHECK(!donor_add(&list, &d));                       /* duplicate id */
    Donor bad = make(2);
    strcpy(bad.contact, "abc");
    CHECK(!donor_add(&list, &bad));
    Donor future = make(3);
    future.date_of_birth = (Date){2999, 1, 1};
    CHECK(!donor_add(&list, &future));
    CHECK(donor_next_id(list) == 2);
    CHECK(donor_find(list, 1) != NULL && donor_find(list, 9) == NULL);
    d.blood_group = BLOOD_A_POS;
    CHECK(donor_update(list, &d) && donor_find(list, 1)->blood_group == BLOOD_A_POS);
    CHECK(donor_deactivate(list, 1) && !donor_find(list, 1)->active);
    CHECK(!donor_deactivate(list, 42));
    donor_free_all(&list);
    CHECK(list == NULL);
    TEST_DONE();
}
