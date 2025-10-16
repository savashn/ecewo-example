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
    char *header;
    char *content;
    char *original_slug;
    char *new_slug;
    int reading_time;
    char *author_id;
    int created_at;
    int updated_at;
    bool is_hidden;
    int *category_ids;
    int category_count;
    int completed_categories;
    char *batch_sql;
    char **batch_params;
} ctx_t;

static void on_query_post_exists(PGquery *pg, PGresult *result, void *data);
static void on_check_new_slug(PGquery *pg, PGresult *result, void *data);
static void update_post(PGquery *pg, ctx_t *ctx);
static void on_post_updated(PGquery *pg, PGresult *result, void *data);
static void clear_post_categories(PGquery *pg, ctx_t *ctx, const char *post_id);
static void update_post_categories(PGquery *pg, ctx_t *ctx, const char *post_id);
static void on_old_categories_deleted(PGquery *pg, PGresult *result, void *data);
static void insert_new_categories(PGquery *pg, ctx_t *ctx);
static void on_categories_cleared(PGquery *pg, PGresult *result, void *data);
static void on_categories_inserted(PGquery *pg, PGresult *result, void *data);

void edit_post(Req *req, Res *res)
{
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req, "auth_ctx");

    if (!auth_ctx || !auth_ctx->is_author)
    {
        send_text(res, 401, "Not allowed");
        return;
    }

    const char *slug = get_param(req, "post");
    if (!slug)
    {
        send_text(res, 400, "Post slug is required");
        return;
    }

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

    const char *header = jheader->valuestring;
    const char *content = jcontent->valuestring;
    bool is_hidden = jis_hidden ? jis_hidden->valueint : false;

    char *new_slug = slugify(header, NULL);
    if (!new_slug)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Memory allocation error in slugify");
        return;
    }

    int reading_time = compute_reading_time(content);

    // Create context to hold all the data for async operation
    ctx_t *ctx = ecewo_alloc(res, sizeof(ctx_t));
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

    ctx->header = ecewo_strdup(res, header);
    ctx->content = ecewo_strdup(res, content);
    ctx->original_slug = ecewo_strdup(res, slug);
    ctx->new_slug = ecewo_strdup(res, new_slug);
    ctx->author_id = ecewo_strdup(res, auth_ctx->id);
    ctx->reading_time = reading_time;
    ctx->updated_at = (int)time(NULL);
    ctx->is_hidden = is_hidden;

    free(new_slug);

    if (!ctx->header || !ctx->content || !ctx->original_slug || !ctx->new_slug || !ctx->author_id)
    {
        cJSON_Delete(json);
        send_text(res, 500, "Memory allocation failed");
        return;
    }

    ctx->category_ids = NULL;
    ctx->category_count = 0;

    // Process categories
    const cJSON *jcategories = cJSON_GetObjectItem(json, "categories");
    if (jcategories && cJSON_IsArray(jcategories))
    {
        int n = cJSON_GetArraySize(jcategories);
        if (n > 0)
        {
            ctx->category_ids = ecewo_alloc(res, n * sizeof(int));
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

    PGquery *pg = query_create(db, ctx);
    if (!pg)
    {
        send_text(res, 500, "Database connection error");
        return;
    }

    // First, check if the post exists and the user has it
    const char *select_sql = "SELECT id, author_id FROM posts WHERE slug = $1";
    const char *params[] = {ctx->original_slug};

    int query_result = query_queue(pg, select_sql, 1, params, on_query_post_exists, ctx);
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

static void on_query_post_exists(PGquery *pg, PGresult *result, void *data)
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
        printf("on_query_post_exists: DB check failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Database check failed");
        return;
    }

    if (PQntuples(result) == 0)
    {
        printf("on_query_post_exists: Post not found\n");
        send_text(ctx->res, 404, "Post not found");
        return;
    }

    // Check if the post belongs to the user
    const char *post_author_id = PQgetvalue(result, 0, 1);
    if (strcmp(post_author_id, ctx->author_id) != 0)
    {
        printf("on_query_post_exists: User is not the author of this post\n");
        send_text(ctx->res, 403, "You can only edit your own posts");
        return;
    }

    // If slug has change, check if the new slug has been occupied
    if (strcmp(ctx->original_slug, ctx->new_slug) != 0)
    {
        const char *check_slug_sql = "SELECT 1 FROM posts WHERE slug = $1 AND slug != $2";
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
        update_post(pg, ctx);
    }
}

static void on_check_new_slug(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result)
    {
        printf("ERROR: Result is NULL\n");
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
        send_text(ctx->res, 409, "A post with this title already exists");
        return;
    }

    update_post(pg, ctx);
}

static void update_post(PGquery *pg, ctx_t *ctx)
{
    // Prepare parameters for update
    char reading_time_str[32], updated_at_str[32];
    char is_hidden_str[8];

    snprintf(reading_time_str, sizeof(reading_time_str), "%d", ctx->reading_time);
    snprintf(updated_at_str, sizeof(updated_at_str), "%d", ctx->updated_at);
    snprintf(is_hidden_str, sizeof(is_hidden_str), "%s", ctx->is_hidden ? "true" : "false");

    const char *update_params[7] = {
        ctx->header,
        ctx->new_slug,
        ctx->content,
        reading_time_str,
        updated_at_str,
        is_hidden_str,
        ctx->original_slug // for WHERE condition
    };

    const char *update_sql =
        "UPDATE posts SET "
        "header = $1, "
        "slug = $2, "
        "content = $3, "
        "reading_time = $4, "
        "updated_at = to_timestamp($5), "
        "is_hidden = $6 "
        "WHERE slug = $7 "
        "RETURNING id;";

    if (query_queue(pg, update_sql, 7, update_params, on_post_updated, ctx) != 0)
    {
        send_text(ctx->res, 500, "Failed to queue update query");
        return;
    }
}

