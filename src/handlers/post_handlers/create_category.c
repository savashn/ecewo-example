#include "handlers.h"
#include "db.h"
#include "connection.h"
#include "context.h"
#include "utils.h"
#include <time.h>
#include "slugify.h"

typedef struct
{
    Res *res;
    char *category;
    char *slug;
    char *author_id;
} ctx_t;

static void free_ctx(ctx_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->res)
        destroy_res(ctx->res);
    if (ctx->category)
        free(ctx->category);
    if (ctx->slug)
        free(ctx->slug);
    if (ctx->author_id)
        free(ctx->author_id);

    free(ctx);
}

static void on_query_category(pg_async_t *pg, PGresult *result, void *data);
static void insert_category_result(pg_async_t *pg, PGresult *result, void *data);

void create_category(Req *req, Res *res)
{
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req);

    cJSON *json = cJSON_Parse(req->body);
    if (!json)
    {
        send_text(res, 400, "Invalid JSON");
        return;
    }

    const cJSON *jcategory = cJSON_GetObjectItem(json, "category");

    if (!jcategory || !jcategory->valuestring)
    {
        cJSON_Delete(json);
        send_text(res, 400, "Category field is missing");
        return;
    }

    const char *author_id = auth_ctx->id;
    const char *category = jcategory->valuestring;

    char *slug = slugify(category, NULL);

    ctx_t *ctx = calloc(1, sizeof(ctx_t));
    if (!ctx)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Memory allocation error");
        return;
    }

    Res *copy = copy_res(res);
    ctx->res = copy;
    ctx->category = strdup(category);
    ctx->slug = strdup(slug);
    ctx->author_id = strdup(author_id);

    free(slug);

    if (!ctx->category || !ctx->author_id)
    {
        free_ctx(ctx);
        cJSON_Delete(json);
        send_text(res, 500, "Memory allocation failed");
        return;
    }

    cJSON_Delete(json);

    pg_async_t *pg = pquv_create(db, ctx);
    if (!pg)
    {
        free_ctx(ctx);
        send_text(res, 500, "Database connection error");
        return;
    }

    const char *select_sql = "SELECT 1 FROM categories WHERE slug = $1";
    const char *params[] = {ctx->slug};

    int query_result = pquv_queue(pg, select_sql, 1, params, on_query_category, ctx);
    if (query_result != 0)
    {
        printf("ERROR: Failed to queue query, result=%d\n", query_result);
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

static void on_query_category(pg_async_t *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!ctx || !ctx->res)
    {
        printf("on_query_category: Invalid context\n");
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

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_TUPLES_OK)
    {
        printf("on_query_category: DB check failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Database check failed");
        free_ctx(ctx);
        return;
    }

    // Check if category with this slug already exists
    if (PQntuples(result) > 0)
    {
        printf("on_query_category: Category with this slug already exists\n");
        send_text(ctx->res, 409, "This category already exists");
        free_ctx(ctx);
        return;
    }

    const char *insert_params[3] = {
        ctx->category,
        ctx->slug,
        ctx->author_id,
    };

    const char *insert_sql =
        "INSERT INTO categories "
        "(category, slug, author_id) "
        "VALUES ($1, $2, $3);";

    // Queue the async insert query using the same pg context
    if (pquv_queue(pg, insert_sql, 3, insert_params, insert_category_result, ctx) != 0)
    {
        send_text(ctx->res, 500, "Failed to queue insert query");
        free_ctx(ctx);
        return;
    }

    printf("on_query_category: Insert operation queued\n");
}

static void insert_category_result(pg_async_t *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!ctx || !ctx->res)
    {
        printf("insert_category_result: Invalid context\n");
        if (ctx)
            free_ctx(ctx);
        return;
    }

    ExecStatusType status = PQresultStatus(result);

    if (status == PGRES_COMMAND_OK)
    {
        printf("insert_category_result: Category created successfully\n");
        send_text(ctx->res, 201, "Category created!");
    }
    else
    {
        printf("insert_category_result: DB insert failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "DB insert failed");
    }

    free_ctx(ctx);
}
