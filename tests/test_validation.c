#include "test_util.h"
#include "validation.h"
#include <string.h>

int main(void)
{
    CHECK(validate_username("alice_01"));
    CHECK(!validate_username("al"));
    CHECK(!validate_username("bad name"));
    CHECK(!validate_username("pipe|x"));

    CHECK(validate_password_policy("Str0ng!Passw0rd"));
    CHECK(!validate_password_policy("short1!A"));
    CHECK(!validate_password_policy("alllowercase1234!"));
    CHECK(!validate_password_policy("NoSymbolsHere1234"));

    CHECK(validate_contact("+919876543210"));
    CHECK(validate_contact("5551234567"));
    CHECK(!validate_contact("12345"));
    CHECK(!validate_contact("555-123-4567"));

    CHECK(validate_name("Mary O'Neil-Smith", 100));
    CHECK(!validate_name("", 100));
    CHECK(!validate_name("a|b", 100));
    CHECK(!validate_name("line\nbreak", 100));
    CHECK(!validate_name("toolong", 4));

    Date leap = {2024, 2, 29}, bad = {2023, 2, 29};
    CHECK(validate_date(leap));
    CHECK(!validate_date(bad));

    Date d = {2024, 12, 20};
    Date e = date_add_days(d, 35);
    CHECK(e.year == 2025 && e.month == 1 && e.day == 24);
    CHECK(date_diff_days(e, d) == 35);
    Date dob = {2000, 6, 15}, t1 = {2024, 6, 14}, t2 = {2024, 6, 15};
    CHECK(date_age_years(dob, t1) == 23 && date_age_years(dob, t2) == 24);

    Date p;
    CHECK(date_parse("2025-03-09", &p) && p.month == 3);
    CHECK(!date_parse("2025-13-09", &p));
    CHECK(!date_parse("25-3-9", &p));

    BloodGroup g;
    CHECK(blood_group_parse("ab-", &g) && g == BLOOD_AB_NEG);
    CHECK(!blood_group_parse("C+", &g));
    TEST_DONE();
}
