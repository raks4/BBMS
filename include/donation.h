#ifndef DONATION_H
#define DONATION_H

#include "common.h"
#include "donor.h"

typedef struct {
    int donation_id;
    int donor_id;
    Date donation_date;
    int quantity_ml;
} Donation;

typedef struct DonationNode {
    Donation data;
    struct DonationNode *next;
} DonationNode;

int  donation_next_id(const DonationNode *head);
bool donation_record(DonationNode **head, const Donation *donation);
bool donation_last_date(const DonationNode *head, int donor_id, Date *out);
void donation_history(const DonationNode *head, int donor_id);
void donation_free_all(DonationNode **head);

/* Business rules (age, interval, active donor). */
bool donation_check_eligibility(const AppData *app, int donor_id, Date today,
                                char *err, size_t err_cap);

/* Records the donation AND creates a QUARANTINED batch for it. */
bool donation_register(AppData *app, int donor_id, Date today, int quantity_ml,
                       int *batch_id_out, char *err, size_t err_cap);

#endif
