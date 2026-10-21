#include "validation.h"
#include "security.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Field validators                                                    */
/* ------------------------------------------------------------------ */

static size_t bounded_len(const char *s, size_t cap)
{
    size_t n = 0;
    while (n < cap && s[n] != '\0') n++;
    return n;               /* == cap means "not terminated" */
}

bool validate_username(const char *u)
{
    if (!u) return false;
    size_t n = bounded_len(u, USERNAME_LEN);
    if (n < 3 || n >= USERNAME_LEN) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)u[i];
        if (!(isalnum(c) || c == '_' || c == '.' || c == '-')) return false;
    }
    return true;
}

bool validate_password_policy(const char *p)
{
    if (!p) return false;
    size_t n = bounded_len(p, PASSWORD_MAX_LEN + 1);
    if (n < 12 || n > PASSWORD_MAX_LEN) return false;
    bool up = false, lo = false, dg = false, sy = false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c < 0x20 || c == 0x7f) return false;
        if (isupper(c)) up = true;
        else if (islower(c)) lo = true;
        else if (isdigit(c)) dg = true;
        else sy = true;
    }
    return up && lo && dg && sy;
}

bool validate_blood_group(BloodGroup g)
{
    return (int)g >= 0 && (int)g < BLOOD_GROUP_COUNT;
}

bool validate_quantity(int q)
{
    return q > 0 && q <= REQUEST_MAX_ML;
}

static bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

static int days_in_month(int y, int m)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}

bool validate_date(Date d)
{
    return d.year >= 1900 && d.year <= 2200 && d.month >= 1 && d.month <= 12 &&
           d.day >= 1 && d.day <= days_in_month(d.year, d.month);
}

bool validate_contact(const char *c)
{
    if (!c) return false;
    size_t n = bounded_len(c, CONTACT_LEN);
    if (n == CONTACT_LEN || n == 0) return false;
    size_t i = (c[0] == '+') ? 1 : 0;
    size_t digits = n - i;
    if (digits < 7 || digits > 15) return false;
    for (; i < n; i++)
        if (!isdigit((unsigned char)c[i])) return false;
    return true;
}

bool validate_name(const char *s, size_t cap)
{
    if (!s || cap == 0) return false;
    size_t n = bounded_len(s, cap);
    if (n == 0 || n == cap) return false;
    bool visible = false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x80) { visible = true; continue; }       /* UTF-8 text */
        if (isalnum(c)) { visible = true; continue; }
        if (c == ' ' || c == '.' || c == ',' || c == '\'' || c == '-' ||
            c == '&' || c == '(' || c == ')' || c == '/') continue;
        return false;        /* control chars and the '|' file delimiter */
    }
    return visible;
}

/* ------------------------------------------------------------------ */
/* Dates (Howard Hinnant's civil-date algorithms)                      */
/* ------------------------------------------------------------------ */

int date_compare(Date a, Date b)
{
    if (a.year != b.year) return a.year < b.year ? -1 : 1;
    if (a.month != b.month) return a.month < b.month ? -1 : 1;
    if (a.day != b.day) return a.day < b.day ? -1 : 1;
    return 0;
}

long date_to_days(Date d)
{
    long y = d.year;
    unsigned m = (unsigned)d.month, dd = (unsigned)d.day;
    if (m <= 2) y -= 1;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + dd - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

Date date_from_days(long z)
{
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    Date r = { (int)(y + (m <= 2 ? 1 : 0)), (int)m, (int)d };
    return r;
}

Date date_add_days(Date d, int days) { return date_from_days(date_to_days(d) + days); }
long date_diff_days(Date later, Date earlier) { return date_to_days(later) - date_to_days(earlier); }

Date date_today(void)
{
    time_t t = time(NULL);
    struct tm *tp = localtime(&t);
    Date d = { tp->tm_year + 1900, tp->tm_mon + 1, tp->tm_mday };
    return d;
}

int date_age_years(Date dob, Date today)
{
    int age = today.year - dob.year;
    if (today.month < dob.month || (today.month == dob.month && today.day < dob.day))
        age--;
    return age;
}

bool date_parse(const char *s, Date *out)
{
    if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) continue;
        if (!isdigit((unsigned char)s[i])) return false;
    }
    Date d = { atoi(s), atoi(s + 5), atoi(s + 8) };
    if (!validate_date(d)) return false;
    *out = d;
    return true;
}

