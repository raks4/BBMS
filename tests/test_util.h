#ifndef TEST_UTIL_H
#define TEST_UTIL_H
#include <stdio.h>
static int g_failures = 0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: FAILED: %s\n", __FILE__, __LINE__, #c); g_failures++; } } while (0)
#define TEST_DONE() do { if (g_failures) { fprintf(stderr, "%d failure(s)\n", g_failures); return 1; } puts("ok"); return 0; } while (0)
#endif
