#include "handlers.h"
#include "context.h"
#include "json_helper.h"

typedef struct
{
    Res *res;
    bool is_author;
} ctx_t;

static void free_ctx(ctx_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->res)
        free(ctx->res);

    free(ctx);
}

static void on_result(pg_async_t *pg, PGresult *result, void *data);

void get_profile(Req *req, Res *res)
{
    auth_context_t *auth_ctx = get_context(req);

    ctx_t *ctx = calloc(1, sizeof(ctx_t));
    if (!ctx)
    {
        send_text(500, "Memory allocation failed");
        return;
    }

    ctx->res = malloc(sizeof(*ctx->res));

    if (!ctx->res)
    {
        send_text(500, "Memory allocation failed");
        free_ctx(ctx);
        return;
    }

    *ctx->res = *res;
    ctx->is_author = auth_ctx->is_author;

    pg_async_t *pg = pquv_create(db, ctx);
    if (!pg)
    {
        printf("get_all_posts: Failed to create async context\n");
        send_text(500, "Failed to create async context");
        free(ctx);
        return;
    }

    char *sql =
        "SELECT id, name, username, email, about "
        "FROM users "
        "WHERE username = $1; ";

    const char *params[] = {auth_ctx->user_slug};

    if (pquv_queue(pg, sql, 1, params, on_result, ctx) != 0 ||
        pquv_execute(pg) != 0)
    {
        send_text(500, "Failed to queue or execute query");
        free_ctx(ctx);
        return;
    }
}

static void on_result(pg_async_t *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!ctx || !ctx->res)
    {
        printf("on_query_posts: Invalid context\n");
        if (ctx)
            free_ctx(ctx);
        return;
    }

    if (!result)
    {
        printf("ERROR: Result is NULL\n");
        free_ctx(ctx);
        return;
    }

    Res *res = ctx->res;

    cJSON *resp = cJSON_CreateObject();

    ADD_INT(resp, id);
    ADD_STR(resp, name);
    ADD_STR(resp, email);
    ADD_STR(resp, about);
    cJSON_AddBoolToObject(resp, "is_author", ctx->is_author);

    char *json_str = cJSON_PrintUnformatted(resp);
    send_json(200, json_str);

    free(json_str);
    cJSON_Delete(resp);
    free_ctx(ctx);
}