void date_format(Date d, char *buf, size_t cap)
{
    snprintf(buf, cap, "%04d-%02d-%02d", d.year, d.month, d.day);
}

/* ------------------------------------------------------------------ */
/* Blood groups                                                        */
/* ------------------------------------------------------------------ */

static const char *const BG_NAMES[BLOOD_GROUP_COUNT] =
    { "A+", "A-", "B+", "B-", "AB+", "AB-", "O+", "O-" };

const char *blood_group_str(BloodGroup g)
{
    return validate_blood_group(g) ? BG_NAMES[g] : "?";
}

bool blood_group_parse(const char *s, BloodGroup *out)
{
    if (!s) return false;
    char tmp[8];
    size_t n = strlen(s);
    if (n == 0 || n >= sizeof tmp) return false;
    for (size_t i = 0; i <= n; i++) tmp[i] = (char)toupper((unsigned char)s[i]);
    for (int g = 0; g < BLOOD_GROUP_COUNT; g++)
        if (strcmp(tmp, BG_NAMES[g]) == 0) { *out = (BloodGroup)g; return true; }
    return false;
}

/* ------------------------------------------------------------------ */
/* Bounded console input                                               */
/* ------------------------------------------------------------------ */

void trim_in_place(char *s)
{
    size_t n = strlen(s), start = 0;
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
    while (s[start] && isspace((unsigned char)s[start])) start++;
    if (start) memmove(s, s + start, strlen(s + start) + 1);
}

/* Returns false only on EOF/error. Over-long lines are discarded and
 * reported as an empty string so validators reject them. */
bool input_line(const char *prompt, char *buf, size_t cap)
{
    if (prompt) { fputs(prompt, stdout); fflush(stdout); }
    if (!fgets(buf, (int)cap, stdin)) { buf[0] = '\0'; return false; }
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') {
        buf[--n] = '\0';
    } else if (n == cap - 1) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF) { }
        puts("Input too long.");
        buf[0] = '\0';
        return true;
    }
    if (n > 0 && buf[n - 1] == '\r') buf[--n] = '\0';
    return true;
}

bool input_int(const char *prompt, int *out, int min, int max)
{
    char line[64];
    for (;;) {
        if (!input_line(prompt, line, sizeof line)) return false;
        trim_in_place(line);
        char *end;
        errno = 0;
        long v = strtol(line, &end, 10);
        if (line[0] != '\0' && *end == '\0' && errno == 0 && v >= min && v <= max) {
            *out = (int)v;
            return true;
        }
        printf("Enter a whole number between %d and %d.\n", min, max);
    }
}

bool input_date(const char *prompt, Date *out)
{
    char line[32];
    for (;;) {
        if (!input_line(prompt, line, sizeof line)) return false;
        trim_in_place(line);
        if (date_parse(line, out)) return true;
        puts("Enter a valid date as YYYY-MM-DD.");
    }
}

bool input_blood_group(const char *prompt, BloodGroup *out)
{
    char line[16];
    for (;;) {
        if (!input_line(prompt, line, sizeof line)) return false;
        trim_in_place(line);
        if (blood_group_parse(line, out)) return true;
        puts("Valid groups: A+ A- B+ B- AB+ AB- O+ O-");
    }
}

bool input_name(const char *prompt, char *buf, size_t cap)
{
    for (;;) {
        if (!input_line(prompt, buf, cap)) return false;
        trim_in_place(buf);
        if (validate_name(buf, cap)) return true;
        puts("Invalid text: use letters, digits, spaces and . , ' - & ( ) /");
    }
}

bool input_contact(const char *prompt, char *buf, size_t cap)
{
    for (;;) {
        if (!input_line(prompt, buf, cap)) return false;
        trim_in_place(buf);
        if (validate_contact(buf)) return true;
        puts("Contact must be 7-15 digits, optionally starting with '+'.");
    }
}

bool input_confirm(const char *prompt)
{
    char line[16];
    if (!input_line(prompt, line, sizeof line)) return false;
    trim_in_place(line);
    return line[0] == 'y' || line[0] == 'Y';
}
