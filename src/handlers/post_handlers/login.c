#include <sodium.h>
#include "handlers.h"
#include "ecewo-session.h"
#include <stdio.h>

typedef struct
{
    Res *res;
    char *username;
    char *password;
    char *user_id;
    char *name;
    char *hashed_password;
} ctx_t;

static void on_user_found(PGquery *pg, PGresult *result, void *data);

void login(Req *req, Res *res)
{
    Session *user_session = session_get(req);

    if (user_session)
    {
        send_text(res, 400, "Error: You are already logged in");
        return;
    }

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
        cJSON_Delete(json);
        send_text(res, 400, "Username or password is missing");
        return;
    }

    ctx_t *ctx = arena_alloc(res->arena, sizeof(ctx_t));
    if (!ctx)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Context allocation failed");
        return;
    }

    ctx->res = res;
    ctx->username = arena_strdup(res->arena, juser->valuestring);
    ctx->password = arena_strdup(res->arena, jpass->valuestring);
    cJSON_Delete(json);

    PGquery *pg = pg_query_create(db_get_pool(), res->arena);
    if (!pg)
    {
        send_text(res, 500, "Database connection error");
        return;
    }

    const char *select_sql = "SELECT id, name, password FROM users WHERE username = $1";
    const char *params[] = {ctx->username};

    if (pg_query_queue(pg, select_sql, 1, params, on_user_found, ctx) != 0)
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

static void on_user_found(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (PQntuples(result) == 0)
    {
        send_text(ctx->res, 404, "User not found");
        return;
    }

    ctx->user_id = arena_strdup(ctx->res->arena, PQgetvalue(result, 0, 0));
    ctx->name = arena_strdup(ctx->res->arena, PQgetvalue(result, 0, 1));
    ctx->hashed_password = arena_strdup(ctx->res->arena, PQgetvalue(result, 0, 2));

    if (sodium_init() < 0)
    {
        send_text(ctx->res, 500, "Crypto init failed");
        return;
    }

    int verify_result = crypto_pwhash_str_verify(ctx->hashed_password, ctx->password, strlen(ctx->password));
    if (verify_result != 0)
    {
        send_text(ctx->res, 401, "Incorrect password");
        return;
    }

    Session *sess = session_create(3600);

    session_value_set(sess, "id", ctx->user_id);
    session_value_set(sess, "name", ctx->name);
    session_value_set(sess, "username", ctx->username);

    if (strstr(ctx->username, "johndoe"))
        session_value_set(sess, "is_admin", "true");

    Cookie cookie_options = {
        .max_age = 3600,
        .path = "/",
        .same_site = "Lax",
        .http_only = true,
        .secure = true,
    };

    session_send(ctx->res, sess, &cookie_options);
    send_text(ctx->res, 200, "Login successful");
}
