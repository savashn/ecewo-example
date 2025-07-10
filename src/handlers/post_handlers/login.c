#include <sodium.h>
#include "handlers.h"
#include "session.h"

typedef struct
{
    Res *res;
    char *username;
    char *password;
    char *user_id;
    char *name;
    char *hashed_password;
} ctx_t;

static void free_ctx(ctx_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->res)
        destroy_res(ctx->res);
    if (ctx->username)
        free(ctx->username);
    if (ctx->password)
        free(ctx->password);
    if (ctx->user_id)
        free(ctx->user_id);
    if (ctx->name)
        free(ctx->name);
    if (ctx->hashed_password)
        free(ctx->hashed_password);

    free(ctx);
}

static void on_user_found(pg_async_t *pg, PGresult *result, void *data);

void login(Req *req, Res *res)
{
    Session *user_session = get_session(req);

    if (user_session)
    {
        send_text(res, 400, "Error: You are already logged in");
        return;
    }

    // Parse JSON
    cJSON *json = cJSON_Parse(req->body);
    if (!json)
    {
        send_text(res, 400, "Invalid JSON");
        return;
    }

    const cJSON *juser = cJSON_GetObjectItem(json, "username");
    const cJSON *jpass = cJSON_GetObjectItem(json, "password");

    if (!juser || !jpass || !juser->valuestring || !jpass->valuestring)
    {
        printf("ERROR: Username or password missing\n");
        cJSON_Delete(json);
        send_text(res, 400, "Username or password is missing");
        return;
    }

    // Create login context
    ctx_t *ctx = calloc(1, sizeof(ctx_t));
    if (!ctx)
    {
        printf("ERROR: Memory allocation failed\n");
        send_text(res, 500, "Memory allocation error");
        return;
    }

    ctx->username = strdup(juser->valuestring);
    ctx->password = strdup(jpass->valuestring);
    cJSON_Delete(json);

    Res *copy = copy_res(res);
    ctx->res = copy;

    // Create PostgreSQL async context
    pg_async_t *pg = pquv_create(db, ctx);

    if (!pg)
    {
        printf("ERROR: Failed to create pg_async context\n");
        free_ctx(ctx);
        send_text(res, 500, "Database connection error");
        return;
    }

    // Queue the SELECT query
    const char *select_sql = "SELECT id, name, password FROM users WHERE username = $1";
    const char *params[] = {ctx->username};

    int query_result = pquv_queue(pg, select_sql, 1, params, on_user_found, ctx);
    if (query_result != 0)
    {
        printf("ERROR: Failed to queue query, result=%d\n", query_result);
        free_ctx(ctx);
        send_text(res, 500, "Failed to queue query");
        return;
    }

    // Start execution
    int exec_result = pquv_execute(pg);
    if (exec_result != 0)
    {
        printf("ERROR: Failed to execute, result=%d\n", exec_result);
        free_ctx(ctx);
        send_text(res, 500, "Failed to execute query");
        return;
    }
}

static void on_user_found(pg_async_t *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result)
    {
        printf("ERROR: Result is NULL\n");
        free_ctx(ctx);
        return;
    }

    ExecStatusType status = PQresultStatus(result);

    if (PQntuples(result) == 0)
    {
        free_ctx(ctx);
        send_text(ctx->res, 404, "User not found");
        return;
    }

    // Extract user data
    ctx->user_id = strdup(PQgetvalue(result, 0, 0));
    ctx->name = strdup(PQgetvalue(result, 0, 1));
    ctx->hashed_password = strdup(PQgetvalue(result, 0, 2));

    // Verify password
    if (sodium_init() < 0)
    {
        send_text(ctx->res, 500, "Crypto init failed");
        return;
    }

    int verify_result = crypto_pwhash_str_verify(ctx->hashed_password, ctx->password, strlen(ctx->password));
    if (verify_result != 0)
    {
        send_text(ctx->res, 401, "Incorrect password");
        free_ctx(ctx);
        return;
    }

    Session *sess = create_session(3600);

    set_session(sess, "id", ctx->user_id);
    set_session(sess, "name", ctx->name);
    set_session(sess, "username", ctx->username);

    if (strstr(ctx->username, "johndoe"))
    {
        set_session(sess, "is_admin", "true");
    }

    // The cookie_options must be the same in the logout handler
    cookie_options_t cookie_options = {
        .max_age = 3600, // 1 hour
        .path = "/",
        .same_site = "Lax",
        .http_only = true,
        .secure = true,
    };

    send_session(ctx->res, sess, &cookie_options);
    send_text(ctx->res, 200, "Login successful");
}
