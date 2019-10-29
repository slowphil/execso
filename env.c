/* Copyright (c) 2018 Pablo Marcos Oltra <pablo.marcos.oltra@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define _GNU_SOURCE

#include "env.h"
#include "debug.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>

char** env_allocate(size_t size) {
    return calloc(size + 1, sizeof(char*));
}

void env_free(char* const *env) {
    size_t len = 0;
    while (env[len] != 0) {
        free(env[len]);
        len++;
    }
    free((char**)env);
}

size_t env_len(char* const *env) {
    size_t len = 0;
    while (env[len] != 0 ) {
        len++;
    }
    return len;
}

static size_t get_number_of_variables(FILE *file, char **buffer, size_t *len) {
    size_t number = 0;

    if (getline(buffer, len, file) < 0)
        return -1;

    char *ptr = *buffer;
    while (ptr < *buffer + *len) {
        size_t var_len = strlen(ptr);
        ptr += var_len + 1;
        if (var_len == 0)
            break;
        number++;
    }

    return number != 0 ? (ssize_t)number : -1;
}

static char* const* env_from_buffer(FILE *file) {
    char *buffer = NULL;
    size_t len = 0;
    size_t num_vars = get_number_of_variables(file, &buffer, &len);
    char** env = env_allocate(num_vars);

    size_t n = 0;
    char *ptr = buffer;
    while (ptr < buffer + len && n < num_vars) {
        size_t var_len = strlen(ptr);
        if (var_len == 0)
            break;

        env[n] = calloc(var_len + 1, sizeof(char));
        strncpy(env[n], ptr, var_len + 1);
        //DEBUG("\tenv var copied: %s\n", env[n]);
        ptr += var_len + 1;
        n++;
    }
    //DEBUG("number of env vars copied: %zu out of %zu of max length %zu\n", n, num_vars, len);
    free(buffer);

    return env;
}

static char* const* read_env_from_process(pid_t pid) {
    char buffer[256] = {0};

    snprintf(buffer, sizeof(buffer), "/proc/%d/environ", pid);
    //DEBUG("Reading env from parent process: %s\n", buffer);
    FILE *env_file = fopen(buffer, "r");
    if (!env_file) {
        DEBUG("Error reading file: %s (%s)\n", buffer, strerror(errno));
        return NULL;
    }

    char* const* env = env_from_buffer(env_file);
    fclose(env_file);

    return env;
}

char* const* read_env_recursive() {
    pid_t pid = getppid();
    char* const* env = NULL;

    int n_try = 1;
    while (pid > 1) {
        DEBUG("read_env_recursive: try %d, PID: %d\n", n_try++, (int)pid);
        if (env) env_free(env);
        env = read_env_from_process(pid);

        bool appdir_in_env = false;
        for (size_t i = 0; i < env_len(env); i++) {
            if (!strncmp(env[i], "APPDIR=", strlen("APPDIR="))) {
                appdir_in_env = true;
            }
        }
        if (!appdir_in_env) {
            //DEBUG("APPDIR not found in env. OK\n");
            break;
        } else {
            //DEBUG("APPDIR found in env. continue...\n");
            char buf[128];
            sprintf(buf, "/proc/%d/stat", (int)pid);
            FILE* fp = fopen(buf, "r");
            if (fp == NULL) {
              DEBUG("ERROR could not open /proc/%d/stat\n", (int)pid);
              break;
              }
            // Ignore one number, then two strings, then read a number
            if (fscanf(fp, "%*d %*s %*s %d", &pid) < 1) {
                DEBUG("ERROR fscanf failed\n");
        }
        fclose(fp);
        }
    }
    if (!env) {
      DEBUG("***ERROR cannot obtain parent env\n");
      }
    //else {
      //DEBUG("returning env size %zu\n", env_len(env));
      //}
    return env;
}

#ifdef ENV_TEST
int main() {
    putenv("APPIMAGE_EXECSO_DEBUG=1");
    //DEBUG("ENV TEST\n");
    char **env = NULL;
    read_env_recursive(&env);

    return 0;
}
#endif
