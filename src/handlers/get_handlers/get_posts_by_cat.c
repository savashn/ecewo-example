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
        destroy_res(ctx->res);

    free(ctx);
}

void on_result(pg_async_t *pg, PGresult *result, void *data);

void get_posts_by_cat(Req *req, Res *res)
{
    const char *category = get_query(req, "category");

    if (!category || strlen(category) == 0)
    {
        send_text(res, 400, "Category parameter is required");
        return;
    }

    auth_context_t *auth_ctx = (auth_context_t *)get_context(req);

    ctx_t *ctx = calloc(1, sizeof(ctx_t));
    if (!ctx)
    {
        send_text(res, 500, "Memory allocation error");
        return;
    }

    Res *copy = copy_res(res);
    ctx->res = copy;
    ctx->is_author = auth_ctx->is_author;

    pg_async_t *pg = pquv_create(db, ctx);
    if (!pg)
    {
        free_ctx(ctx);
        send_text(res, 500, "Database connection error");
        return;
    }

    char *select_sql =
        "SELECT p.id, p.header, p.slug, p.reading_time, "
        "       p.author_id, u.username, p.created_at, p.updated_at, p.is_hidden, "
        "       COALESCE(string_agg(c.category, ','), '') as categories, "
        "       COALESCE(string_agg(c.slug, ','), '') as category_slugs, "
        "       COALESCE(string_agg(c.id::text, ','), '') as category_ids "
        "FROM posts p "
        "JOIN users u ON p.author_id = u.id "
        "JOIN post_categories pc ON p.id = pc.post_id "
        "JOIN categories c ON pc.category_id = c.id "
        "WHERE u.username = $1 AND c.slug = $2 "
        "GROUP BY p.id, u.username "
        "ORDER BY p.created_at DESC";

    const char *params[] = {auth_ctx->user_slug, category};

    int qr = pquv_queue(pg, select_sql, 2, params, on_result, ctx);
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

void on_result(pg_async_t *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!ctx || !ctx->res)
    {
        printf("ERROR: on_query_posts: Invalid context\n");
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

    if (PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        send_text(ctx->res, 500, "DB select failed");
        free_ctx(ctx);
        return;
    }

    int rows = PQntuples(result);

    cJSON *root = cJSON_CreateObject();
    cJSON *posts = cJSON_CreateArray();

    // If no any rows, return an empty array
    if (rows == 0)
    {
        cJSON_AddItemToObject(root, "posts", posts);
        char *out = cJSON_PrintUnformatted(root);
        send_json(ctx->res, 200, out);
        cJSON_Delete(root);
        free(out);
        free_ctx(ctx);
        return;
    }

    for (int i = 0; i < rows; i++)
    {
        bool hidden = (PQgetvalue(result, i, PQfnumber(result, "is_hidden"))[0] == 't');

        if (!ctx->is_author && hidden)
        {
            continue;
        }

        cJSON *obj = cJSON_CreateObject();
        ADD_STR_ROW(obj, header, i);
        ADD_STR_ROW(obj, slug, i);
        ADD_INT_ROW(obj, reading_time, i);
        ADD_INT_ROW(obj, author_id, i);
        ADD_STR_ROW(obj, username, i);
        ADD_STR_ROW(obj, created_at, i);
        ADD_STR_ROW(obj, updated_at, i);
        ADD_BOOL_ROW(obj, is_hidden, i);

        ADD_STR_ROW(obj, categories, i);
        ADD_STR_ROW(obj, category_slugs, i);
        ADD_STR_ROW(obj, category_ids, i);

        cJSON_AddItemToArray(posts, obj);
    }

    cJSON_AddItemToObject(root, "posts", posts);
    char *out = cJSON_PrintUnformatted(root);
    send_json(ctx->res, 200, out);

    cJSON_Delete(root);
    free(out);
    free_ctx(ctx);
}
