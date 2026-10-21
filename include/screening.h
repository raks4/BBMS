#ifndef SCREENING_H
#define SCREENING_H

#include "common.h"

#define SCREEN_REF_LEN 64

typedef enum { SCREEN_REJECTED = 0, SCREEN_PASSED = 1 } ScreeningOutcome;

/* Append-only audit record of a release or rejection decision on a batch.
 * admin_id 0 is reserved for records synthesized when migrating legacy data. */
typedef struct {
    int screening_id;
    int batch_id;
    int admin_id;
    Date date;
    ScreeningOutcome outcome;
    char reference[SCREEN_REF_LEN];     /* lab report id, or rejection reason */
} ScreeningRecord;

typedef struct ScreeningNode {
    ScreeningRecord data;
    struct ScreeningNode *next;
} ScreeningNode;

int  screening_next_id(const ScreeningNode *head);
bool screening_add(ScreeningNode **head, const ScreeningRecord *rec);

/* Most recent record for a batch, or NULL if it was never decided. */
const ScreeningRecord *screening_latest(const ScreeningNode *head, int batch_id);

/* QUARANTINED -> AVAILABLE, recording who/when/which lab reference. */
bool screening_release_batch(AppData *app, int batch_id, int admin_id, Date today,
                             const char *reference, char *err, size_t err_cap);

/* Any non-discarded batch with no reservations -> DISCARDED, with a reason. */
bool screening_reject_batch(AppData *app, int batch_id, int admin_id, Date today,
                            const char *reason, char *err, size_t err_cap);

void screening_print(const ScreeningNode *head);
void screening_free_all(ScreeningNode **head);

#endif
