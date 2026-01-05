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
    ctx->is_author = auth_ctx->is_author;

    PGquery *pg = pg_query_create(db_get_pool(), res->arena);
    if (!pg)
    {
        send_text(res, 500, "Failed to create async context");
        return;
    }

    const char *sql =
        "SELECT id, name, username, email, about "
        "FROM users "
        "WHERE username = $1;";

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

    if (PQresultStatus(result) != PGRES_TUPLES_OK || PQntuples(result) == 0)
    {
        send_text(ctx->res, 500, "No results");
        return;
    }

    cJSON *resp = cJSON_CreateObject();

    cJSON_AddNumberToObject(resp, "id", atoi(PQgetvalue(result, 0, PQfnumber(result, "id"))));

    cJSON_AddStringToObject(resp, "name", PQgetvalue(result, 0, PQfnumber(result, "name")));
    cJSON_AddStringToObject(resp, "email", PQgetvalue(result, 0, PQfnumber(result, "email")));
    cJSON_AddStringToObject(resp, "about", PQgetvalue(result, 0, PQfnumber(result, "about")));

    cJSON_AddBoolToObject(resp, "is_author", ctx->is_author);

    char *json_str = cJSON_PrintUnformatted(resp);
    send_json(ctx->res, 200, json_str);

    free(json_str);
    cJSON_Delete(resp);
}
