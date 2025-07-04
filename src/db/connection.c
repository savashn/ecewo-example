#include "dotenv.h"
#include <stdlib.h>
#include <stdio.h>

char *build_connection_string(void)
{
    env_load("../..", false);

    const char *db_host = getenv("DB_HOST");
    const char *db_port = getenv("DB_PORT");
    const char *db_name = getenv("DB_NAME");
    const char *db_user = getenv("DB_USER");
    const char *db_password = getenv("DB_PASSWORD");

    static char conninfo[512];
    snprintf(conninfo, sizeof(conninfo),
             "host=%s port=%s dbname=%s user=%s password=%s",
             db_host, db_port, db_name, db_user, db_password);

    return conninfo;
}
