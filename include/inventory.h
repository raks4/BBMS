#ifndef INVENTORY_H
#define INVENTORY_H

#include "common.h"

/* available_ml = physical volume still in the batch.
 * reserved_ml  = part of it promised to approved requests.
 * issuable     = available_ml - reserved_ml. */
typedef struct {
    int batch_id;
    int donation_id;
    BloodGroup blood_group;
    Date collection_date;
    Date expiry_date;
    int available_ml;
    int reserved_ml;
    BatchStatus status;
} BloodBatch;

typedef struct BatchNode {
    BloodBatch data;
    struct BatchNode *next;
} BatchNode;

int        inventory_next_id(const BatchNode *head);
bool       inventory_add_batch(BatchNode **head, const BloodBatch *batch);
BatchNode *inventory_find(BatchNode *head, int batch_id);
int        inventory_free_ml(const BloodBatch *batch);

/* Counts only AVAILABLE, unexpired, unreserved volume. */
int  inventory_available_ml(const BatchNode *head, BloodGroup group, Date today);

bool inventory_reserve(BatchNode *head, int batch_id, int quantity_ml, Date today);
bool inventory_unreserve(BatchNode *head, int batch_id, int quantity_ml);
bool inventory_consume(BatchNode *head, int batch_id, int quantity_ml);

/* Marks expired batches; returns how many changed. */
int  inventory_release_expired(BatchNode *head, Date today);

const char *batch_status_str(BatchStatus s);
void inventory_print(const BatchNode *head);
void inventory_free_all(BatchNode **head);

#endif
