#include "handlers.h"
#include "context.h"
#include <stdlib.h>

typedef struct
{
    Res *res;
    bool is_author;
} ctx_t;

static void on_result(PGquery *pg, PGresult *result, void *data);

void get_profile(Req *req, Res *res)
{
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req, "auth_ctx");

    ctx_t *ctx = arena_alloc(req->arena, sizeof(ctx_t));
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

    ctx->is_author = auth_ctx->is_author;

    PGquery *pg = pg_query(db_get_pool(), res->arena);
    if (!pg)
    {
        printf("get_all_posts: Failed to create async context\n");
        send_text(res, 500, "Failed to create async context");
        return;
    }

    char *sql =
        "SELECT id, name, username, email, about "
        "FROM users "
        "WHERE username = $1; ";

    const char *params[] = {auth_ctx->user_slug};

    if (pg_query_queue(pg, sql, 1, params, on_result, ctx) != 0 ||
        pg_query_exec(pg) != 0)
    {
        send_text(res, 500, "Failed to queue or execute query");
        return;
    }
}

static void on_result(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result)
    {
        printf("ERROR: Result is NULL\n");
        send_text(ctx->res, 500, "No results");
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
}
