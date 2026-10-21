# Blood Bank Management System (C)

Console application: donors, donations, quarantined/released batches, inventory with expiry,
requests with stock reservation (earliest-expiry-first), issuing, reports, and admin accounts.

## Build
Needs a C11 compiler and OpenSSL development files.

    # Debian/Ubuntu: sudo apt install build-essential libssl-dev
    # Fedora:        sudo dnf install gcc make openssl-devel
    # macOS:         brew install openssl   (then: make CPPFLAGS=-I$(brew --prefix openssl)/include LDFLAGS=-L$(brew --prefix openssl)/lib)
    make
    make test
    ./bloodbank

## First run
No admin exists, so you are guided through one-time setup. For anything beyond local
experiments, set a setup secret first so only someone who knows it can create the first admin:

    BBMS_SETUP_SECRET='something-long' ./bloodbank

If `data/.setup_complete` exists but `admins.dat` is missing or has no active admin, the program
refuses to start rather than recreating the first admin (controlled recovery required).

## Workflow
Record donation -> batch is QUARANTINED -> release after screening (records who, when and the lab
reference in `screenings.dat`) -> request -> approve (reserves stock) -> issue (consumes the
reservation, request becomes fulfilled). Rejecting/discarding a batch also records who and why.

## Accounts
Administrators can change their own password and reset another admin's password. Both
re-verify the caller's current password first, enforce the password policy, refuse to reuse the
old password, and roll back if the save fails.

## Saving and recovery
Saves are crash-safe across all files: a new snapshot directory is written and flushed, then a
single atomic rename of `data/CURRENT` commits it (details in `docs/file-format.md`).
If `CURRENT` is damaged the program refuses to start; the recovery steps are in that document.

## Tests
`make test` runs the unit tests. `make asan` runs them under AddressSanitizer/UBSan.

## Security notes
- Passwords: PBKDF2-HMAC-SHA-256, 600,000 iterations, 16-byte random salt, constant-time compare.
- Every protected admin function re-checks the session itself.
- Nothing is encrypted at rest; protect `data/` with OS permissions or disk encryption.

## Known limitations (prototype)
- Blood-group matching is exact only. Real compatibility/component rules need clinical validation.
- Business constants in `include/common.h` (age, interval, shelf life, volumes) are placeholders.
- No reversal/correction records for issues yet; issue history is append-only.
- Low-stock thresholds are in memory for the session only.
- Single user at a time; there is no file locking, so do not run two instances on the same data.
- Admin accounts live outside the snapshots, so restoring an old snapshot does not roll back passwords.
- Login attempt limits reset when the program restarts.
