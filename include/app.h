#ifndef APP_H
#define APP_H

#include "common.h"
#include "auth.h"
#include "donor.h"
#include "donation.h"
#include "inventory.h"
#include "request.h"
#include "distribution.h"
#include "screening.h"

struct AppData {
    AdminNode       *admins;
    DonorNode       *donors;
    DonationNode    *donations;
    BatchNode       *batches;
    RequestNode     *requests;
    ReservationNode *reservations;
    IssueNode       *issues;
    ScreeningNode   *screenings;
};

#endif
