#ifndef DISTRIBUTION_H
#define DISTRIBUTION_H

#include "common.h"

typedef struct {
    int issue_id;
    int request_id;
    int batch_id;
    int quantity_ml;
    Date issue_date;
} IssueRecord;

typedef struct IssueNode {
    IssueRecord data;
    struct IssueNode *next;
} IssueNode;

int  distribution_next_id(const IssueNode *head);
bool issue_add(IssueNode **head, const IssueRecord *rec);

/* Issues every reservation of an APPROVED request, all-or-nothing. Issue
 * records are append-only: there is no edit or delete function. */
bool distribution_issue(AppData *app, int request_id, Date today,
                        char *err, size_t err_cap);

void distribution_history(const IssueNode *head);
void distribution_free_all(IssueNode **head);

#endif
