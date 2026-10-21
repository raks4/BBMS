#ifndef REPORTS_H
#define REPORTS_H

#include "common.h"
#include "donor.h"
#include "donation.h"
#include "inventory.h"
#include "request.h"
#include "distribution.h"

void report_donors(const DonorNode *donors);
void report_donations(const DonationNode *donations);
void report_inventory(const BatchNode *batches, Date today);
void report_requests(const RequestNode *requests);
void report_issues(const IssueNode *issues);

/* Low-stock thresholds per blood group (kept in memory for the session). */
void report_set_low_stock_threshold(BloodGroup group, int ml);
int  report_get_low_stock_threshold(BloodGroup group);

#endif
