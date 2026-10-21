#ifndef REQUEST_H
#define REQUEST_H

#include "common.h"

typedef struct {
    int request_id;
    char hospital[HOSPITAL_LEN];
    BloodGroup blood_group;
    int quantity_ml;
    Date request_date;
    RequestStatus status;
} BloodRequest;

typedef struct RequestNode {
    BloodRequest data;
    struct RequestNode *next;
} RequestNode;

/* A reservation ties part of a batch to an approved request. */
typedef struct {
    int request_id;
    int batch_id;
    int quantity_ml;
} Reservation;

typedef struct ReservationNode {
    Reservation data;
    struct ReservationNode *next;
} ReservationNode;

int          request_next_id(const RequestNode *head);
bool         request_create(RequestNode **head, const BloodRequest *request);
RequestNode *request_find(RequestNode *head, int request_id);

/* Approval checks stock and creates reservations (earliest expiry first). */
bool request_approve(AppData *app, int request_id, Date today,
                     char *err, size_t err_cap);
/* Rejecting an approved request releases its reservations. */
bool request_reject(AppData *app, int request_id, char *err, size_t err_cap);

const char *request_status_str(RequestStatus s);
void request_list(const RequestNode *head);
void request_free_all(RequestNode **head);

bool reservation_add(ReservationNode **head, const Reservation *r);
int  reservation_total_for_request(const ReservationNode *head, int request_id);
void reservation_remove_for_request(ReservationNode **head, int request_id);
void reservation_free_all(ReservationNode **head);

#endif
