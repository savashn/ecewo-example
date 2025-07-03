#include "handlers.h"
#include "context.h"

typedef struct
{
    Res *res;
} ctx_t;

static void free_ctx(ctx_t *ctx)
{
    if (!ctx)
        return;
    if (ctx->res)
        free(ctx->res);
    free(ctx);
}

static void on_cat_deleted(pg_async_t *pg, PGresult *result, void *data);

void del_category(Req *req, Res *res)
{
    const char *cat_slug = get_params("category");

    auth_context_t *auth_ctx = get_context(req);

    if (!auth_ctx || !auth_ctx->is_author)
    {
        send_text(401, "Not allowed");
        return;
    }

    ctx_t *ctx = calloc(1, sizeof(ctx_t));
    if (!ctx)
    {
        send_text(500, "Memory allocation error");
        return;
    }

    ctx->res = malloc(sizeof(*ctx->res));
    if (!ctx->res)
    {
        free_ctx(ctx);
        send_text(500, "Memory allocation failed");
        return;
    }

    *ctx->res = *res;

    pg_async_t *pg = pquv_create(db, ctx);
    if (!pg)
    {
        free_ctx(ctx);
        send_text(500, "Database connection error");
        return;
    }

    const char *delete_sql =
        "DELETE FROM categories "
        "WHERE author_id = $1 "
        "AND slug = $2;";

    const char *params[] = {auth_ctx->id, cat_slug};

    printf("auth id: %s", auth_ctx->id);

    int qr = pquv_queue(pg, delete_sql, 2, params, on_cat_deleted, ctx);
    if (qr != 0)
    {
        printf("ERROR: Failed to queue delete, result=%d\n", qr);
        free_ctx(ctx);
        send_text(500, "Failed to queue delete");
        return;
    }

    if (pquv_execute(pg) != 0)
    {
        printf("ERROR: Failed to execute delete\n");
        free_ctx(ctx);
        send_text(500, "Failed to execute delete");
        return;
    }
}

static void on_cat_deleted(pg_async_t *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!ctx || !ctx->res)
    {
        printf("Invalid context\n");
        if (ctx)
            free_ctx(ctx);
        return;
    }

    Res *res = ctx->res;
    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_COMMAND_OK)
    {
        printf("Category could not be deleted: %s\n", PQresultErrorMessage(result));
        send_text(500, "Category could not be deleted");
        free_ctx(ctx);
        return;
    }

    if (PQcmdTuples(result) == 0)
    {
        send_text(404, "Category not found");
        free_ctx(ctx);
        return;
    }

    send_text(200, "Category deleted successfully");
    free_ctx(ctx);
}
