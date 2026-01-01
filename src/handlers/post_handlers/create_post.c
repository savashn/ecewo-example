#include "handlers.h"
#include "db.h"
#include "context.h"
#include "utils.h"
#include "slugify.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    Res *res;
    char *header;
    char *content;
    char *slug;
    int reading_time;
    char *author_id;
    int created_at;
    int updated_at;
    bool is_hidden;
    int *category_ids;
    int category_count;
    char *batch_sql;
} ctx_t;

static void on_query_post(PGquery *pg, PGresult *result, void *data);
static void on_post_created(PGquery *pg, PGresult *result, void *data);
static void insert_post_result(PGquery *pg, PGresult *result, void *data);

void create_post(Req *req, Res *res)
{
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req, "auth_ctx");

    cJSON *json = cJSON_Parse(req->body);
    if (!json)
    {
        send_text(res, 400, "Invalid JSON");
        return;
    }

    const cJSON *jheader = cJSON_GetObjectItem(json, "header");
    const cJSON *jcontent = cJSON_GetObjectItem(json, "content");
    const cJSON *jis_hidden = cJSON_GetObjectItem(json, "is_hidden");

    if (!jheader || !jcontent || !jheader->valuestring || !jcontent->valuestring)
    {
        cJSON_Delete(json);
        send_text(res, 400, "Header or content is missing");
        return;
    }

    const char *author_id = auth_ctx->id;
    const char *header = jheader->valuestring;
    const char *content = jcontent->valuestring;
    bool is_hidden = jis_hidden ? jis_hidden->valueint : false;

    char *slug = slugify(header, NULL);
    if (!slug)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Memory allocation error in slugify");
        return;
    }

    int reading_time = compute_reading_time(content);

    // Allocate async context in the response arena
    ctx_t *ctx = arena_alloc(res->arena, sizeof(ctx_t));
    if (!ctx)
    {
        free(slug);
        cJSON_Delete(json);
        send_text(res, 500, "Context allocation failed");
        return;
    }

    ctx->res = res;
    ctx->header = arena_strdup(res->arena, header);
    ctx->content = arena_strdup(res->arena, content);
    ctx->slug = arena_strdup(res->arena, slug);
    ctx->author_id = arena_strdup(res->arena, author_id);
    ctx->reading_time = reading_time;
    ctx->created_at = (int)time(NULL);
    ctx->updated_at = ctx->created_at;
    ctx->is_hidden = is_hidden;

    free(slug);

    if (!ctx->header || !ctx->content || !ctx->slug || !ctx->author_id)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Memory allocation failed");
        return;
    }

    // Initialize defaults for categories
    ctx->category_ids = NULL;
    ctx->category_count = 0;

    // Process categories
    const cJSON *jcategories = cJSON_GetObjectItem(json, "categories");
    if (jcategories && cJSON_IsArray(jcategories))
    {
        int n = cJSON_GetArraySize(jcategories);
        if (n > 0)
        {
            ctx->category_ids = arena_alloc(res->arena, n * sizeof(int));
            if (!ctx->category_ids)
            {
                cJSON_Delete(json);
                send_text(res, 500, "Memory allocation failed for categories");
                return;
            }

            ctx->category_count = 0;
            for (int i = 0; i < n; i++)
            {
                const cJSON *item = cJSON_GetArrayItem(jcategories, i);
                if (cJSON_IsNumber(item))
                {
                    ctx->category_ids[ctx->category_count] = item->valueint;
                    ctx->category_count++;
                }
            }

            if (ctx->category_count == 0)
                ctx->category_ids = NULL;
        }
    }

    cJSON_Delete(json);

    PGquery *pg = pg_query(db_get_pool(), res->arena);
    if (!pg)
    {
        send_text(res, 500, "Database connection error");
        return;
    }

    const char *select_sql = "SELECT 1 FROM posts WHERE slug = $1";
    const char *params[] = {ctx->slug};

    int query_result = pg_query_queue(pg, select_sql, 1, params, on_query_post, ctx);
    if (query_result != 0)
    {
        printf("ERROR: Failed to queue query, result=%d\n", query_result);
        send_text(res, 500, "Failed to queue query");
        return;
    }

    int exec_result = pg_query_exec(pg);
    if (exec_result != 0)
    {
        printf("ERROR: Failed to execute, result=%d\n", exec_result);
        send_text(res, 500, "Failed to execute query");
        return;
    }
}

