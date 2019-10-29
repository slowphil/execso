#ifndef DEBUG_H
#define DEBUG_H

#include <stdlib.h>
#include <stdio.h>

#define DEBUG(...) do { \
    if (getenv("APPIMAGE_EXECSO_DEBUG")) \
        {fprintf(stderr,"EXECSO>> %s: ", __func__); \
        fprintf(stderr,__VA_ARGS__);} \
} while (0)

#endif // DEBUG_H
