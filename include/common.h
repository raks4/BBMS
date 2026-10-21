#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NAME_LEN        100
#define USERNAME_LEN     32
#define CONTACT_LEN      20
#define HOSPITAL_LEN    100
#define ERR_LEN         160
#define DATA_DIR        "data"

/* Business-rule constants (adjust to your local policy; these are NOT
 * clinically validated). */
#define DONATION_MIN_ML          200
#define DONATION_MAX_ML          600
#define REQUEST_MIN_ML            50
#define REQUEST_MAX_ML         20000
#define BATCH_SHELF_LIFE_DAYS     35
#define DONOR_MIN_AGE             18
#define DONOR_MAX_AGE             65
#define DONATION_INTERVAL_DAYS    56
#define DEFAULT_LOW_STOCK_ML    2000
#define EXPIRY_WARNING_DAYS        7
#define BLOOD_GROUP_COUNT          8

typedef enum {
    BLOOD_A_POS, BLOOD_A_NEG,
    BLOOD_B_POS, BLOOD_B_NEG,
    BLOOD_AB_POS, BLOOD_AB_NEG,
    BLOOD_O_POS, BLOOD_O_NEG
} BloodGroup;

typedef struct {
    int year;
    int month;
    int day;
} Date;

typedef enum { STATUS_ACTIVE, STATUS_INACTIVE } AccountStatus;

typedef enum {
    REQUEST_PENDING, REQUEST_APPROVED, REQUEST_REJECTED, REQUEST_FULFILLED
} RequestStatus;

typedef enum {
    BATCH_QUARANTINED, BATCH_AVAILABLE, BATCH_EXPIRED, BATCH_DISCARDED
} BatchStatus;

/* Aggregate of every in-memory list; defined in app.h */
typedef struct AppData AppData;

#endif
