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
    char *original_slug;
    char *new_slug;
    char *author_id;
} ctx_t;

static void on_query_category(PGquery *pg, PGresult *result, void *data);
static void on_check_new_slug(PGquery *pg, PGresult *result, void *data);
static void update_category(PGquery *pg, ctx_t *ctx);
static void on_category_updated(PGquery *pg, PGresult *result, void *data);

void edit_category(Req *req, Res *res)
{
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req, "auth_ctx");

    if (!auth_ctx || !auth_ctx->is_author)
    {
        send_text(res, 401, "Not allowed");
        return;
    }

    const char *slug = get_param(req, "category");
    if (!slug)
    {
        send_text(res, 400, "Category slug is required");
        return;
    }

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

    char *new_slug = slugify(category, NULL);
    if (!new_slug)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Memory allocation error in slugify");
        return;
    }

    // Create context to hold all the data for async operation
    ctx_t *ctx = arena_alloc(res->arena, sizeof(ctx_t));
    if (!ctx)
    {
        cJSON_Delete(json);
        free(new_slug);
        send_text(res, 500, "Context allocation failed");
        return;
    }

    ctx->res = res;
    if (!ctx->res)
    {
        cJSON_Delete(json);
        free(new_slug);
        send_text(res, 500, "Response copy failed");
        return;
    }

    ctx->category = arena_strdup(res->arena, category);
    ctx->original_slug = arena_strdup(res->arena, slug);
    ctx->new_slug = arena_strdup(res->arena, new_slug);
    ctx->author_id = arena_strdup(res->arena, author_id);

    free(new_slug);

    if (!ctx->category || !ctx->author_id)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Memory allocation failed");
        return;
    }

    cJSON_Delete(json);

    PGquery *pg = query_create(db, res->arena);
    if (!pg)
    {
        send_text(res, 500, "Database connection error");
        return;
    }

    const char *select_sql = "SELECT id, author_id FROM categories WHERE slug = $1";
    const char *params[] = {ctx->original_slug};

    int query_result = query_queue(pg, select_sql, 1, params, on_query_category, ctx);
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
}

static void on_query_category(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result)
    {
        send_text(ctx->res, 500, "Result is NULL");
        return;
    }

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_TUPLES_OK)
    {
        printf("on_query_category: DB check failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Database check failed");
        return;
    }

    if (PQntuples(result) == 0)
    {
        printf("on_query_category: Category not found\n");
        send_text(ctx->res, 404, "Category not found");
        return;
    }

    if (strcmp(ctx->original_slug, ctx->new_slug) != 0)
    {
        const char *check_slug_sql = "SELECT 1 FROM categories WHERE slug = $1 AND slug != $2";
        const char *check_params[] = {ctx->new_slug, ctx->original_slug};

        if (query_queue(pg, check_slug_sql, 2, check_params, on_check_new_slug, ctx) != 0)
        {
            send_text(ctx->res, 500, "Failed to queue slug check query");
            return;
        }
    }
    else
    {
        // If slug hasn't change, continue to update directly
        update_category(pg, ctx);
    }
}

static void on_check_new_slug(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result)
    {
        send_text(ctx->res, 500, "Result is NULL");
        return;
    }

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_TUPLES_OK)
    {
        printf("on_check_new_slug: DB check failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Database check failed");
        return;
    }

    if (PQntuples(result) > 0)
    {
        printf("on_check_new_slug: New slug already exists\n");
        send_text(ctx->res, 409, "A category with this title already exists");
        return;
    }

    update_category(pg, ctx);
}

static void update_category(PGquery *pg, ctx_t *ctx)
{
    const char *update_params[3] = {
        ctx->category,
        ctx->new_slug,
        ctx->original_slug // for WHERE condition
    };

    const char *update_sql =
        "UPDATE categories SET "
        "category = $1, "
        "slug = $2 "
        "WHERE slug = $3 "
        "RETURNING id;";

    if (query_queue(pg, update_sql, 3, update_params, on_category_updated, ctx) != 0)
    {
        send_text(ctx->res, 500, "Failed to queue update query");
        return;
    }
}

static void on_category_updated(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result)
    {
        send_text(ctx->res, 500, "Result is NULL");
        return;
    }

    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_TUPLES_OK)
    {
        printf("on_category_updated: Update failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Category update failed");
        return;
    }

    if (PQntuples(result) == 0)
    {
        printf("on_category_updated: No rows affected\n");
        send_text(ctx->res, 404, "Category not found or not updated");
        return;
    }

    send_text(ctx->res, 200, "Category updated successfully");
}
