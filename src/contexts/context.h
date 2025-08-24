#ifndef CONTEXT_H
#define CONTEXT_H

#include <stdbool.h>

typedef struct
{
    char *id; // It should be char* because libpq waits for string
    char *name;
    char *username;
    bool is_admin;
    bool is_author;
    char *user_slug;
} auth_context_t;

#endif
