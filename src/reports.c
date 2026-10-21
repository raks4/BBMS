#include "reports.h"
#include "validation.h"

#include <stdio.h>

static int thresholds[BLOOD_GROUP_COUNT];
static bool thresholds_init;

static void init_thresholds(void)
{
    if (thresholds_init) return;
    for (int i = 0; i < BLOOD_GROUP_COUNT; i++) thresholds[i] = DEFAULT_LOW_STOCK_ML;
    thresholds_init = true;
}

void report_set_low_stock_threshold(BloodGroup g, int ml)
{
    init_thresholds();
    if (validate_blood_group(g) && ml >= 0) thresholds[g] = ml;
}

int report_get_low_stock_threshold(BloodGroup g)
{
    init_thresholds();
    return validate_blood_group(g) ? thresholds[g] : 0;
}

void report_donors(const DonorNode *d)
{
    int total = 0, active = 0;
    for (const DonorNode *n = d; n; n = n->next) { total++; if (n->data.active) active++; }
    donor_list(d);
    printf("\nTotal donors: %d (active: %d)\n", total, active);
}

void report_donations(const DonationNode *h)
{
    int count = 0;
    long ml = 0;
    printf("\n%-8s %-7s %-11s %s\n", "Don. ID", "Donor", "Date", "ml");
    for (; h; h = h->next) {
        char d[16];
        date_format(h->data.donation_date, d, sizeof d);
        printf("%-8d %-7d %-11s %d\n", h->data.donation_id, h->data.donor_id, d, h->data.quantity_ml);
        count++;
        ml += h->data.quantity_ml;
    }
    printf("\nTotal donations: %d, total volume: %ld ml\n", count, ml);
}

void report_inventory(const BatchNode *batches, Date today)
{
    init_thresholds();
    printf("\nStock summary as of %04d-%02d-%02d\n", today.year, today.month, today.day);
    printf("%-6s %-10s %-10s %-12s %-12s %s\n", "Group", "Issuable", "Reserved",
           "Quarantined", "Expiring<=7d", "Alert");
    for (int g = 0; g < BLOOD_GROUP_COUNT; g++) {
        int issuable = 0, reserved = 0, quarantine = 0, expiring = 0;
        for (const BatchNode *n = batches; n; n = n->next) {
            const BloodBatch *b = &n->data;
            if (b->blood_group != (BloodGroup)g || date_compare(b->expiry_date, today) < 0) continue;
            if (b->status == BATCH_QUARANTINED) quarantine += b->available_ml;
            if (b->status == BATCH_AVAILABLE) {
                issuable += inventory_free_ml(b);
                reserved += b->reserved_ml;
                if (date_diff_days(b->expiry_date, today) <= EXPIRY_WARNING_DAYS)
                    expiring += inventory_free_ml(b);
            }
        }
        printf("%-6s %-10d %-10d %-12d %-12d %s\n", blood_group_str((BloodGroup)g),
               issuable, reserved, quarantine, expiring,
               issuable < thresholds[g] ? "LOW STOCK" : "");
    }
    puts("\nExpired and discarded batches are excluded. Volumes are in ml.");
}

void report_requests(const RequestNode *r) { request_list(r); }
void report_issues(const IssueNode *i) { distribution_history(i); }
