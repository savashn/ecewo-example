#ifndef ECEWO_POSTGRES_H
#define ECEWO_POSTGRES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ecewo.h"
#include "libpq-fe.h"

typedef struct pg_async_s PGquery;
typedef struct pg_query_s pg_query_t;
typedef struct pg_pool_s PGpool;

typedef void (*pg_result_cb_t)(PGquery *pg, PGresult *result, void *data);
typedef void (*pg_complete_cb_t)(PGquery *pg, void *data);

typedef struct {
    const char *host;
    const char *port;
    const char *dbname;
    const char *user;
    const char *password;
    int pool_size;           // Number of connections in pool
    int acquire_timeout_ms;  // 0 = no wait, -1 = infinite
} PGPoolConfig;

typedef struct {
    int total;
    int available;
    int in_use;
} PGPoolStats;

PGpool *pg_pool_create(PGPoolConfig *config);
PGconn *pg_pool_borrow(PGpool *pool);
void pg_pool_return(PGpool *pool, PGconn *conn);
void pg_pool_destroy(PGpool *pool);

void pg_pool_get_stats(PGpool *pool, PGPoolStats *stats);

int pg_pool_cleanup_idle(PGpool *pool, uint64_t max_idle_ms);

// Create query from pool
// - If arena is NULL: borrows arena automatically
// - If arena is provided: uses provided arena
PGquery *pg_query(PGpool *pool, Arena *arena);

// Set completion callback
// Called after all queries complete, before automatic cleanup
void pg_query_on_complete(PGquery *pg, pg_complete_cb_t callback, void *data);

// Queue a query for execution
int pg_query_queue(PGquery *pg,
                   const char *sql,
                   int param_count,
                   const char **params,
                   pg_result_cb_t result_cb,
                   void *query_data);

// Execute all queued queries
// PGquery is automatically cleaned up after all queries complete
int pg_query_exec(PGquery *pg);

#ifdef __cplusplus
}
#endif

#endif