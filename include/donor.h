#ifndef DONOR_H
#define DONOR_H

#include "common.h"

typedef struct {
    int donor_id;
    char name[NAME_LEN];
    char contact[CONTACT_LEN];
    BloodGroup blood_group;
    Date date_of_birth;
    bool active;
} Donor;

typedef struct DonorNode {
    Donor data;
    struct DonorNode *next;
} DonorNode;

int    donor_next_id(const DonorNode *head);
bool   donor_add(DonorNode **head, const Donor *donor);
Donor *donor_find(DonorNode *head, int donor_id);
bool   donor_update(DonorNode *head, const Donor *updated);
bool   donor_deactivate(DonorNode *head, int donor_id);
void   donor_list(const DonorNode *head);
void   donor_free_all(DonorNode **head);

#endif
