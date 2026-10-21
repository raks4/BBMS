# Data layout and file format

## Directory layout

    data/
      CURRENT                 generation number of the committed snapshot, e.g. "7"
      snapshots/00000007/     complete copy of all business data (last 3 are kept)
        donors.dat donations.dat batches.dat requests.dat
        reservations.dat issues.dat screenings.dat
      admins.dat              credentials (saved independently, atomically)
      audit.log               append-only event log
      .setup_complete         marker: the first admin was created

## How saving works (crash safety)
1. Write every business file into a NEW directory `snapshots/<gen+1>/` and fsync each file.
2. Write `CURRENT.tmp`, fsync it, then `rename()` it over `CURRENT`. **This rename is the commit point.**
3. Prune snapshots older than the newest 3.

A crash before step 2 leaves `CURRENT` pointing at the previous complete snapshot (the half-written
directory is cleaned up on the next save). A crash after it leaves the new complete snapshot.
There is never a state where some files are new and others old.

If `CURRENT` is corrupt, or is missing while snapshots exist, the program refuses to start.
Recovery: check the newest directory under `snapshots/` and write its number into `CURRENT`.

Older flat-file installs (files directly in `data/`) are loaded and migrated on the first save;
released/discarded batches get a labelled `LEGACY-IMPORT` screening record with admin id 0.

## Record format
Text, one record per line, fields separated by `|`. Line 1 of every file is `BBMS1`.
Text fields never contain `|` or control characters. Dates are `YYYY-MM-DD`.
Blood groups are 0-7 (A+, A-, B+, B-, AB+, AB-, O+, O-). Files are mode `0600`, `data/` is `0700`.

| File | Fields |
|---|---|
| admins.dat | admin_id, username, salt (32 hex), PBKDF2 hash (64 hex), iterations, status (0 active, 1 inactive) |
| donors.dat | donor_id, name, contact, blood group, date of birth, active (0/1) |
| donations.dat | donation_id, donor_id, date, ml |
| batches.dat | batch_id, donation_id, blood group, collected, expiry, available_ml, reserved_ml, status (0 quarantined, 1 available, 2 expired, 3 discarded) |
| requests.dat | request_id, hospital, blood group, ml, date, status (0 pending, 1 approved, 2 rejected, 3 fulfilled) |
| reservations.dat | request_id, batch_id, ml |
| issues.dat | issue_id, request_id, batch_id, ml, date |
| screenings.dat | screening_id, batch_id, admin_id, date, outcome (1 passed, 0 rejected), reference or reason |
| audit.log | timestamp, admin_id, event, result |

## Integrity rules checked at startup
- Every record passes the same validators used for interactive input; IDs are unique.
- Donations reference donors; batches reference donations; issues reference a request and a batch.
- For every batch, the sum of its reservations equals `reserved_ml`.
- Every approved request holds reservations totalling exactly its quantity; no other request holds any.
- A batch's status must match its **latest screening record**: quarantined = none; available = passed;
  discarded = rejected; expired = none or passed. Screening records never precede the collection date.

Any failure aborts startup instead of guessing.
