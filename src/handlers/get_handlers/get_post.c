#include "handlers.h"
#include "context.h"

typedef struct
{
    Res *res;
    char *username;
    char *post_slug;
} ctx_t;

static void free_ctx(ctx_t *ctx)
{
    if (!ctx)
        return;
    if (ctx->res)
        destroy_res(ctx->res);
    if (ctx->username)
        free(ctx->username);
    if (ctx->post_slug)
        free(ctx->post_slug);
    free(ctx);
}

static void on_query_posts(pg_async_t *pg, PGresult *result, void *data);

void get_post(Req *req, Res *res)
{
    const char *post_slug = get_params(req, "post");

    auth_context_t *auth_ctx = (auth_context_t *)get_context(req);
    const char *user_slug = auth_ctx->user_slug;
    bool is_author = auth_ctx->is_author;

    ctx_t *ctx = calloc(1, sizeof(ctx_t));
    if (!ctx)
    {
        send_text(res, 500, "Memory allocation error");
        return;
    }

    Res *copy = copy_res(res);
    ctx->res = copy;
    ctx->username = strdup(user_slug);
    ctx->post_slug = strdup(post_slug);

    pg_async_t *pg = pquv_create(db, ctx);
    if (!pg)
    {
        free_ctx(ctx);
        send_text(res, 500, "Database connection error");
        return;
    }

    char *select_sql;

    if (is_author)
    {
        select_sql =
            "SELECT p.id, p.header, p.slug, p.content, p.reading_time, "
            "       p.author_id, u.username, p.created_at, p.updated_at, p.is_hidden, "
            "       COALESCE(string_agg(c.category, ','), '') as categories, "
            "       COALESCE(string_agg(c.slug, ','), '') as category_slugs, "
            "       COALESCE(string_agg(c.id::text, ','), '') as category_ids "
            "FROM posts p "
            "JOIN users u ON p.author_id = u.id "
            "LEFT JOIN post_categories pc ON p.id = pc.post_id "
            "LEFT JOIN categories c ON pc.category_id = c.id "
            "WHERE u.username = $1 AND p.slug = $2 "
            "GROUP BY p.id, u.username";
    }
    else
    {
        select_sql =
            "SELECT p.id, p.header, p.slug, p.content, p.reading_time, "
            "       p.author_id, u.username, p.created_at, p.updated_at, p.is_hidden, "
            "       COALESCE(string_agg(c.category, ','), '') as categories, "
            "       COALESCE(string_agg(c.slug, ','), '') as category_slugs, "
            "       COALESCE(string_agg(c.id::text, ','), '') as category_ids "
            "FROM posts p "
            "JOIN users u ON p.author_id = u.id "
            "LEFT JOIN post_categories pc ON p.id = pc.post_id "
            "LEFT JOIN categories c ON pc.category_id = c.id "
            "WHERE u.username = $1 AND p.slug = $2 AND p.is_hidden = FALSE "
            "GROUP BY p.id, u.username";
    }

    const char *params[] = {ctx->username, ctx->post_slug};

    int qr = pquv_queue(pg, select_sql, 2, params, on_query_posts, ctx);
    if (qr != 0)
    {
        printf("ERROR: Failed to queue query, result=%d\n", qr);
        free_ctx(ctx);
        send_text(res, 500, "Failed to queue query");
        return;
    }

    int exec_result = pquv_execute(pg);
    if (exec_result != 0)
    {
        printf("ERROR: Failed to execute, result=%d\n", exec_result);
        free_ctx(ctx);
        send_text(res, 500, "Failed to execute query");
        return;
    }
}

static void on_query_posts(pg_async_t *pg, PGresult *result, void *data)
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

    if (PQntuples(result) == 0)
    {
        send_text(ctx->res, NOT_FOUND, "Post not found");
        free_ctx(ctx);
        return;
    }

    cJSON *response = cJSON_CreateObject();

    // Add integer fields
    cJSON_AddNumberToObject(response, "id", atoi(PQgetvalue(result, 0, PQfnumber(result, "id"))));
    cJSON_AddNumberToObject(response, "reading_time", atoi(PQgetvalue(result, 0, PQfnumber(result, "reading_time"))));
    cJSON_AddNumberToObject(response, "author_id", atoi(PQgetvalue(result, 0, PQfnumber(result, "author_id"))));

    // Add string fields
    cJSON_AddStringToObject(response, "header", PQgetvalue(result, 0, PQfnumber(result, "header")));
    cJSON_AddStringToObject(response, "slug", PQgetvalue(result, 0, PQfnumber(result, "slug")));
    cJSON_AddStringToObject(response, "content", PQgetvalue(result, 0, PQfnumber(result, "content")));
    cJSON_AddStringToObject(response, "username", PQgetvalue(result, 0, PQfnumber(result, "username")));
    cJSON_AddStringToObject(response, "created_at", PQgetvalue(result, 0, PQfnumber(result, "created_at")));
    cJSON_AddStringToObject(response, "updated_at", PQgetvalue(result, 0, PQfnumber(result, "updated_at")));

    // Add boolean field
    cJSON_AddBoolToObject(response, "is_hidden", strcmp(PQgetvalue(result, 0, PQfnumber(result, "is_hidden")), "t") == 0);

    char *cats = PQgetvalue(result, 0, PQfnumber(result, "categories"));
    char *slugs = PQgetvalue(result, 0, PQfnumber(result, "category_slugs"));
    char *ids = PQgetvalue(result, 0, PQfnumber(result, "category_ids"));
    cJSON *arr = cJSON_CreateArray();

    if (cats && *cats)
    {
        char *c_copy = strdup(cats), *s_copy = strdup(slugs), *i_copy = strdup(ids);
        char *ctok, *stok, *itok;
        char *save1, *save2, *save3;

        ctok = strtok_r(c_copy, ",", &save1);
        stok = strtok_r(s_copy, ",", &save2);
        itok = strtok_r(i_copy, ",", &save3);

        while (ctok && stok && itok)
        {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "id", atoi(itok));
            cJSON_AddStringToObject(o, "category", ctok);
            cJSON_AddStringToObject(o, "slug", stok);
            cJSON_AddItemToArray(arr, o);

            ctok = strtok_r(NULL, ",", &save1);
            stok = strtok_r(NULL, ",", &save2);
            itok = strtok_r(NULL, ",", &save3);
        }
        free(c_copy);
        free(s_copy);
        free(i_copy);
    }

    cJSON_AddItemToObject(response, "categories", arr);

    char *json_str = cJSON_PrintUnformatted(response);
    send_json(ctx->res, OK, json_str);

    free(json_str);
    cJSON_Delete(response);
    free_ctx(ctx);
}
