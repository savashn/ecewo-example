#include <stdlib.h>
#include "middlewares.h"
#include "session.h"
#include "cJSON.h"
#include "context.h"

int body_checker(Req *req, Res *res, Chain *chain)
{
    if (!req->body)
    {
        send_text(res, 400, "Missing request body");
        return 0;
    }

    return next(chain, req, res);
}

static bool string_to_bool(const char *str)
{
    if (!str)
        return false;
    return (strcmp(str, "true") == 0 || strcmp(str, "1") == 0);
}

int is_auth(Req *req, Res *res, Chain *chain)
{
    // Get the user session
    Session *session = get_session(req);

    // Always allocate a session context, even if no session data
    auth_context_t *ctx = calloc(1, sizeof(auth_context_t));
    if (!ctx)
    {
        send_text(res, 500, "Internal Server Error");
        return 0;
    }

    if (session)
    {
        char *id = get_session_value(session, "id");
        char *name = get_session_value(session, "name");
        char *username = get_session_value(session, "username");
        char *is_admin_str = get_session_value(session, "is_admin");

        if (!id || !name || !username)
        {
            free(ctx);

            free(id);
            free(name);
            free(username);
            free(is_admin_str);

            send_text(res, 500, "Error: Incomplete session data");
            return 0;
        }

        ctx->id = id;
        ctx->name = name;
        ctx->username = username;
        ctx->is_admin = string_to_bool(is_admin_str);

        free(is_admin_str);
    }
    else
    {
        ctx->id = NULL;
        ctx->name = NULL;
        ctx->username = NULL;
        ctx->is_admin = false;
    }

    // Attach context to request; cleanup will free ctx and strings
    set_context(req, ctx, sizeof(*ctx), cleanup_auth_ctx);

    // Continue to next handler in chain
    return next(chain, req, res);
}

int is_authors_self(Req *req, Res *res, Chain *chain)
{
    auth_context_t *auth_ctx = (auth_context_t *)get_context(req);

    const char *user_slug = get_params(req, "user");
    auth_ctx->user_slug = strdup(user_slug ? user_slug : "");

    if (!auth_ctx->user_slug)
    {
        send_text(res, 500, "Internal Server Error");
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
        send_text(res, 401, "Not allowed");
        return 0;
    }

    return next(chain, req, res);
}
