#include "handlers.h"
#include "context.h"

typedef struct
{
    Res *res;
} ctx_t;

static void on_cat_deleted(PGquery *pg, PGresult *result, void *data);

void del_category(Req *req, Res *res)
{
    const char *cat_slug = get_param(req, "category");

    auth_context_t *auth_ctx = (auth_context_t *)get_context(req, "auth_ctx");

    if (!auth_ctx || !auth_ctx->is_author)
    {
        send_text(res, 401, "Not allowed");
        return;
    }

    // Allocate login context
    ctx_t *ctx = arena_alloc(res->arena, sizeof(ctx_t));
    if (!ctx)
    {
        send_text(res, 500, "Context allocation failed");
        return;
    }

    ctx->res = res;
    if (!ctx->res)
    {
        send_text(res, 500, "Response copy failed");
        return;
    }

    PGquery *pg = pg_query(db_get_pool(), res->arena);
    if (!pg)
    {
        send_text(res, 500, "Database connection error");
        return;
    }

    const char *delete_sql =
        "DELETE FROM categories "
        "WHERE author_id = $1 "
        "AND slug = $2;";

    const char *params[] = {auth_ctx->id, cat_slug};

    int qr = pg_query_queue(pg, delete_sql, 2, params, on_cat_deleted, ctx);
    if (qr != 0)
    {
        printf("ERROR: Failed to queue delete, result=%d\n", qr);
        send_text(res, 500, "Failed to queue delete");
        return;
    }

    if (pg_query_exec(pg) != 0)
    {
        printf("ERROR: Failed to execute delete\n");
        send_text(res, 500, "Failed to execute delete");
        return;
    }
}

static void on_cat_deleted(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_COMMAND_OK)
    {
        printf("Category could not be deleted: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Category could not be deleted");
        return;
    }

    if (PQcmdTuples(result) == 0)
    {
        send_text(ctx->res, 404, "Category not found");
        return;
    }

    send_text(ctx->res, 200, "Category deleted successfully");
}
