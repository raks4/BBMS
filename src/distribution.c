#include "distribution.h"
#include "app.h"
#include "validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int distribution_next_id(const IssueNode *h)
{
    int m = 0;
    for (; h; h = h->next) if (h->data.issue_id > m) m = h->data.issue_id;
    return m + 1;
}

static void append_node(IssueNode **head, IssueNode *node)
{
    IssueNode **pp = head;
    while (*pp) pp = &(*pp)->next;
    node->next = NULL;
    *pp = node;
}

bool issue_add(IssueNode **head, const IssueRecord *r)
{
    if (!head || !r || r->issue_id <= 0 || r->request_id <= 0 || r->batch_id <= 0 ||
        r->quantity_ml <= 0 || !validate_date(r->issue_date))
        return false;
    for (IssueNode *n = *head; n; n = n->next)
        if (n->data.issue_id == r->issue_id) return false;
    IssueNode *node = calloc(1, sizeof *node);
    if (!node) return false;
    node->data = *r;
    append_node(head, node);
    return true;
}

bool distribution_issue(AppData *app, int request_id, Date today, char *err, size_t cap)
{
    RequestNode *req = request_find(app->requests, request_id);
    if (!req) { snprintf(err, cap, "Request %d not found.", request_id); return false; }
    if (req->data.status != REQUEST_APPROVED) {
        snprintf(err, cap, "Request %d is %s; only approved requests can be issued.",
                 request_id, request_status_str(req->data.status));
        return false;
    }
    inventory_release_expired(app->batches, today);

    /* Phase 1: validate everything before touching any state. */
    int count = 0;
    for (ReservationNode *r = app->reservations; r; r = r->next) {
        if (r->data.request_id != request_id) continue;
        BatchNode *b = inventory_find(app->batches, r->data.batch_id);
        if (!b || b->data.status != BATCH_AVAILABLE ||
            date_compare(b->data.expiry_date, today) < 0 ||
            b->data.reserved_ml < r->data.quantity_ml) {
            snprintf(err, cap, "Batch %d is no longer usable; reject this request and create a new one.",
                     r->data.batch_id);
            return false;
        }
        count++;
    }
    if (count == 0) { snprintf(err, cap, "Request has no reservations."); return false; }

    /* Phase 2: allocate every record up front so nothing can fail midway. */
    IssueNode **nodes = calloc((size_t)count, sizeof *nodes);
    if (!nodes) { snprintf(err, cap, "Out of memory."); return false; }
    for (int i = 0; i < count; i++) {
        nodes[i] = calloc(1, sizeof **nodes);
        if (!nodes[i]) {
            for (int j = 0; j < i; j++) free(nodes[j]);
            free(nodes);
            snprintf(err, cap, "Out of memory.");
            return false;
        }
    }

    /* Phase 3: commit. */
    int next = distribution_next_id(app->issues), i = 0;
    for (ReservationNode *r = app->reservations; r; r = r->next) {
        if (r->data.request_id != request_id) continue;
        IssueNode *node = nodes[i++];
        node->data.issue_id = next++;
        node->data.request_id = request_id;
        node->data.batch_id = r->data.batch_id;
        node->data.quantity_ml = r->data.quantity_ml;
        node->data.issue_date = today;
        (void)inventory_consume(app->batches, r->data.batch_id, r->data.quantity_ml);
        append_node(&app->issues, node);
    }
    free(nodes);
    reservation_remove_for_request(&app->reservations, request_id);
    req->data.status = REQUEST_FULFILLED;
    return true;
}

void distribution_history(const IssueNode *h)
{
    printf("\n%-7s %-8s %-7s %-8s %s\n", "Issue", "Request", "Batch", "ml", "Date");
    if (!h) { puts("(no issues)"); return; }
    for (; h; h = h->next) {
        char d[16];
        date_format(h->data.issue_date, d, sizeof d);
        printf("%-7d %-8d %-7d %-8d %s\n", h->data.issue_id, h->data.request_id,
               h->data.batch_id, h->data.quantity_ml, d);
    }
}

void distribution_free_all(IssueNode **head)
{
    while (*head) { IssueNode *n = *head; *head = n->next; free(n); }
}
