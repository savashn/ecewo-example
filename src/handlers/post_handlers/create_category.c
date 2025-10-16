#include "handlers.h"
#include "db.h"
#include "connection.h"
#include "context.h"
#include "utils.h"
#include "slugify.h"
#include <time.h>
#include <stdio.h>

typedef struct
{
    Res *res;
    char *category;
    char *slug;
    char *author_id;
} ctx_t;

static void on_category_insert(PGquery *pg, PGresult *result, void *data);

void create_category(Req *req, Res *res)
{
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req, "auth_ctx");

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

    // Allocate async context in the async arena
    ctx_t *ctx = ecewo_alloc(req, sizeof(ctx_t));
    if (!ctx)
    {
        free(slug);
        cJSON_Delete(json);
        send_text(res, 500, "Context allocation failed");
        return;
    }

    ctx->res = res;
    if (!ctx->res)
    {
        free(slug);
        cJSON_Delete(json);
        send_text(res, 500, "Response copy failed");
        return;
    }

    ctx->category = ecewo_strdup(ctx->res, category);
    ctx->slug = ecewo_strdup(ctx->res, slug);
    ctx->author_id = ecewo_strdup(ctx->res, author_id);

    free(slug);
    cJSON_Delete(json);

    if (!ctx->category || !ctx->slug || !ctx->author_id)
    {
        send_text(res, 500, "Memory allocation failed");
        return;
    }

    PGquery *pg = query_create(db, ctx);
    if (!pg)
    {
        send_text(res, 500, "Database connection error");
        return;
    }

    const char *conditional_insert_sql =
        "INSERT INTO categories (category, slug, author_id) "
        "SELECT $1, $2, $3 "
        "WHERE NOT EXISTS ("
        "    SELECT 1 FROM categories WHERE slug = $2"
        ");";

    const char *params[] = {ctx->category, ctx->slug, ctx->author_id};

    int query_result = query_queue(pg, conditional_insert_sql, 3, params, on_category_insert, ctx);
    if (query_result != 0)
    {
        printf("ERROR: Failed to queue query, result=%d\n", query_result);
        send_text(res, 500, "Failed to queue query");
        return;
    }

    int exec_result = query_execute(pg);
    if (exec_result != 0)
    {
        printf("ERROR: Failed to execute, result=%d\n", exec_result);
        send_text(res, 500, "Failed to execute query");
        return;
    }

    printf("create_category: Conditional INSERT query started\n");
}

static void on_category_insert(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result)
    {
        printf("ERROR: Result is NULL\n");
        send_text(ctx->res, 500, "Database error");
        return;
    }

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_COMMAND_OK)
    {
        printf("on_category_insert: DB operation failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Database operation failed");
        return;
    }

    // Check how many rows were affected
    char *affected_rows = PQcmdTuples(result);
    int rows_inserted = atoi(affected_rows);

    if (rows_inserted == 0)
    {
        // No rows inserted = category already exists
        printf("on_category_insert: Category '%s' already exists\n", ctx->category);
        send_text(ctx->res, 409, "This category already exists");
    }
    else if (rows_inserted == 1)
    {
        // One row inserted = success
        printf("on_category_insert: Category '%s' created successfully\n", ctx->category);
        send_text(ctx->res, 201, "Category created!");
    }
    else
    {
        // Unexpected result
        printf("on_category_insert: Unexpected result: %d rows affected\n", rows_inserted);
        send_text(ctx->res, 500, "Unexpected database result");
    }
}