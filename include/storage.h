#ifndef STORAGE_H
#define STORAGE_H

#include "app.h"

/*
 * On-disk layout
 *   data/CURRENT                  generation number of the committed snapshot
 *   data/snapshots/<gen>/        complete copy of all business data
 *   data/admins.dat               credentials (independent of business data)
 *   data/audit.log, data/.setup_complete
 *
 * storage_save_all() writes a NEW snapshot directory, flushes it, and then
 * atomically replaces CURRENT. That rename is the single commit point, so a
 * crash leaves either the complete old state or the complete new state.
 */

bool storage_init(void);                   /* creates data/ if needed */
bool storage_setup_marker_exists(void);
bool storage_write_setup_marker(void);

bool storage_load_admins(AdminNode **head);
bool storage_save_admins(const AdminNode *head);

/* Loads admins + the committed snapshot and validates cross-file integrity.
 * Data written by the earlier flat-file layout is migrated on the next save. */
bool storage_load_all(AppData *app);
bool storage_save_all(const AppData *app);   /* business data; admins excluded */
void storage_cleanup_all(AppData *app);

#endif
