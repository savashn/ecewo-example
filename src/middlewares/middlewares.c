#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "middlewares.h"
#include "ecewo-session.h"
#include "cJSON.h"
#include "context.h"

void body_checker(Req *req, Res *res, Next next)
{
    if (!req->body)
    {
        send_text(res, 400, "Missing request body");
        return;
    }

    next(req, res);
}

static bool string_to_bool(const char *str)
{
    if (!str)
        return false;
    return (strcmp(str, "true") == 0 || strcmp(str, "1") == 0);
}

void is_auth(Req *req, Res *res, Next next)
{
    Session *session = session_get(req);

    auth_context_t *ctx = arena_alloc(req->arena, sizeof(auth_context_t));
    if (!ctx)
    {
        send_text(res, 500, "Internal Server Error");
        return;
    }

    if (session)
    {
        char *id = session_value_get(session, "id", req->arena);
        char *name = session_value_get(session, "name", req->arena);
        char *username = session_value_get(session, "username", req->arena);
        char *is_admin_str = session_value_get(session, "is_admin", req->arena);

        if (!id || !name || !username)
        {
            free(id);
            free(name);
            free(username);
            free(is_admin_str);

            send_text(res, 500, "Error: Incomplete session data");
            return;
        }

        // Copy to arena BEFORE freeing
        ctx->id = arena_strdup(req->arena, id);
        ctx->name = arena_strdup(req->arena, name);
        ctx->username = arena_strdup(req->arena, username);
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
            return;
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

    set_context(req, "auth_ctx", ctx);
    next(req, res);
}

void is_authors_self(Req *req, Res *res, Next next)
{
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req, "auth_ctx");

    if (!auth_ctx)
    {
        send_text(res, 500, "Auth context not found");
        return;
    }

    // Get user slug from URL parameter
    const char *user_slug_param = get_param(req, "user");
    if (!user_slug_param)
    {
        send_text(res, 400, "User parameter missing in URL");
        return;
    }

    printf("[is_authors_self] URL user param: %s\n", user_slug_param);

    // Store the URL user slug in context
    auth_ctx->user_slug = arena_strdup(req->arena, user_slug_param);
    if (!auth_ctx->user_slug)
    {
        send_text(res, 500, "Memory allocation failed");
        return;
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

    next(req, res);
}

void auth_only(Req *req, Res *res, Next next)
{
    auth_context_t *ctx = (auth_context_t *)get_context(req, "auth_ctx");

    if (!ctx || !ctx->id || !ctx->name || !ctx->username)
    {
        send_text(res, 401, "Not allowed");
        return;
    }

    next(req, res);
}
