#ifndef ENV_H
#define ENV_H

#include <unistd.h>

char* const* read_env_recursive();
void env_free(char* const *env);

#endif
