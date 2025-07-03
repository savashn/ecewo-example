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
    char *original_slug;
    char *new_slug;
    char *author_id;
} ctx_t;

static void free_ctx(ctx_t *ctx)
{
    if (!ctx)
        return;

    if (ctx->res)
        free(ctx->res);
    if (ctx->category)
        free(ctx->category);
    if (ctx->original_slug)
        free(ctx->original_slug);
    if (ctx->new_slug)
        free(ctx->new_slug);
    if (ctx->author_id)
        free(ctx->author_id);

    free(ctx);
}

static void on_query_category(pg_async_t *pg, PGresult *result, void *data);
static void on_check_new_slug(pg_async_t *pg, PGresult *result, void *data);
static void update_category(pg_async_t *pg, ctx_t *ctx);
static void on_category_updated(pg_async_t *pg, PGresult *result, void *data);

void edit_category(Req *req, Res *res)
{
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req);

    if (!auth_ctx || !auth_ctx->is_author)
    {
        send_text(401, "Not allowed");
        return;
    }

    const char *slug = get_params("category");
    if (!slug)
    {
        send_text(400, "Category slug is required");
        return;
    }

    cJSON *json = cJSON_Parse(req->body);
    if (!json)
    {
        send_text(400, "Invalid JSON");
        return;
    }

    const cJSON *jcategory = cJSON_GetObjectItem(json, "category");

    if (!jcategory || !jcategory->valuestring)
    {
        cJSON_Delete(json);
        send_text(400, "Category field is missing");
        return;
    }

    const char *author_id = auth_ctx->id;
    const char *category = jcategory->valuestring;

    char *new_slug = slugify(category, NULL);
    if (!new_slug)
    {
        cJSON_Delete(json);
        send_text(500, "Memory allocation error in slugify");
        return;
    }

    ctx_t *ctx = calloc(1, sizeof(ctx_t));
    if (!ctx)
    {
        cJSON_Delete(json);
        send_text(500, "Memory allocation error");
        return;
    }

    ctx->res = malloc(sizeof(*ctx->res));

    if (!ctx->res)
    {
        free_ctx(ctx);
        cJSON_Delete(json);
        send_text(500, "Memory allocation failed");
        return;
    }

    *ctx->res = *res;
    ctx->category = strdup(category);
    ctx->original_slug = strdup(slug);
    ctx->new_slug = strdup(new_slug);
    ctx->author_id = strdup(author_id);

    free(new_slug);

    if (!ctx->category || !ctx->author_id)
    {
        free_ctx(ctx);
        cJSON_Delete(json);
        send_text(500, "Memory allocation failed");
        return;
    }

    cJSON_Delete(json);

    pg_async_t *pg = pquv_create(db, ctx);
    if (!pg)
    {
        free_ctx(ctx);
        send_text(500, "Database connection error");
        return;
    }

    const char *select_sql = "SELECT id, author_id FROM categories WHERE slug = $1";
    const char *params[] = {ctx->original_slug};

    int query_result = pquv_queue(pg, select_sql, 1, params, on_query_category, ctx);
    if (query_result != 0)
    {
        printf("ERROR: Failed to queue query, result=%d\n", query_result);
        free_ctx(ctx);
        send_text(500, "Failed to queue query");
        return;
    }

    int exec_result = pquv_execute(pg);
    if (exec_result != 0)
    {
        printf("ERROR: Failed to execute, result=%d\n", exec_result);
        free_ctx(ctx);
        send_text(500, "Failed to execute query");
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

    Res *res = ctx->res;
    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_TUPLES_OK)
    {
        printf("on_query_category: DB check failed: %s\n", PQresultErrorMessage(result));
        send_text(500, "Database check failed");
        free_ctx(ctx);
        return;
    }

    if (PQntuples(result) == 0)
    {
        printf("on_query_category: Category not found\n");
        send_text(404, "Category not found");
        free_ctx(ctx);
        return;
    }

    if (strcmp(ctx->original_slug, ctx->new_slug) != 0)
    {
        const char *check_slug_sql = "SELECT 1 FROM categories WHERE slug = $1 AND slug != $2";
        const char *check_params[] = {ctx->new_slug, ctx->original_slug};

        if (pquv_queue(pg, check_slug_sql, 2, check_params, on_check_new_slug, ctx) != 0)
        {
            send_text(500, "Failed to queue slug check query");
            free_ctx(ctx);
            return;
        }
    }
    else
    {
        // If slug hasn't change, continue to update directly
        update_category(pg, ctx);
    }
}

static void on_check_new_slug(pg_async_t *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!ctx || !ctx->res)
    {
        printf("on_check_new_slug: Invalid context\n");
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

    Res *res = ctx->res;
    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_TUPLES_OK)
    {
        printf("on_check_new_slug: DB check failed: %s\n", PQresultErrorMessage(result));
        send_text(500, "Database check failed");
        free_ctx(ctx);
        return;
    }

    if (PQntuples(result) > 0)
    {
        printf("on_check_new_slug: New slug already exists\n");
        send_text(409, "A category with this title already exists");
        free_ctx(ctx);
        return;
    }

    update_category(pg, ctx);
}

static void update_category(pg_async_t *pg, ctx_t *ctx)
{
    Res *res = ctx->res;

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

    if (pquv_queue(pg, update_sql, 3, update_params, on_category_updated, ctx) != 0)
    {
        send_text(500, "Failed to queue update query");
        free_ctx(ctx);
        return;
    }
}

static void on_category_updated(pg_async_t *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!ctx || !ctx->res)
    {
        printf("on_category_updated: Invalid context\n");
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

    Res *res = ctx->res;
    ExecStatusType status = PQresultStatus(result);

    if (status != PGRES_TUPLES_OK)
    {
        printf("on_category_updated: Update failed: %s\n", PQresultErrorMessage(result));
        send_text(500, "Category update failed");
        free_ctx(ctx);
        return;
    }

    if (PQntuples(result) == 0)
    {
        printf("on_category_updated: No rows affected\n");
        send_text(404, "Category not found or not updated");
        free_ctx(ctx);
        return;
    }

    send_text(200, "Category updated successfully");
    free_ctx(ctx);
}
