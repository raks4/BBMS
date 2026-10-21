#include "request.h"
#include "app.h"
#include "validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- reservations ---------------- */

bool reservation_add(ReservationNode **head, const Reservation *r)
{
    if (!head || !r || r->request_id <= 0 || r->batch_id <= 0 || r->quantity_ml <= 0) return false;
    ReservationNode *node = calloc(1, sizeof *node);
    if (!node) return false;
    node->data = *r;
    ReservationNode **pp = head;
    while (*pp) pp = &(*pp)->next;
    *pp = node;
    return true;
}

int reservation_total_for_request(const ReservationNode *h, int id)
{
    int t = 0;
    for (; h; h = h->next) if (h->data.request_id == id) t += h->data.quantity_ml;
    return t;
}

void reservation_remove_for_request(ReservationNode **head, int id)
{
    ReservationNode **pp = head;
    while (*pp) {
        if ((*pp)->data.request_id == id) {
            ReservationNode *dead = *pp;
            *pp = dead->next;
            free(dead);
        } else {
            pp = &(*pp)->next;
        }
    }
}

void reservation_free_all(ReservationNode **head)
{
    while (*head) { ReservationNode *n = *head; *head = n->next; free(n); }
}

/* ---------------- requests ---------------- */

int request_next_id(const RequestNode *h)
{
    int m = 0;
    for (; h; h = h->next) if (h->data.request_id > m) m = h->data.request_id;
    return m + 1;
}

RequestNode *request_find(RequestNode *h, int id)
{
    for (; h; h = h->next) if (h->data.request_id == id) return h;
    return NULL;
}

bool request_create(RequestNode **head, const BloodRequest *r)
{
    if (!head || !r || r->request_id <= 0 || !validate_name(r->hospital, HOSPITAL_LEN) ||
        !validate_blood_group(r->blood_group) ||
        r->quantity_ml < REQUEST_MIN_ML || r->quantity_ml > REQUEST_MAX_ML ||
        !validate_date(r->request_date) ||
        (int)r->status < REQUEST_PENDING || (int)r->status > REQUEST_FULFILLED)
        return false;
    if (request_find(*head, r->request_id)) return false;
    RequestNode *node = calloc(1, sizeof *node);
    if (!node) return false;
    node->data = *r;
    RequestNode **pp = head;
    while (*pp) pp = &(*pp)->next;
    *pp = node;
    return true;
}

/* Undo every reservation held by a request. */
static void release_reservations(AppData *app, int request_id)
{
    for (ReservationNode *n = app->reservations; n; n = n->next)
        if (n->data.request_id == request_id)
            (void)inventory_unreserve(app->batches, n->data.batch_id, n->data.quantity_ml);
    reservation_remove_for_request(&app->reservations, request_id);
}

/* First-expiry-first-out: the usable batch that expires soonest. */
static BatchNode *pick_fefo(BatchNode *h, BloodGroup g, Date today)
{
    BatchNode *best = NULL;
    for (; h; h = h->next) {
        const BloodBatch *b = &h->data;
        if (b->blood_group != g || b->status != BATCH_AVAILABLE ||
            date_compare(b->expiry_date, today) < 0 || inventory_free_ml(b) <= 0)
            continue;
        if (!best || date_compare(b->expiry_date, best->data.expiry_date) < 0)
            best = h;
    }
    return best;
}

bool request_approve(AppData *app, int id, Date today, char *err, size_t cap)
{
    RequestNode *r = request_find(app->requests, id);
    if (!r) { snprintf(err, cap, "Request %d not found.", id); return false; }
    if (r->data.status != REQUEST_PENDING) {
        snprintf(err, cap, "Request %d is %s, not pending.", id, request_status_str(r->data.status));
        return false;
    }
    inventory_release_expired(app->batches, today);
    int avail = inventory_available_ml(app->batches, r->data.blood_group, today);
    if (avail < r->data.quantity_ml) {
        snprintf(err, cap, "Insufficient %s stock: %d ml available, %d ml requested.",
                 blood_group_str(r->data.blood_group), avail, r->data.quantity_ml);
        return false;
    }
    int remaining = r->data.quantity_ml;
    while (remaining > 0) {
        BatchNode *b = pick_fefo(app->batches, r->data.blood_group, today);
        if (!b) { release_reservations(app, id); snprintf(err, cap, "Stock changed during approval."); return false; }
        int take = inventory_free_ml(&b->data);
        if (take > remaining) take = remaining;
        if (!inventory_reserve(app->batches, b->data.batch_id, take, today)) {
            release_reservations(app, id);
            snprintf(err, cap, "Reservation failed.");
            return false;
        }
        Reservation rv = { id, b->data.batch_id, take };
        if (!reservation_add(&app->reservations, &rv)) {
            (void)inventory_unreserve(app->batches, rv.batch_id, take);
            release_reservations(app, id);
            snprintf(err, cap, "Out of memory.");
            return false;
        }
        remaining -= take;
    }
    r->data.status = REQUEST_APPROVED;
    return true;
}

bool request_reject(AppData *app, int id, char *err, size_t cap)
{
    RequestNode *r = request_find(app->requests, id);
    if (!r) { snprintf(err, cap, "Request %d not found.", id); return false; }
    if (r->data.status != REQUEST_PENDING && r->data.status != REQUEST_APPROVED) {
        snprintf(err, cap, "Request %d is %s and cannot be rejected.", id, request_status_str(r->data.status));
        return false;
    }
    release_reservations(app, id);
    r->data.status = REQUEST_REJECTED;
    return true;
}

const char *request_status_str(RequestStatus s)
{
    switch (s) {
    case REQUEST_PENDING:   return "pending";
    case REQUEST_APPROVED:  return "approved";
    case REQUEST_REJECTED:  return "rejected";
    case REQUEST_FULFILLED: return "fulfilled";
    }
    return "?";
}

void request_list(const RequestNode *h)
{
    printf("\n%-5s %-28s %-6s %-8s %-11s %s\n", "ID", "Hospital", "Group", "ml", "Date", "Status");
    if (!h) { puts("(no requests)"); return; }
    for (; h; h = h->next) {
        char d[16];
        date_format(h->data.request_date, d, sizeof d);
        printf("%-5d %-28.28s %-6s %-8d %-11s %s\n", h->data.request_id, h->data.hospital,
               blood_group_str(h->data.blood_group), h->data.quantity_ml, d,
               request_status_str(h->data.status));
    }
}

void request_free_all(RequestNode **head)
{
    while (*head) { RequestNode *n = *head; *head = n->next; free(n); }
}
