#ifndef UTILS_H
#define UTILS_H

#include "arena.h"

int compute_reading_time(const char *content);

static inline void free_ctx(Arena *arena)
{
    if (!arena)
        return;
    arena_free(arena);
    free(arena);
}

#endif
