#include "handlers.h"
#include "context.h"

typedef struct
{
    Arena *arena;
    Res *res;
    bool is_author;
} ctx_t;

static void on_result(pg_async_t *pg, PGresult *result, void *data);

void get_profile(Req *req, Res *res)
{
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req);

    Arena *async_arena = calloc(1, sizeof(Arena));
    if (!async_arena) {
        send_text(res, 500, "Arena allocation failed");
        return;
    }

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

    ctx->is_author = auth_ctx->is_author;

    pg_async_t *pg = pquv_create(db, ctx);
    if (!pg)
    {
        printf("get_all_posts: Failed to create async context\n");
        send_text(res, 500, "Failed to create async context");
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
        send_text(res, 500, "Failed to queue or execute query");
        free_ctx(ctx->arena);
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
            free_ctx(ctx->arena);
        return;
    }

    if (!result)
    {
        printf("ERROR: Result is NULL\n");
        free_ctx(ctx->arena);
        return;
    }

    cJSON *resp = cJSON_CreateObject();

    // Add integer field
    cJSON_AddNumberToObject(resp, "id", atoi(PQgetvalue(result, 0, PQfnumber(result, "id"))));

    // Add string fields
    cJSON_AddStringToObject(resp, "name", PQgetvalue(result, 0, PQfnumber(result, "name")));
    cJSON_AddStringToObject(resp, "email", PQgetvalue(result, 0, PQfnumber(result, "email")));
    cJSON_AddStringToObject(resp, "about", PQgetvalue(result, 0, PQfnumber(result, "about")));

    // Add boolean field
    cJSON_AddBoolToObject(resp, "is_author", ctx->is_author);

    char *json_str = cJSON_PrintUnformatted(resp);
    send_json(ctx->res, 200, json_str);

    free(json_str);
    cJSON_Delete(resp);
    free_ctx(ctx->arena);
}
