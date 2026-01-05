#include <stdlib.h>
#include <stdio.h>
#include <sodium.h>
#include "handlers.h"
#include "ecewo-session.h"

typedef struct
{
    Res *res;
    char *name;
    char *username;
    char *password;
    char *email;
    char *about;
    char *hashpw;
} ctx_t;

static void check_user_exists(PGquery *pg, PGresult *result, void *data);
static void add_user_result(PGquery *pg, PGresult *result, void *data);

void add_user(Req *req, Res *res)
{
    cJSON *json = cJSON_Parse(req->body);
    if (!json)
    {
        send_text(res, 400, "Invalid JSON");
        return;
    }

    cJSON *j_name = cJSON_GetObjectItem(json, "name");
    cJSON *j_username = cJSON_GetObjectItem(json, "username");
    cJSON *j_password = cJSON_GetObjectItem(json, "password");
    cJSON *j_email = cJSON_GetObjectItem(json, "email");
    cJSON *j_about = cJSON_GetObjectItem(json, "about");

    if (!cJSON_IsString(j_name) ||
        !cJSON_IsString(j_username) ||
        !cJSON_IsString(j_password) ||
        !cJSON_IsString(j_email))
    {
        cJSON_Delete(json);
        send_text(res, 400, "Missing or invalid fields");
        return;
    }

    const char *name = j_name->valuestring;
    const char *username = j_username->valuestring;
    const char *password = j_password->valuestring;
    const char *email = j_email->valuestring;
    const char *about = cJSON_IsString(j_about) ? j_about->valuestring : "";

    ctx_t *ctx = arena_alloc(res->arena, sizeof(ctx_t));
    if (!ctx)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Context allocation failed");
        return;
    }

    ctx->res = res;
    ctx->name = arena_strdup(res->arena, name);
    ctx->username = arena_strdup(res->arena, username);
    ctx->password = arena_strdup(res->arena, password);
    ctx->email = arena_strdup(res->arena, email);
    ctx->about = arena_strdup(res->arena, about);

    if (!ctx->name || !ctx->username || !ctx->password || !ctx->email || !ctx->about)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Memory allocation failed");
        return;
    }

    ctx->hashpw = arena_alloc(res->arena, crypto_pwhash_STRBYTES);
    if (!ctx->hashpw)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Memory allocation failed");
        return;
    }

    if (crypto_pwhash_str(
            ctx->hashpw,
            password, strlen(password),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Password hashing failed");
        return;
    }

    cJSON_Delete(json);

    PGquery *pg = pg_query_create(db_get_pool(), res->arena);
    if (!pg)
    {
        send_text(res, 500, "Failed to create async DB context");
        return;
    }

    const char *check_sql =
        "SELECT COUNT(*) FROM users WHERE username = $1 OR email = $2;";

    const char *check_params[2] = {
        ctx->username,
        ctx->email};

    if (pg_query_queue(pg, check_sql, 2, check_params, check_user_exists, ctx) != 0)
    {
        send_text(res, 500, "Failed to queue database query");
        return;
    }

    if (pg_query_exec(pg) != 0)
    {
        send_text(res, 500, "Failed to execute database query");
        return;
    }
}

static void check_user_exists(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_TUPLES_OK)
    {
        printf("check_user_exists: DB check failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Database check failed");
        return;
    }

    char *count_str = PQgetvalue(result, 0, 0);
    int count = atoi(count_str);

    if (count > 0)
    {
        send_text(ctx->res, 409, "Username or email already exists");
        return;
    }

    const char *insert_sql =
        "INSERT INTO users "
        "(name, username, password, email, about) "
        "VALUES ($1, $2, $3, $4, $5);";

    const char *insert_params[5] = {
        ctx->name,
        ctx->username,
        ctx->hashpw,
        ctx->email,
        ctx->about};

    if (pg_query_queue(pg, insert_sql, 5, insert_params, add_user_result, ctx) != 0)
    {
        send_text(ctx->res, 500, "Failed to queue insert query");
        return;
    }
}

static void add_user_result(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    ExecStatusType status = PQresultStatus(result);

    if (status == PGRES_COMMAND_OK)
    {
        send_text(ctx->res, 201, "User created!");
    }
    else
    {
        printf("add_user_result: DB insert failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "DB insert failed");
    }
}
