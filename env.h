#ifndef ENV_H
#define ENV_H

#include <unistd.h>

char* const* read_env_recursive();
void env_free(char* const *env);
size_t env_len(char* const x[]); 
char** env_allocate(size_t size);

#endif
