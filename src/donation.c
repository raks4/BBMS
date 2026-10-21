#include "donation.h"
#include "app.h"
#include "validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int donation_next_id(const DonationNode *h)
{
    int m = 0;
    for (; h; h = h->next) if (h->data.donation_id > m) m = h->data.donation_id;
    return m + 1;
}

bool donation_record(DonationNode **head, const Donation *d)
{
    if (!head || !d || d->donation_id <= 0 || d->donor_id <= 0 ||
        !validate_date(d->donation_date) ||
        d->quantity_ml < DONATION_MIN_ML || d->quantity_ml > DONATION_MAX_ML)
        return false;
    for (DonationNode *n = *head; n; n = n->next)
        if (n->data.donation_id == d->donation_id) return false;
    DonationNode *node = calloc(1, sizeof *node);
    if (!node) return false;
    node->data = *d;
    DonationNode **pp = head;
    while (*pp) pp = &(*pp)->next;
    *pp = node;
    return true;
}

bool donation_last_date(const DonationNode *h, int donor_id, Date *out)
{
    bool found = false;
    for (; h; h = h->next) {
        if (h->data.donor_id != donor_id) continue;
        if (!found || date_compare(h->data.donation_date, *out) > 0) *out = h->data.donation_date;
        found = true;
    }
    return found;
}

void donation_history(const DonationNode *h, int donor_id)
{
    printf("\n%-8s %-11s %s\n", "Don. ID", "Date", "ml");
    bool any = false;
    for (; h; h = h->next) {
        if (h->data.donor_id != donor_id) continue;
        char d[16];
        date_format(h->data.donation_date, d, sizeof d);
        printf("%-8d %-11s %d\n", h->data.donation_id, d, h->data.quantity_ml);
        any = true;
    }
    if (!any) puts("(no donations)");
}

void donation_free_all(DonationNode **head)
{
    while (*head) { DonationNode *n = *head; *head = n->next; free(n); }
}

bool donation_check_eligibility(const AppData *app, int donor_id, Date today,
                                char *err, size_t cap)
{
    Donor *d = donor_find(app->donors, donor_id);
    if (!d) { snprintf(err, cap, "Donor %d not found.", donor_id); return false; }
    if (!d->active) { snprintf(err, cap, "Donor is inactive."); return false; }
    int age = date_age_years(d->date_of_birth, today);
    if (age < DONOR_MIN_AGE || age > DONOR_MAX_AGE) {
        snprintf(err, cap, "Donor age %d is outside %d-%d.", age, DONOR_MIN_AGE, DONOR_MAX_AGE);
        return false;
    }
    Date last;
    if (donation_last_date(app->donations, donor_id, &last)) {
        long gap = date_diff_days(today, last);
        if (gap < DONATION_INTERVAL_DAYS) {
            snprintf(err, cap, "Last donation was %ld day(s) ago; %d days required.",
                     gap, DONATION_INTERVAL_DAYS);
            return false;
        }
    }
    return true;
}

bool donation_register(AppData *app, int donor_id, Date today, int qty,
                       int *batch_id_out, char *err, size_t cap)
{
    if (!donation_check_eligibility(app, donor_id, today, err, cap)) return false;
    if (qty < DONATION_MIN_ML || qty > DONATION_MAX_ML) {
        snprintf(err, cap, "Quantity must be %d-%d ml.", DONATION_MIN_ML, DONATION_MAX_ML);
        return false;
    }
    Donor *d = donor_find(app->donors, donor_id);

    Donation don = { donation_next_id(app->donations), donor_id, today, qty };
    if (!donation_record(&app->donations, &don)) { snprintf(err, cap, "Could not record donation."); return false; }

    BloodBatch b;
    memset(&b, 0, sizeof b);
    b.batch_id = inventory_next_id(app->batches);
    b.donation_id = don.donation_id;
    b.blood_group = d->blood_group;
    b.collection_date = today;
    b.expiry_date = date_add_days(today, BATCH_SHELF_LIFE_DAYS);
    b.available_ml = qty;
    b.status = BATCH_QUARANTINED;           /* not issuable until screening passes */
    if (!inventory_add_batch(&app->batches, &b)) {
        /* undo: the donation we just appended is the tail node */
        DonationNode **pp = &app->donations;
        while ((*pp)->next) pp = &(*pp)->next;
        free(*pp);
        *pp = NULL;
        snprintf(err, cap, "Could not create batch.");
        return false;
    }
    if (batch_id_out) *batch_id_out = b.batch_id;
    return true;
}
