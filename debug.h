#ifndef DEBUG_H
#define DEBUG_H

#include <stdlib.h>
#include <stdio.h>

#define DEBUG(...) do { \
    if (getenv("APPIMAGE_CHECKRT_DEBUG")) \
        printf("APPIMAGE_CHECKRT>> %s: ", __func__); \
        printf(__VA_ARGS__); \
} while (0)

#endif // DEBUG_H
