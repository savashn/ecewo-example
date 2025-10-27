#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "middlewares.h"
#include "ecewo/session.h"
#include "cJSON.h"
#include "context.h"

int body_checker(Req *req, Res *res, Chain *chain)
{
    if (!req->body)
    {
        send_text(res, 400, "Missing request body");
        return 0;
    }

    return next(req, res, chain);
}

static bool string_to_bool(const char *str)
{
    if (!str)
        return false;
    return (strcmp(str, "true") == 0 || strcmp(str, "1") == 0);
}

int is_auth(Req *req, Res *res, Chain *chain)
{
    Session *session = session_get(req);

    auth_context_t *ctx = arena_alloc(req->arena, sizeof(auth_context_t));
    if (!ctx)
    {
        send_text(res, 500, "Internal Server Error");
        return 0;
    }

    if (session)
    {
        char *id = session_value_get(session, "id");
        char *name = session_value_get(session, "name");
        char *username = session_value_get(session, "username");
        char *is_admin_str = session_value_get(session, "is_admin");

        if (!id || !name || !username)
        {
            free(id);
            free(name);
            free(username);
            free(is_admin_str);

            send_text(res, 500, "Error: Incomplete session data");
            return 0;
        }

        // Copy to arena BEFORE freeing
        ctx->id = ecewo_strdup(req, id);
        ctx->name = ecewo_strdup(req, name);
        ctx->username = ecewo_strdup(req, username);
        ctx->is_admin = string_to_bool(is_admin_str);

        // Initialize - will be set by is_authors_self
        ctx->user_slug = NULL;
        ctx->is_author = false;

        // Free malloc'd strings
        free(id);
        free(name);
        free(username);
        free(is_admin_str);

        if (!ctx->id || !ctx->name || !ctx->username)
        {
            send_text(res, 500, "Memory allocation failed");
            return 0;
        }

        printf("[is_auth] Session found - username: %s\n", ctx->username);
    }
    else
    {
        printf("[is_auth] No session - guest user\n");
        ctx->id = NULL;
        ctx->name = NULL;
        ctx->username = NULL;
        ctx->user_slug = NULL;
        ctx->is_admin = false;
        ctx->is_author = false;
    }

    set_context(req, "auth_ctx", ctx, sizeof(auth_context_t));
    return next(req, res, chain);
}

int is_authors_self(Req *req, Res *res, Chain *chain)
{
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req, "auth_ctx");

    if (!auth_ctx)
    {
        send_text(res, 500, "Auth context not found");
        return 0;
    }

    // Get user slug from URL parameter
    const char *user_slug_param = get_param(req, "user");
    if (!user_slug_param)
    {
        send_text(res, 400, "User parameter missing in URL");
        return 0;
    }

    printf("[is_authors_self] URL user param: %s\n", user_slug_param);

    // Store the URL user slug in context
    auth_ctx->user_slug = ecewo_strdup(req, user_slug_param);
    if (!auth_ctx->user_slug)
    {
        send_text(res, 500, "Memory allocation failed");
        return 0;
    }

    // Check if logged-in user matches the URL user
    if (auth_ctx->username != NULL)
    {
        printf("[is_authors_self] Logged in as: %s, viewing: %s\n", 
               auth_ctx->username, auth_ctx->user_slug);
        
        if (strcmp(auth_ctx->user_slug, auth_ctx->username) == 0)
        {
            auth_ctx->is_author = true;
            printf("[is_authors_self] User is author - will show hidden posts\n");
        }
        else
        {
            auth_ctx->is_author = false;
            printf("[is_authors_self] User is NOT author\n");
        }
    }
    else
    {
        auth_ctx->is_author = false;
        printf("[is_authors_self] Guest user viewing: %s\n", auth_ctx->user_slug);
    }

    return next(req, res, chain);
}

int auth_only(Req *req, Res *res, Chain *chain)
{
    auth_context_t *ctx = (auth_context_t *)get_context(req, "auth_ctx");

    if (!ctx || !ctx->id || !ctx->name || !ctx->username)
    {
        send_text(res, 401, "Not allowed");
        return 0;
    }

    return next(req, res, chain);
}
