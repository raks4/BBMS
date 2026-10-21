#ifndef VALIDATION_H
#define VALIDATION_H

#include "common.h"

/* ---- field validators ---- */
bool validate_username(const char *username);
bool validate_password_policy(const char *password);
bool validate_blood_group(BloodGroup group);
bool validate_quantity(int quantity_ml);
bool validate_date(Date date);
bool validate_contact(const char *contact);
bool validate_name(const char *name, size_t capacity);

/* ---- dates ---- */
int    date_compare(Date a, Date b);
long   date_to_days(Date d);
Date   date_from_days(long days);
Date   date_add_days(Date d, int days);
long   date_diff_days(Date later, Date earlier);
Date   date_today(void);
int    date_age_years(Date dob, Date today);
bool   date_parse(const char *s, Date *out);          /* YYYY-MM-DD */
void   date_format(Date d, char *buf, size_t cap);

/* ---- blood groups ---- */
const char *blood_group_str(BloodGroup g);
bool        blood_group_parse(const char *s, BloodGroup *out);

/* ---- bounded console input ---- */
void trim_in_place(char *s);
bool input_line(const char *prompt, char *buf, size_t cap);
bool input_int(const char *prompt, int *out, int min, int max);
bool input_date(const char *prompt, Date *out);
bool input_blood_group(const char *prompt, BloodGroup *out);
bool input_name(const char *prompt, char *buf, size_t cap);
bool input_contact(const char *prompt, char *buf, size_t cap);
bool input_confirm(const char *prompt);

#endif
