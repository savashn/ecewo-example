#include "handlers.h"

// Callback structure to hold request/response context
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

static void users_result_callback(pg_async_t *pg, PGresult *result, void *data);

// Async version of get_all_users
void get_all_users_async(Req *req, Res *res)
{
    const char *sql = "SELECT id, name, username FROM users;";

    // Create context to pass to callback
    ctx_t *ctx = calloc(1, sizeof(ctx_t));
    if (!ctx)
    {
        send_text(res, 500, "Memory allocation failed");
        return;
    }

    ctx->res = copy_res(res);

    // Create async PostgreSQL context
    pg_async_t *pg = pquv_create(db, ctx);
    if (!pg)
    {
        printf("get_all_users_async: Failed to create pg_async context\n");
        send_text(res, 500, "Failed to create async context");
        free(ctx);
        return;
    }

    // Queue the query
    int result = pquv_queue(pg, sql, 0, NULL, users_result_callback, ctx);
    if (result != 0)
    {
        printf("get_all_users_async: Failed to queue query\n");
        send_text(res, 500, "Failed to queue query");
        free(ctx);
        return;
    }

    // Start execution (this will return immediately)
    result = pquv_execute(pg);
    if (result != 0)
    {
        printf("get_all_users_async: Failed to execute query\n");
        send_text(res, 500, "Failed to execute query");
        free(ctx);
        return;
    }

    // Function returns here, callback will be called when query completes
}

// Callback function that processes the query result
static void users_result_callback(pg_async_t *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!ctx || !ctx->res)
    {
        printf("users_result_callback: Invalid context\n");
        return;
    }

    // Check result status
    ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_TUPLES_OK)
    {
        printf("users_result_callback: Query failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "DB select failed");
        free(ctx);
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

    // Cleanup
    cJSON_Delete(json_array);
    free(json_string);
    free_ctx(ctx);

    printf("users_result_callback: Response sent successfully\n");
}
