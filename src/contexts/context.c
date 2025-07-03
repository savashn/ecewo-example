#include "context.h"
#include <stdlib.h>
#include <stdio.h>

void cleanup_auth_ctx(void *data)
{
    auth_context_t *ctx = (auth_context_t *)data;

    if (ctx->id)
        free(ctx->id);
    if (ctx->name)
        free(ctx->name);
    if (ctx->username)
        free(ctx->username);
    if (ctx->user_slug)
        free(ctx->user_slug);

    free(ctx);
}
