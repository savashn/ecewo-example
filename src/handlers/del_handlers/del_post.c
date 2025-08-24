#include "handlers.h"
#include "context.h"

typedef struct
{
    Arena *arena;
    Res *res;
} ctx_t;

static void on_post_deleted(pg_async_t *pg, PGresult *result, void *data);

void del_post(Req *req, Res *res)
{
    const char *post_slug = get_params(req, "post");

    auth_context_t *auth_ctx = (auth_context_t *)get_context(req);

    if (!auth_ctx || !auth_ctx->is_author)
    {
        send_text(res, 401, "Not allowed");
        return;
    }

    // Create separate arena for async operation
    Arena *async_arena = calloc(1, sizeof(Arena));
    if (!async_arena) {
        send_text(res, 500, "Arena allocation failed");
        return;
    }

    // Allocate login context
    ctx_t *ctx = arena_alloc(async_arena, sizeof(ctx_t));
    if (!ctx) {
        arena_free(async_arena);
        free(async_arena);
        send_text(res, 500, "Context allocation failed");
        return;
    }

    // Store arena reference
    ctx->arena = async_arena;

    ctx->res = arena_copy_res(async_arena, res);
    if (!ctx->res)
    {
        free_ctx(ctx->arena);
        send_text(res, 500, "Response copy failed");
        return;
    }

    pg_async_t *pg = pquv_create(db, ctx);
    if (!pg)
    {
        free_ctx(ctx->arena);
        send_text(res, 500, "Database connection error");
        return;
    }

    const char *delete_sql =
        "DELETE FROM posts p "
        "WHERE p.author_id = $1 "
        "AND p.slug     = $2;";

    const char *params[] = {auth_ctx->id, post_slug};

    int qr = pquv_queue(pg, delete_sql, 2, params, on_post_deleted, ctx);
    if (qr != 0)
    {
        printf("ERROR: Failed to queue delete, result=%d\n", qr);
        free_ctx(ctx->arena);
        send_text(res, 500, "Failed to queue delete");
        return;
    }

    if (pquv_execute(pg) != 0)
    {
        printf("ERROR: Failed to execute delete\n");
        send_text(res, 500, "Failed to execute delete");
        free_ctx(ctx->arena);
        return;
    }
}

static void on_post_deleted(pg_async_t *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!ctx || !ctx->res)
    {
        printf("Invalid context\n");
        send_text(ctx->res, 400, "Invalid context");
        if (ctx)
            free_ctx(ctx->arena);
        return;
    }

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_COMMAND_OK)
    {
        printf("Post could not be deleted: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Post could not be deleted");
        free_ctx(ctx->arena);
        return;
    }

    if (PQcmdTuples(result) == 0)
    {
        send_text(ctx->res, 404, "Post not found");
        free_ctx(ctx->arena);
        return;
    }

    send_text(ctx->res, 200, "Post deleted successfully");
    free_ctx(ctx->arena);
}
