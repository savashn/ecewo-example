#include "handlers.h"
#include "context.h"
#include <stdlib.h>

typedef struct
{
    Res *res;
} ctx_t;

static void on_post_deleted(PGquery *pg, PGresult *result, void *data);

void del_post(Req *req, Res *res)
{
    const char *post_slug = get_param(req, "post");

    auth_context_t *auth_ctx = (auth_context_t *)get_context(req, "auth_ctx");

    if (!auth_ctx || !auth_ctx->is_author)
    {
        send_text(res, 401, "Not allowed");
        return;
    }

    ctx_t *ctx = arena_alloc(res->arena, sizeof(ctx_t));
    if (!ctx)
    {
        send_text(res, 500, "Context allocation failed");
        return;
    }

    ctx->res = res;

    PGquery *pg = pg_query_create(db_get_pool(), res->arena);
    if (!pg)
    {
        send_text(res, 500, "Database connection error");
        return;
    }

    const char *delete_sql =
        "DELETE FROM posts p "
        "WHERE p.author_id = $1 "
        "AND p.slug     = $2;";

    const char *params[] = {auth_ctx->id, post_slug};

    if (pg_query_queue(pg, delete_sql, 2, params, on_post_deleted, ctx) != 0)
    {
        send_text(res, 500, "Failed to queue delete");
        return;
    }

    if (pg_query_exec(pg) != 0)
    {
        send_text(res, 500, "Failed to execute delete");
        return;
    }
}

static void on_post_deleted(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_COMMAND_OK)
    {
        printf("Post could not be deleted: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Post could not be deleted");
        return;
    }

    char *affected = PQcmdTuples(result);
    if (atoi(affected) == 0)
    {
        send_text(ctx->res, 404, "Post not found");
        return;
    }

    send_text(ctx->res, 200, "Post deleted successfully");
}
