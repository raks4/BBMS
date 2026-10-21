#include "donor.h"
#include "validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool donor_valid(const Donor *d)
{
    return d->donor_id > 0 && validate_name(d->name, NAME_LEN) &&
           validate_contact(d->contact) && validate_blood_group(d->blood_group) &&
           validate_date(d->date_of_birth) &&
           date_compare(d->date_of_birth, date_today()) < 0;
}

int donor_next_id(const DonorNode *h)
{
    int m = 0;
    for (; h; h = h->next) if (h->data.donor_id > m) m = h->data.donor_id;
    return m + 1;
}

Donor *donor_find(DonorNode *h, int id)
{
    for (; h; h = h->next) if (h->data.donor_id == id) return &h->data;
    return NULL;
}

bool donor_add(DonorNode **head, const Donor *d)
{
    if (!head || !d || !donor_valid(d) || donor_find(*head, d->donor_id)) return false;
    DonorNode *node = calloc(1, sizeof *node);
    if (!node) return false;
    node->data = *d;
    DonorNode **pp = head;
    while (*pp) pp = &(*pp)->next;
    *pp = node;
    return true;
}

bool donor_update(DonorNode *head, const Donor *u)
{
    if (!u || !donor_valid(u)) return false;
    Donor *d = donor_find(head, u->donor_id);
    if (!d) return false;
    *d = *u;
    return true;
}

bool donor_deactivate(DonorNode *head, int id)
{
    Donor *d = donor_find(head, id);
    if (!d) return false;
    d->active = false;
    return true;
}

void donor_list(const DonorNode *h)
{
    printf("\n%-5s %-28s %-17s %-6s %-11s %s\n", "ID", "Name", "Contact", "Group", "DOB", "Status");
    if (!h) { puts("(no donors)"); return; }
    for (; h; h = h->next) {
        char dob[16];
        date_format(h->data.date_of_birth, dob, sizeof dob);
        printf("%-5d %-28.28s %-17s %-6s %-11s %s\n", h->data.donor_id, h->data.name,
               h->data.contact, blood_group_str(h->data.blood_group), dob,
               h->data.active ? "active" : "inactive");
    }
}

void donor_free_all(DonorNode **head)
{
    while (*head) { DonorNode *n = *head; *head = n->next; free(n); }
}
