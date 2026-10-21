#include "inventory.h"
#include "validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int inventory_next_id(const BatchNode *h)
{
    int m = 0;
    for (; h; h = h->next) if (h->data.batch_id > m) m = h->data.batch_id;
    return m + 1;
}

BatchNode *inventory_find(BatchNode *h, int id)
{
    for (; h; h = h->next) if (h->data.batch_id == id) return h;
    return NULL;
}

int inventory_free_ml(const BloodBatch *b) { return b->available_ml - b->reserved_ml; }

bool inventory_add_batch(BatchNode **head, const BloodBatch *b)
{
    if (!head || !b || b->batch_id <= 0 || b->donation_id <= 0 ||
        !validate_blood_group(b->blood_group) ||
        !validate_date(b->collection_date) || !validate_date(b->expiry_date) ||
        date_compare(b->expiry_date, b->collection_date) < 0 ||
        b->available_ml < 0 || b->reserved_ml < 0 || b->reserved_ml > b->available_ml ||
        (int)b->status < BATCH_QUARANTINED || (int)b->status > BATCH_DISCARDED)
        return false;
    if (inventory_find(*head, b->batch_id)) return false;
    BatchNode *node = calloc(1, sizeof *node);
    if (!node) return false;
    node->data = *b;
    BatchNode **pp = head;
    while (*pp) pp = &(*pp)->next;
    *pp = node;
    return true;
}

int inventory_available_ml(const BatchNode *h, BloodGroup g, Date today)
{
    int total = 0;
    for (; h; h = h->next) {
        const BloodBatch *b = &h->data;
        if (b->blood_group == g && b->status == BATCH_AVAILABLE &&
            date_compare(b->expiry_date, today) >= 0)
            total += inventory_free_ml(b);
    }
    return total;
}

bool inventory_reserve(BatchNode *head, int id, int ml, Date today)
{
    BatchNode *n = inventory_find(head, id);
    if (!n || ml <= 0 || n->data.status != BATCH_AVAILABLE ||
        date_compare(n->data.expiry_date, today) < 0 ||
        ml > inventory_free_ml(&n->data))
        return false;
    n->data.reserved_ml += ml;
    return true;
}

bool inventory_unreserve(BatchNode *head, int id, int ml)
{
    BatchNode *n = inventory_find(head, id);
    if (!n || ml <= 0 || ml > n->data.reserved_ml) return false;
    n->data.reserved_ml -= ml;
    return true;
}

bool inventory_consume(BatchNode *head, int id, int ml)
{
    BatchNode *n = inventory_find(head, id);
    if (!n || ml <= 0 || ml > n->data.reserved_ml) return false;
    n->data.reserved_ml -= ml;
    n->data.available_ml -= ml;
    return true;
}

int inventory_release_expired(BatchNode *h, Date today)
{
    int changed = 0;
    for (; h; h = h->next)
        if ((h->data.status == BATCH_AVAILABLE || h->data.status == BATCH_QUARANTINED) &&
            date_compare(h->data.expiry_date, today) < 0) {
            h->data.status = BATCH_EXPIRED;
            changed++;
        }
    return changed;
}

const char *batch_status_str(BatchStatus s)
{
    switch (s) {
    case BATCH_QUARANTINED: return "quarantined";
    case BATCH_AVAILABLE:   return "available";
    case BATCH_EXPIRED:     return "expired";
    case BATCH_DISCARDED:   return "discarded";
    }
    return "?";
}

void inventory_print(const BatchNode *h)
{
    printf("\n%-6s %-6s %-6s %-11s %-11s %-9s %-9s %s\n", "Batch", "Don.", "Group",
           "Collected", "Expires", "In stock", "Reserved", "Status");
    if (!h) { puts("(no batches)"); return; }
    for (; h; h = h->next) {
        char c[16], e[16];
        date_format(h->data.collection_date, c, sizeof c);
        date_format(h->data.expiry_date, e, sizeof e);
        printf("%-6d %-6d %-6s %-11s %-11s %-9d %-9d %s\n", h->data.batch_id,
               h->data.donation_id, blood_group_str(h->data.blood_group), c, e,
               h->data.available_ml, h->data.reserved_ml, batch_status_str(h->data.status));
    }
}

void inventory_free_all(BatchNode **head)
{
    while (*head) { BatchNode *n = *head; *head = n->next; free(n); }
}
