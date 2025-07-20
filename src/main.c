#include "server.h"
#include "cors.h"
#include "sodium.h"
#include "dotenv.h"
#include "db.h"
#include "routers.h"
#include "session.h"
#include "middlewares.h"

void destroy_app()
{
    close_db();
    reset_sessions();
    reset_middleware();
    reset_router();
    reset_cors();
}

int main()
{
    env_load("..", false);
    const char *port = getenv("PORT");
    const unsigned short PORT = (unsigned short)atoi(port);
    char *CORS_ORIGIN = getenv("CORS_ORIGIN");

    cors_t cors = {
        .origin = CORS_ORIGIN,
        .headers = "Content-Type, Authorization",
        .credentials = "true",
        .max_age = "86400",
        .enabled = false, // Delete this line if you want to enable the cors configuration
    };

    init_cors(&cors);
    init_router();
    init_sessions();

    if (init_db() != 0)
    {
        fprintf(stderr, "Database initialization failed.\n");
        return 1;
    }

    hook(is_auth); // Global middleware

    register_routers();

    shutdown_hook(destroy_app);
    ecewo(PORT);
    return 0;
}