static void on_query_post(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result)
    {
        printf("ERROR: Result is NULL\n");
        send_text(ctx->res, 500, "Result not found");
        return;
    }

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_TUPLES_OK)
    {
        printf("on_query_post: DB check failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Database check failed");
        return;
    }

    if (PQntuples(result) > 0)
    {
        printf("on_query_post: Post with this slug already exists\n");
        send_text(ctx->res, 409, "This post already exists");
        return;
    }
    printf("on_query_post: No duplicate found, proceeding with insert\n");

    // Prepare parameters for insert
    char *reading_time_str = arena_sprintf(ctx->res->arena, "%d", ctx->reading_time);
    char *created_at_str = arena_sprintf(ctx->res->arena, "%d", ctx->created_at);
    char *updated_at_str = arena_sprintf(ctx->res->arena, "%d", ctx->updated_at);
    char *is_hidden_str = arena_sprintf(ctx->res->arena, "%s", ctx->is_hidden ? "true" : "false");

    const char *insert_params[8] = {
        ctx->header,
        ctx->slug,
        ctx->content,
        reading_time_str,
        ctx->author_id,
        created_at_str,
        updated_at_str,
        is_hidden_str};

    const char *insert_sql =
        "INSERT INTO posts "
        "(header, slug, content, reading_time, author_id, created_at, updated_at, is_hidden) "
        "VALUES ($1, $2, $3, $4, $5, to_timestamp($6), to_timestamp($7), $8) "
        "RETURNING id; ";

    if (pg_query_queue(pg, insert_sql, 8, insert_params, on_post_created, ctx) != 0)
    {
        send_text(ctx->res, 500, "Failed to queue insert query");
        return;
    }

    printf("on_query_post: Insert operation queued\n");
}

static void on_post_created(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result || PQresultStatus(result) != PGRES_TUPLES_OK)
    {
        printf("on_post_created: Post insert failed\n");
        send_text(ctx->res, 500, "DB insert failed");
        return;
    }

    int post_id = atoi(PQgetvalue(result, 0, 0));

    if (ctx->category_count == 0)
    {
        // No categories - let completion callback handle success response
        return;
    }

    size_t sql_len = 256 + (ctx->category_count * 32);
    ctx->batch_sql = arena_alloc(ctx->res->arena, sql_len);

    if (!ctx->batch_sql)
    {
        send_text(ctx->res, 500, "Memory allocation failed");
        return;
    }

    strcpy(ctx->batch_sql, "INSERT INTO post_categories (post_id, category_id) VALUES ");

    for (int i = 0; i < ctx->category_count; i++)
    {
        if (i > 0)
        {
            strcat(ctx->batch_sql, ", ");
        }

        char *temp_values = arena_sprintf(ctx->res->arena, "(%d, %d)", post_id, ctx->category_ids[i]);
        strcat(ctx->batch_sql, temp_values);
    }
    strcat(ctx->batch_sql, " ON CONFLICT DO NOTHING;");

    printf("on_post_created: Executing batch SQL: %s\n", ctx->batch_sql);

    if (pg_query_queue(pg, ctx->batch_sql, 0, NULL, insert_post_result, ctx) != 0)
    {
        send_text(ctx->res, 500, "Failed to queue batch category insert");
        return;
    }

    printf("on_post_created: Batch category insert queued for %d categories\n", ctx->category_count);
}

static void insert_post_result(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_COMMAND_OK)
    {
        printf("insert_post_result: Batch category insert failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Category insert failed");
        return;
    }

    printf("insert_post_result: Post created successfully with all categories\n");
    send_text(ctx->res, 201, "Post created successfully");
}
