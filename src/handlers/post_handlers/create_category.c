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

    ctx_t *ctx = arena_alloc(req->arena, sizeof(ctx_t));
    if (!ctx)
    {
        free(slug);
        cJSON_Delete(json);
        send_text(res, 500, "Context allocation failed");
        return;
    }

    ctx->res = res;
    ctx->category = arena_strdup(ctx->res->arena, category);
    ctx->slug = arena_strdup(ctx->res->arena, slug);
    ctx->author_id = arena_strdup(ctx->res->arena, author_id);

    free(slug);
    cJSON_Delete(json);

    if (!ctx->category || !ctx->slug || !ctx->author_id)
    {
        send_text(res, 500, "Memory allocation failed");
        return;
    }

    PGquery *pg = pg_query_create(db_get_pool(), res->arena);
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

    if (pg_query_queue(pg, conditional_insert_sql, 3, params, on_category_insert, ctx) != 0)
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

static void on_category_insert(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_COMMAND_OK)
    {
        printf("on_category_insert: DB operation failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Database operation failed");
        return;
    }

    char *affected_rows = PQcmdTuples(result);
    int rows_inserted = atoi(affected_rows);

    if (rows_inserted == 0)
    {
        send_text(ctx->res, 409, "This category already exists");
    }
    else if (rows_inserted == 1)
    {
        send_text(ctx->res, 201, "Category created!");
    }
    else
    {
        send_text(ctx->res, 500, "Unexpected database result");
    }
}
