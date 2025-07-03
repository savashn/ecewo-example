#include "middlewares.h"
#include "session.h"
#include "cJSON.h"
#include "context.h"

int body_checker(Req *req, Res *res, Chain *chain)
{
    if (!req->body)
    {
        send_text(400, "Missing request body");
        return 0;
    }

    return next(chain, req, res);
}

int is_auth(Req *req, Res *res, Chain *chain)
{
    // Get the user session
    Session *session = get_session(&req->headers);

    // Always allocate a session context, even if no session data
    auth_context_t *ctx = calloc(1, sizeof(auth_context_t));
    if (!ctx)
    {
        send_text(500, "Internal Server Error");
        return 0;
    }

    if (session && session->data)
    {
        // Parse session JSON once (session->data is a JSON string)
        cJSON *session_json = cJSON_Parse(session->data);
        if (!session_json)
        {
            free(ctx);
            send_text(500, "Error: Failed to parse session data");
            return 0;
        }

        cJSON *id_item = cJSON_GetObjectItem(session_json, "id");
        cJSON *name_item = cJSON_GetObjectItem(session_json, "name");
        cJSON *username_item = cJSON_GetObjectItem(session_json, "username");
        cJSON *admin_item = cJSON_GetObjectItem(session_json, "is_admin");

        if (!id_item || !name_item || !username_item)
        {
            cJSON_Delete(session_json);
            free(ctx);
            send_text(500, "Error: Incomplete session data");
            return 0;
        }

        ctx->id = strdup(id_item->valuestring);
        ctx->name = strdup(name_item->valuestring);
        ctx->username = strdup(username_item->valuestring);
        ctx->is_admin = false;
        if (admin_item && cJSON_IsString(admin_item))
        {
            if (strcmp(admin_item->valuestring, "true") == 0)
                ctx->is_admin = true;
        }

        cJSON_Delete(session_json);
    }
    else
    {
        ctx->id = NULL;
        ctx->name = NULL;
        ctx->username = NULL;
        ctx->is_admin = false;
    }

    // Attach context to request; cleanup will free cJSON and ctx
    set_context(req, ctx, sizeof(*ctx), cleanup_auth_ctx);

    // Continue to next handler in chain
    return next(chain, req, res);
}

int is_authors_self(Req *req, Res *res, Chain *chain)
{
    auth_context_t *auth_ctx = get_context(req);

    const char *user_slug = get_params("user");
    auth_ctx->user_slug = strdup(user_slug ? user_slug : "");

    if (!auth_ctx->user_slug)
    {
        send_text(500, "Internal Server Error");
        return 0;
    }

    if (auth_ctx && auth_ctx->username)
    {
        if (strcmp(auth_ctx->user_slug, auth_ctx->username) == 0)
            auth_ctx->is_author = true;
    }

    return next(chain, req, res);
}

int auth_only(Req *req, Res *res, Chain *chain)
{
    auth_context_t *ctx = (auth_context_t *)get_context(req);

    if (!ctx || !ctx->id || !ctx->name || !ctx->username)
    {
        send_text(401, "Not allowed");
        return 0;
    }

    return next(chain, req, res);
}
