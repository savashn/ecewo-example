#include "handlers.h"
#include "context.h"
#include <stdio.h>

typedef struct
{
    Res *res;
    const char *username;
    const char *post_slug;
    bool is_author;
} ctx_t;

static void on_query_posts(PGquery *pg, PGresult *result, void *data);

void get_post(Req *req, Res *res)
{
    const char *post_slug = get_param(req, "post");
    
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req, "auth_ctx");
    if (!auth_ctx)
    {
        send_text(res, 500, "No auth context");
        return;
    }

    if (!auth_ctx->user_slug)
    {
        send_text(res, 500, "User slug not set in auth context");
        return;
    }

    if (!post_slug)
    {
        send_text(res, 400, "Post slug missing from URL parameters");
        return;
    }

    ctx_t *ctx = ecewo_alloc(res, sizeof(ctx_t));
    if (!ctx)
    {
        send_text(res, 500, "Context allocation failed");
        return;
    }

    ctx->res = res;
    ctx->username = auth_ctx->user_slug;
    ctx->post_slug = post_slug;
    ctx->is_author = auth_ctx->is_author;

    PGquery *pg = query_create(db, ctx);
    if (!pg)
    {
        send_text(res, 500, "Database connection error");
        return;
    }

    const char *select_sql;
    
    if (ctx->is_author)
    {
        select_sql = "SELECT p.id, p.header, p.slug, p.content, p.reading_time, "
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
        select_sql = "SELECT p.id, p.header, p.slug, p.content, p.reading_time, "
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
    
    int qr = query_queue(pg, select_sql, 2, params, on_query_posts, ctx);
    if (qr != 0)
    {
        send_text(res, 500, "Failed to queue query");
        return;
    }

    int exec_result = query_execute(pg);
    if (exec_result != 0)
    {
        send_text(res, 500, "Failed to execute query");
        return;
    }
}

static void on_query_posts(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result)
    {
        send_text(ctx->res, NOT_FOUND, "Post not found");
        return;
    }

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_TUPLES_OK)
    {
        send_text(ctx->res, NOT_FOUND, "Post not found");
        return;
    }

    int ntuples = PQntuples(result);

    if (ntuples == 0)
    {
        send_text(ctx->res, NOT_FOUND, "Post not found");
        return;
    }

    cJSON *response = cJSON_CreateObject();
    if (!response)
    {
        send_text(ctx->res, 500, "JSON could not be created");
        return;
    }

    char *id_val = PQgetvalue(result, 0, PQfnumber(result, "id"));
    char *reading_time_val = PQgetvalue(result, 0, PQfnumber(result, "reading_time"));
    char *author_id_val = PQgetvalue(result, 0, PQfnumber(result, "author_id"));

    cJSON_AddNumberToObject(response, "id", atoi(id_val));
    cJSON_AddNumberToObject(response, "reading_time", atoi(reading_time_val));
    cJSON_AddNumberToObject(response, "author_id", atoi(author_id_val));

    char *header_val = PQgetvalue(result, 0, PQfnumber(result, "header"));
    cJSON_AddStringToObject(response, "header", header_val);

    char *slug_val = PQgetvalue(result, 0, PQfnumber(result, "slug"));
    cJSON_AddStringToObject(response, "slug", slug_val);

    char *content_val = PQgetvalue(result, 0, PQfnumber(result, "content"));
    cJSON_AddStringToObject(response, "content", content_val);

    char *username_val = PQgetvalue(result, 0, PQfnumber(result, "username"));
    cJSON_AddStringToObject(response, "username", username_val);

    char *created_at_val = PQgetvalue(result, 0, PQfnumber(result, "created_at"));
    cJSON_AddStringToObject(response, "created_at", created_at_val);

    char *updated_at_val = PQgetvalue(result, 0, PQfnumber(result, "updated_at"));
    cJSON_AddStringToObject(response, "updated_at", updated_at_val);

    char *is_hidden_val = PQgetvalue(result, 0, PQfnumber(result, "is_hidden"));
    bool is_hidden = (strcmp(is_hidden_val, "t") == 0);
    
    cJSON_AddBoolToObject(response, "is_hidden", is_hidden);

    char *cats = PQgetvalue(result, 0, PQfnumber(result, "categories"));
    char *slugs = PQgetvalue(result, 0, PQfnumber(result, "category_slugs"));
    char *ids = PQgetvalue(result, 0, PQfnumber(result, "category_ids"));

    cJSON *arr = cJSON_CreateArray();
    if (!arr)
    {
        send_text(ctx->res, 500, "JSON Array could not be created");
        cJSON_Delete(response);
        return;
    }

    if (cats && *cats)
    {
        char *c_copy = ecewo_strdup(ctx->res, cats);
        char *s_copy = ecewo_strdup(ctx->res, slugs);
        char *i_copy = ecewo_strdup(ctx->res, ids);
        if (!c_copy || !s_copy || !i_copy)
        {
            cJSON_Delete(arr);
            cJSON_Delete(response);
            send_text(ctx->res, 500, "Error");
            return;
        }

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
    }

    cJSON_AddItemToObject(response, "categories", arr);

    char *json_str = cJSON_PrintUnformatted(response);
    if (!json_str)
    {
        cJSON_Delete(response);
        send_text(ctx->res, 500, "Error while printing the JSON object");
        return;
    }

    send_json(ctx->res, OK, json_str);
    free(json_str);
    cJSON_Delete(response);
}