static void on_post_updated(PGquery *pg, PGresult *result, void *data)
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
        printf("on_post_updated: Update failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Post update failed");
        return;
    }

    if (PQntuples(result) == 0)
    {
        printf("on_post_updated: No rows affected\n");
        send_text(ctx->res, 404, "Post not found or not updated");
        return;
    }

    // If referenced with categories, update them
    if (ctx->category_ids && ctx->category_count > 0)
    {
        const char *post_id = PQgetvalue(result, 0, 0);
        update_post_categories(pg, ctx, post_id);
    }
    else
    {
        // If no categories sent, delete the existing categories and create new ones
        const char *post_id = PQgetvalue(result, 0, 0);
        clear_post_categories(pg, ctx, post_id);
    }
}

static void clear_post_categories(PGquery *pg, ctx_t *ctx, const char *post_id)
{
    const char *delete_sql = "DELETE FROM post_categories WHERE post_id = $1";
    const char *delete_params[] = {post_id};

    if (query_queue(pg, delete_sql, 1, delete_params, on_categories_cleared, ctx) != 0)
    {
        send_text(ctx->res, 500, "Failed to queue category deletion");
        return;
    }
}

static void update_post_categories(PGquery *pg, ctx_t *ctx, const char *post_id)
{
    const char *delete_sql = "DELETE FROM post_categories WHERE post_id = $1";
    const char *delete_params[] = {post_id};

    ctx->author_id = ecewo_strdup(ctx->res, post_id);
    if (!ctx->author_id)
    {
        send_text(ctx->res, 500, "Memory allocation failed");
        return;
    }

    if (query_queue(pg, delete_sql, 1, delete_params, on_old_categories_deleted, ctx) != 0)
    {
        send_text(ctx->res, 500, "Failed to queue category deletion");
        return;
    }
}

static void on_old_categories_deleted(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result)
    {
        send_text(ctx->res, 500, "Result is NULL");
        return;
    }

    ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_COMMAND_OK)
    {
        printf("on_old_categories_deleted: Delete failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Failed to delete old categories");
        return;
    }

    insert_new_categories(pg, ctx);
}

static void insert_new_categories(PGquery *pg, ctx_t *ctx)
{
    if (!ctx->category_ids || ctx->category_count == 0)
    {
        send_text(ctx->res, 200, "Post updated successfully");
        return;
    }

    size_t sql_size = 256 + (ctx->category_count * 32);
    ctx->batch_sql = ecewo_alloc(ctx->res, sql_size);
    if (!ctx->batch_sql)
    {
        send_text(ctx->res, 500, "Memory allocation failed");
        return;
    }

    strcpy(ctx->batch_sql, "INSERT INTO post_categories (post_id, category_id) VALUES ");

    ctx->batch_params = ecewo_alloc(ctx->res, ctx->category_count * 2 * sizeof(char *));
    if (!ctx->batch_params)
    {
        send_text(ctx->res, 500, "Memory allocation failed");
        return;
    }

    for (int i = 0; i < ctx->category_count; i++)
    {
        char *category_str = ecewo_sprintf(ctx->res, "%d", ctx->category_ids[i]);
        if (!category_str)
        {
            send_text(ctx->res, 500, "Memory allocation failed");
            return;
        }

        ctx->batch_params[i * 2] = ecewo_strdup(ctx->res, ctx->author_id);
        ctx->batch_params[i * 2 + 1] = category_str;

        if (!ctx->batch_params[i * 2] || !ctx->batch_params[i * 2 + 1])
        {
            send_text(ctx->res, 500, "Memory allocation failed");
            return;
        }

        if (i > 0)
            strcat(ctx->batch_sql, ", ");

        char *value_part = ecewo_sprintf(ctx->res, "($%d, $%d)", i * 2 + 1, i * 2 + 2);
        if (!value_part)
        {
            send_text(ctx->res, 500, "Memory allocation failed");
            return;
        }
        strcat(ctx->batch_sql, value_part);
    }

    if (query_queue(pg, ctx->batch_sql, ctx->category_count * 2,
                    (const char **)ctx->batch_params,
                    on_categories_inserted, ctx) != 0)
    {
        send_text(ctx->res, 500, "Failed to queue category insert");
        return;
    }
}

static void on_categories_cleared(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;
    send_text(ctx->res, 200, "Post updated successfully");
}

static void on_categories_inserted(PGquery *pg, PGresult *result, void *data)
{
    ctx_t *ctx = (ctx_t *)data;

    if (!result)
    {
        send_text(ctx->res, 500, "Result is NULL");
        return;
    }

    ExecStatusType status = PQresultStatus(result);
    if (status != PGRES_COMMAND_OK)
    {
        printf("on_categories_inserted: Insert failed: %s\n", PQresultErrorMessage(result));
        send_text(ctx->res, 500, "Failed to insert categories");
        return;
    }

    send_text(ctx->res, 200, "Post updated successfully");
}
