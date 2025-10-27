#include "ecewo/cors.h"
#include "ecewo/helmet.h"
#include "ecewo/session.h"
#include "sodium.h"
#include "dotenv.h"
#include "db.h"
#include "routers.h"
#include "middlewares.h"
#include <stdio.h>

static const Cors cors = {
    .origin = "http://localhost:3000",        // Default "*"
    .methods = "GET, POST, OPTIONS",          // Default "GET, POST, PUT, DELETE, OPTIONS"
    .headers = "Content-Type, Authorization", // Default "Content-Type"
    .credentials = "true",                    // Default "false"
    .max_age = "86400",                       // Default "3600"
};

void destroy_app(void)
{
    close_db();
    session_cleanup();
}

int main(void)
{
    if (server_init() != SERVER_OK)
    {
        fprintf(stderr, "Failed to initialize server\n");
        return 1;
    }

    env_load("..", false);
    const char *port = getenv("PORT");
    const int PORT = (int)atoi(port);

    cors_init(&cors);
    helmet_init(NULL);
    session_init();

    if (init_db() != 0)
    {
        fprintf(stderr, "Database initialization failed.\n");
        return 1;
    }

    hook(is_auth); // Global middleware

    register_routers();

    shutdown_hook(destroy_app);

    if (server_listen(PORT) != SERVER_OK)
    {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    server_run();
    return 0;
}
