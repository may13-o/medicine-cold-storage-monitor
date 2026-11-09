#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>

static int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        long long _a = (long long)(a), _b = (long long)(b);                      \
        if (_a != _b) {                                                          \
            printf("  FAIL %s:%d  %s == %s  (got %lld, want %lld)\n", __FILE__,  \
                   __LINE__, #a, #b, _a, _b);                                    \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

#define FINISH(name)                                                             \
    do {                                                                         \
        if (g_failures == 0) {                                                   \
            printf("%s: all checks passed\n", name);                             \
            return 0;                                                            \
        }                                                                        \
        printf("%s: %d check(s) FAILED\n", name, g_failures);                    \
        return 1;                                                                \
    } while (0)

#endif /* CHECK_H */
