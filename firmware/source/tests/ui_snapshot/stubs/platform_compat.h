#pragma once
#include <time.h>
#ifdef _WIN32
static inline struct tm* localtime_r(const time_t* value, struct tm* output) {
    return localtime_s(output, value) == 0 ? output : NULL;
}
#endif
