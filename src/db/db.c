#include "ecewo-postgres.h"
#include "dotenv.h"
#include <stdlib.h>

static PGpool *db_pool = NULL;

static int create_tables(void)
{
    // This fn is gonna run sync because
    // I dont want server to start
    // before all the tables created
    PGconn *conn = pg_pool_borrow(db_pool);
    if (!conn) {
        fprintf(stderr, "Failed to acquire connection\n");
        return -1;
    }

    const char *queries[] = {
        "CREATE TABLE IF NOT EXISTS users ("
        "  id SERIAL PRIMARY KEY, "
        "  name TEXT NOT NULL, "
        "  username TEXT NOT NULL, "
        "  password TEXT NOT NULL, "
        "  email TEXT NOT NULL, "
        "  about TEXT, "
        "  created_at TIMESTAMP WITHOUT TIME ZONE DEFAULT NOW()"
        ");",

        "CREATE TABLE IF NOT EXISTS posts ("
        "  id SERIAL PRIMARY KEY, "
        "  header TEXT NOT NULL, "
        "  slug TEXT NOT NULL, "
        "  content TEXT NOT NULL, "
        "  reading_time INTEGER NOT NULL, "
        "  author_id INTEGER NOT NULL REFERENCES users(id), "
        "  created_at TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT NOW(), "
        "  updated_at TIMESTAMP WITHOUT TIME ZONE NOT NULL DEFAULT NOW(), "
        "  is_hidden BOOLEAN NOT NULL DEFAULT FALSE"
        ");",

        "CREATE TABLE IF NOT EXISTS categories ("
        "  id SERIAL PRIMARY KEY, "
        "  category TEXT NOT NULL, "
        "  slug TEXT NOT NULL, "
        "  author_id INTEGER NOT NULL REFERENCES users(id)"
        ");",

        "CREATE TABLE IF NOT EXISTS post_categories ("
        "  post_id INTEGER NOT NULL REFERENCES posts(id) ON DELETE CASCADE, "
        "  category_id INTEGER NOT NULL REFERENCES categories(id) ON DELETE CASCADE, "
        "  PRIMARY KEY(post_id, category_id)"
        ");",
    };

    size_t count = sizeof(queries) / sizeof(queries[0]);

    for (size_t i = 0; i < count; ++i) {
        PGresult *result = PQexec(conn, queries[i]);
        ExecStatusType status = PQresultStatus(result);
        
        if (status != PGRES_COMMAND_OK) {
            fprintf(stderr, "Table creation failed: %s\n", PQerrorMessage(conn));
            PQclear(result);
            pg_pool_return(db_pool, conn);
            return -1;
        }
        
        PQclear(result);
    }

    pg_pool_return(db_pool, conn);
    printf("All tables created or they already exist.\n");
    return 0;
}

int db_init(void)
{
    PGPoolConfig config = {
        .host = getenv("DB_HOST"),
        .port = getenv("DB_PORT"),
        .dbname = getenv("DB_NAME"),
        .user = getenv("DB_USER"),
        .password = getenv("DB_PASSWORD"),
        .pool_size = 10,
        .timeout_ms = 5000
    };
    
    printf("[DB] Config: host=%s port=%s db=%s user=%s\n",
           config.host ? config.host : "NULL",
           config.port ? config.port : "NULL",
           config.dbname ? config.dbname : "NULL",
           config.user ? config.user : "NULL");

    db_pool = pg_pool_create(&config);
    
    if (!db_pool) {
        fprintf(stderr, "[DB] Failed to create database pool\n");
        return -1;
    }
    
    if (create_tables() != 0) {
        fprintf(stderr, "[DB] Tables couldn't be created\n");
        return -1;
    }
    
    return 0;
}

PGpool *db_get_pool(void)
{
    return db_pool;
}

void db_cleanup(void)
{
    if (db_pool) {
        printf("[DB] Cleaning up database pool\n");
        pg_pool_destroy(db_pool);
        db_pool = NULL;
    }
}
