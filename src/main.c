#include "ecewo-cors.h"
#include "ecewo-helmet.h"
#include "ecewo-session.h"
#include "sodium.h"
#include "dotenv.h"
#include "db.h"
#include "routers.h"
#include "middlewares.h"
#include <stdio.h>

static const char *allowed_origins[] = {
    "http://localhost:3000"
};

static const Cors cors = {
    .origins = allowed_origins,
    .origins_count = 1,

    .methods = "GET, POST, OPTIONS",
    .allowed_headers = "Content-Type, Authorization",
    .exposed_headers = NULL,

    .credentials = true,
    .max_age = 86400
};

void destroy_app(void) {
    cors_cleanup();
    session_cleanup();
    db_cleanup();
}

int main(void) {
    if (server_init() != 0) {
        fprintf(stderr, "Failed to initialize server\n");
        return 1;
    }

    env_load("..", false);

    const char *port_str = getenv("PORT");
    if (!port_str) {
        fprintf(stderr, "PORT is not set\n");
        return 1;
    }

    char *end = NULL;
    long port = strtol(port_str, &end, 10);
    if (*end != '\0' || port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid PORT: %s\n", port_str);
        return 1;
    }

    cors_init(&cors);
    helmet_init(NULL);
    session_init();

    if (db_init() != 0) {
        fprintf(stderr, "Database initialization failed.\n");
        return 1;
    }

    use(is_auth);
    register_routers();

    server_atexit(destroy_app);

    if (server_listen(port) != 0) {
        fprintf(stderr, "Failed to start server\n");
        return 1;
    }

    server_run();
    return 0;
}
