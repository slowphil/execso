#ifndef ENV_H
#define END_H

#include <unistd.h>

char* const* read_env_recursive();
void env_free(char* const *env);

#endif
