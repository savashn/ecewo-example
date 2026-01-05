#include "handlers.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    Res *res;
} ctx_t;

static void users_result_callback(PGquery *pg, PGresult *result, void *data);

void get_all_users_async(Req *req, Res *res)
{
    const char *sql = "SELECT id, name, username FROM users;";

    ctx_t *ctx = arena_alloc(req->arena, sizeof(ctx_t));
    if (!ctx)
    {
        send_text(res, 500, "Context allocation failed");
        return;
    }

    ctx->res = res;

    PGquery *pg = pg_query_create(db_get_pool(), res->arena);
    if (!pg)
    {
        send_text(res, 500, "Failed to create async context");
        return;
    }

    if (pg_query_queue(pg, sql, 0, NULL, users_result_callback, ctx) != 0)
    {
        send_text(res, 500, "Failed to queue query");
        return;
    }

    if (pg_query_exec(pg) != 0)
    {
        send_text(res, 500, "Failed to execute query");
        return;
    }
}

static void users_result_callback(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_TUPLES_OK)
    {
        printf("users_result_callback: Query failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "DB select failed");
        return;
    }

    int rows = PQntuples(result);
    cJSON *json_array = cJSON_CreateArray();

    for (int i = 0; i < rows; i++)
    {
        int id = atoi(PQgetvalue(result, i, 0));
        const char *name = PQgetvalue(result, i, 1);
        const char *username = PQgetvalue(result, i, 2);

        cJSON *user_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(user_json, "id", id);
        cJSON_AddStringToObject(user_json, "name", name);
        cJSON_AddStringToObject(user_json, "username", username);

        cJSON_AddItemToArray(json_array, user_json);
    }

    char *json_string = cJSON_PrintUnformatted(json_array);
    send_json(ctx->res, 200, json_string);

    cJSON_Delete(json_array);
    free(json_string);
}
