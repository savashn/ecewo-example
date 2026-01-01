#include "ecewo-postgres.h"
#include "ecewo.h"
#include "uv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_ERROR(fmt, ...) \
    fprintf(stderr, "[ERROR] [ecewo-postgres] " fmt "\n", ##__VA_ARGS__)

struct pg_query_s {
    char *sql;
    char **params;
    int param_count;
    pg_result_cb_t result_cb;
    void *data;
    pg_query_t *next;
};

struct pg_async_s {
    PGconn *conn;
    Arena *arena;
    void *data;

    int is_connected;
    int is_executing;

    pg_query_t *query_queue;
    pg_query_t *query_queue_tail;
    pg_query_t *current_query;

    // Pool integration
    PGpool *pool;  // If non-NULL, release connection on completion
    
    // Arena management
    bool arena_owned;  // If true, we borrowed it and must return it
    
    pg_complete_cb_t on_complete;
    void *complete_data;

    int handle_initialized;
#ifdef _WIN32
    uv_timer_t timer;
#else
    uv_poll_t poll;
#endif
};

typedef struct {
    PGconn *conn;
    int in_use;
    uint64_t last_used;
} pool_connection_t;

struct pg_pool_s {
    pool_connection_t *connections;
    int size;
    int acquire_timeout_ms;
    char *conninfo;

    uv_mutex_t mutex;
    uv_cond_t cond;
    int destroyed;
};

static void execute_next_query(PGquery *pg);
static void cleanup_and_destroy(PGquery *pg);

#ifdef _WIN32
static void on_timer(uv_timer_t *handle);
#else
static void on_poll(uv_poll_t *handle, int status, int events);
#endif

static char *build_conninfo(PGPoolConfig *config)
{
    size_t len = snprintf(NULL, 0,
                         "host=%s port=%s dbname=%s user=%s password=%s",
                         config->host, config->port, config->dbname,
                         config->user, config->password) + 1;

    char *conninfo = malloc(len);
    if (!conninfo)
        return NULL;

    snprintf(conninfo, len,
            "host=%s port=%s dbname=%s user=%s password=%s",
            config->host, config->port, config->dbname,
            config->user, config->password);

    return conninfo;
}

static PGconn *create_connection(const char *conninfo)
{
    PGconn *conn = PQconnectdb(conninfo);

    if (PQstatus(conn) != CONNECTION_OK) {
        LOG_ERROR("Connection failed: %s", PQerrorMessage(conn));
        PQfinish(conn);
        return NULL;
    }

    if (PQsetnonblocking(conn, 1) != 0) {
        LOG_ERROR("Failed to set non-blocking mode");
        PQfinish(conn);
        return NULL;
    }

    return conn;
}

static int ensure_connection_healthy(pool_connection_t *pool_conn, const char *conninfo)
{
    if (PQstatus(pool_conn->conn) == CONNECTION_OK)
        return 0;

    PQreset(pool_conn->conn);

    if (PQstatus(pool_conn->conn) != CONNECTION_OK) {
        PQfinish(pool_conn->conn);

        pool_conn->conn = create_connection(conninfo);
        if (!pool_conn->conn)
            return -1;
    }

    if (PQsetnonblocking(pool_conn->conn, 1) != 0) {
        LOG_ERROR("Failed to set non-blocking mode after reset");
        return -1;
    }

    return 0;
}

PGpool *pg_pool_create(PGPoolConfig *config)
{
    if (!config || config->pool_size <= 0 || config->pool_size > 1024) {
        LOG_ERROR("Invalid pool configuration");
        return NULL;
    }

    PGpool *pool = malloc(sizeof(PGpool));
    if (!pool) {
        LOG_ERROR("Failed to allocate pool");
        return NULL;
    }

    memset(pool, 0, sizeof(PGpool));

    pool->conninfo = build_conninfo(config);
    if (!pool->conninfo) {
        free(pool);
        return NULL;
    }

    pool->size = config->pool_size;
    pool->acquire_timeout_ms = config->acquire_timeout_ms;
    pool->destroyed = 0;

    if (uv_mutex_init(&pool->mutex) != 0) {
        LOG_ERROR("Failed to initialize mutex");
        free(pool->conninfo);
        free(pool);
        return NULL;
    }

    if (uv_cond_init(&pool->cond) != 0) {
        LOG_ERROR("Failed to initialize condition variable");
        uv_mutex_destroy(&pool->mutex);
        free(pool->conninfo);
        free(pool);
        return NULL;
    }

    pool->connections = calloc(pool->size, sizeof(pool_connection_t));
    if (!pool->connections) {
        LOG_ERROR("Failed to allocate connections array");
        uv_cond_destroy(&pool->cond);
        uv_mutex_destroy(&pool->mutex);
        free(pool->conninfo);
        free(pool);
        return NULL;
    }

    for (int i = 0; i < pool->size; i++) {
        pool->connections[i].conn = create_connection(pool->conninfo);

        if (!pool->connections[i].conn) {
            LOG_ERROR("Failed to create connection %d/%d", i + 1, pool->size);

            for (int j = 0; j < i; j++) {
                if (pool->connections[j].conn) {
                    PQfinish(pool->connections[j].conn);
                }
            }

            free(pool->connections);
            uv_cond_destroy(&pool->cond);
            uv_mutex_destroy(&pool->mutex);
            free(pool->conninfo);
            free(pool);
            return NULL;
        }

        pool->connections[i].in_use = 0;
        pool->connections[i].last_used = 0;
    }

    return pool;
}

void pg_pool_destroy(PGpool *pool)
{
    if (!pool)
        return;

    uv_mutex_lock(&pool->mutex);
    pool->destroyed = 1;

    uv_cond_broadcast(&pool->cond);

    for (int i = 0; i < pool->size; i++) {
        if (pool->connections[i].conn) {
            PQfinish(pool->connections[i].conn);
            pool->connections[i].conn = NULL;
        }
    }

    uv_mutex_unlock(&pool->mutex);

    uv_cond_destroy(&pool->cond);
    uv_mutex_destroy(&pool->mutex);

    free(pool->connections);
    free(pool->conninfo);
    free(pool);
}

PGconn *pg_pool_borrow(PGpool *pool)
{
    if (!pool) {
        LOG_ERROR("pg_pool_borrow: pool is NULL");
        return NULL;
    }

    uv_mutex_lock(&pool->mutex);

    if (pool->destroyed) {
        uv_mutex_unlock(&pool->mutex);
        LOG_ERROR("pg_pool_borrow: pool is destroyed");
        return NULL;
    }

    uint64_t start_time = 0;
    if (pool->acquire_timeout_ms > 0) {
        start_time = uv_hrtime() / 1000000;
    }

    while (1) {
        for (int i = 0; i < pool->size; i++) {
            if (pool->connections[i].conn && !pool->connections[i].in_use) {
                if (ensure_connection_healthy(&pool->connections[i],
                                               pool->conninfo) == 0) {
                    pool->connections[i].in_use = 1;
                    pool->connections[i].last_used = uv_hrtime() / 1000000;
                    PGconn *conn = pool->connections[i].conn;
                    uv_mutex_unlock(&pool->mutex);
                    return conn;
                } else {
                    LOG_ERROR("Connection %d is unhealthy and cannot be repaired", i);
                    continue;
                }
            }
        }

        if (pool->acquire_timeout_ms == 0) {
            uv_mutex_unlock(&pool->mutex);
            LOG_ERROR("All %d connections in use (no wait mode)", pool->size);
            return NULL;
        }

        if (pool->acquire_timeout_ms > 0) {
            uint64_t now = uv_hrtime() / 1000000;
            uint64_t elapsed = now - start_time;

            if (elapsed >= (uint64_t)pool->acquire_timeout_ms) {
                uv_mutex_unlock(&pool->mutex);
                LOG_ERROR("Acquire timeout after %llu ms", (unsigned long long)elapsed);
                return NULL;
            }

            uint64_t remaining_ns = ((uint64_t)pool->acquire_timeout_ms - elapsed) * 1000000;
            int wait_result = uv_cond_timedwait(&pool->cond, &pool->mutex, remaining_ns);

            if (wait_result != 0 && wait_result != UV_ETIMEDOUT) {
                LOG_ERROR("Condition wait error: %d", wait_result);
                uv_mutex_unlock(&pool->mutex);
                return NULL;
            }
        } else {
            uv_cond_wait(&pool->cond, &pool->mutex);
        }

        if (pool->destroyed) {
            uv_mutex_unlock(&pool->mutex);
            LOG_ERROR("Pool destroyed while waiting");
            return NULL;
        }
    }
}

void pg_pool_return(PGpool *pool, PGconn *conn)
{
    if (!pool || !conn) {
        LOG_ERROR("pg_pool_return: invalid parameters");
        return;
    }

    uv_mutex_lock(&pool->mutex);

    for (int i = 0; i < pool->size; i++) {
        if (pool->connections[i].conn == conn) {
            if (!pool->connections[i].in_use) {
                LOG_ERROR("Warning: releasing connection that wasn't in use");
            }

            pool->connections[i].in_use = 0;
            uv_cond_signal(&pool->cond);
            uv_mutex_unlock(&pool->mutex);
            return;
        }
    }

    uv_mutex_unlock(&pool->mutex);
    LOG_ERROR("Warning: attempted to release unknown connection");
}

void pg_pool_get_stats(PGpool *pool, PGPoolStats *stats)
{
    if (!pool || !stats) {
        if (stats) {
            stats->total = 0;
            stats->available = 0;
            stats->in_use = 0;
        }
        return;
    }

    uv_mutex_lock(&pool->mutex);

    stats->total = pool->size;
    stats->available = 0;
    stats->in_use = 0;

    for (int i = 0; i < pool->size; i++) {
        if (pool->connections[i].conn) {
            if (pool->connections[i].in_use) {
                stats->in_use++;
            } else {
                stats->available++;
            }
        }
    }

    uv_mutex_unlock(&pool->mutex);
}

int pg_pool_cleanup_idle(PGpool *pool, uint64_t max_idle_ms)
{
    if (!pool)
        return -1;

    int closed_count = 0;
    uint64_t now = uv_hrtime() / 1000000;

    uv_mutex_lock(&pool->mutex);

    for (int i = 0; i < pool->size; i++) {
        if (pool->connections[i].conn && !pool->connections[i].in_use) {
            uint64_t idle_time = now - pool->connections[i].last_used;

            if (idle_time > max_idle_ms) {
                PQfinish(pool->connections[i].conn);
                pool->connections[i].conn = create_connection(pool->conninfo);

                if (pool->connections[i].conn) {
                    closed_count++;
                    pool->connections[i].last_used = now;
                }
            }
        }
    }

    uv_mutex_unlock(&pool->mutex);

    return closed_count;
}

static void on_handle_closed(uv_handle_t *handle)
{
    if (!handle || !handle->data)
        return;

    PGquery *pg = (PGquery *)handle->data;
    pg->handle_initialized = 0;
}

static void cancel_execution(PGquery *pg)
{
    if (!pg)
        return;

    if (pg->is_executing && pg->handle_initialized) {
#ifdef _WIN32
        uv_timer_stop(&pg->timer);
        if (!uv_is_closing((uv_handle_t *)&pg->timer)) {
            uv_close((uv_handle_t *)&pg->timer, on_handle_closed);
        }
#else
        uv_poll_stop(&pg->poll);
        if (!uv_is_closing((uv_handle_t *)&pg->poll)) {
            uv_close((uv_handle_t *)&pg->poll, on_handle_closed);
        }
#endif
    }

    if (pg->conn && pg->is_executing) {
        PGcancel *cancel = PQgetCancel(pg->conn);
        if (cancel) {
            char errbuf[256];
            PQcancel(cancel, errbuf, sizeof(errbuf));
            PQfreeCancel(cancel);
        }
    }

    pg->is_executing = 0;
}

static void cleanup_and_destroy(PGquery *pg)
{
    if (!pg)
        return;

    cancel_execution(pg);

    if (pg->pool && pg->conn)
        pg_pool_return(pg->pool, pg->conn);

    if (pg->on_complete)
        pg->on_complete(pg, pg->complete_data);

    if (pg->arena_owned && pg->arena)
        arena_return(pg->arena);

    // PGquery itself is in the arena, so it's freed when arena is returned
    // If arena is user-managed, user is responsible for cleanup
}

#ifdef _WIN32
static void on_timer(uv_timer_t *handle)
{
    if (!handle || !handle->data)
        return;

    PGquery *pg = (PGquery *)handle->data;

    if (!server_is_running()) {
        uv_timer_stop(&pg->timer);
        if (!uv_is_closing((uv_handle_t *)&pg->timer)) {
            uv_close((uv_handle_t *)&pg->timer, on_handle_closed);
        }
        pg->is_executing = 0;
        cleanup_and_destroy(pg);
        return;
    }

    if (!PQconsumeInput(pg->conn)) {
        LOG_ERROR("PQconsumeInput failed: %s", PQerrorMessage(pg->conn));
        pg->is_executing = 0;
        cleanup_and_destroy(pg);
        return;
    }

    if (PQisBusy(pg->conn))
        return;

    uv_timer_stop(&pg->timer);

    PGresult *result;
    while ((result = PQgetResult(pg->conn)) != NULL) {
        ExecStatusType result_status = PQresultStatus(result);

        if (result_status != PGRES_TUPLES_OK && result_status != PGRES_COMMAND_OK) {
            LOG_ERROR("Query failed: %s", PQresultErrorMessage(result));
            PQclear(result);
            pg->current_query = NULL;
            pg->is_executing = 0;
            cleanup_and_destroy(pg);
            return;
        }

        if (pg->current_query && pg->current_query->result_cb) {
            pg->current_query->result_cb(pg, result, pg->current_query->data);
        }

        PQclear(result);
    }

    pg->current_query = NULL;
    execute_next_query(pg);
}
#else
static void on_poll(uv_poll_t *handle, int status, int events)
{
    if (!handle || !handle->data)
        return;

    PGquery *pg = (PGquery *)handle->data;

    if (!server_is_running()) {
        uv_poll_stop(&pg->poll);
        if (!uv_is_closing((uv_handle_t *)&pg->poll)) {
            uv_close((uv_handle_t *)&pg->poll, on_handle_closed);
        }
        pg->is_executing = 0;
        cleanup_and_destroy(pg);
        return;
    }

    if (status < 0) {
        LOG_ERROR("Poll error: %s", uv_strerror(status));
        pg->is_executing = 0;
        cleanup_and_destroy(pg);
        return;
    }

    if (!PQconsumeInput(pg->conn)) {
        LOG_ERROR("PQconsumeInput failed: %s", PQerrorMessage(pg->conn));
        pg->is_executing = 0;
        cleanup_and_destroy(pg);
        return;
    }

    if (PQisBusy(pg->conn))
        return;

    uv_poll_stop(&pg->poll);

    PGresult *result;
    while ((result = PQgetResult(pg->conn)) != NULL) {
        ExecStatusType result_status = PQresultStatus(result);

        if (result_status != PGRES_TUPLES_OK && result_status != PGRES_COMMAND_OK) {
            LOG_ERROR("Query failed: %s", PQresultErrorMessage(result));
            PQclear(result);
            pg->current_query = NULL;
            pg->is_executing = 0;
            cleanup_and_destroy(pg);
            return;
        }

        if (pg->current_query && pg->current_query->result_cb) {
            pg->current_query->result_cb(pg, result, pg->current_query->data);
        }

        PQclear(result);
    }

    pg->current_query = NULL;
    execute_next_query(pg);
}
#endif

static void execute_next_query(PGquery *pg)
{
    if (!pg->query_queue) {
        pg->is_executing = 0;
        cleanup_and_destroy(pg);
        return;
    }

    if (!server_is_running()) {
        pg->is_executing = 0;
        cleanup_and_destroy(pg);
        return;
    }

    pg->current_query = pg->query_queue;
    pg->query_queue = pg->query_queue->next;
    if (!pg->query_queue) {
        pg->query_queue_tail = NULL;
    }

    int result;
    if (pg->current_query->param_count > 0) {
        result = PQsendQueryParams(
            pg->conn,
            pg->current_query->sql,
            pg->current_query->param_count,
            NULL,
            (const char **)pg->current_query->params,
            NULL,
            NULL,
            0);
    } else {
        result = PQsendQuery(pg->conn, pg->current_query->sql);
    }

    if (!result) {
        LOG_ERROR("Failed to send query: %s", PQerrorMessage(pg->conn));
        pg->is_executing = 0;
        cleanup_and_destroy(pg);
        return;
    }

#ifdef _WIN32
    if (!pg->handle_initialized) {
        int init_result = uv_timer_init(get_loop(), &pg->timer);
        if (init_result != 0) {
            LOG_ERROR("uv_timer_init failed: %s", uv_strerror(init_result));
            pg->is_executing = 0;
            cleanup_and_destroy(pg);
            return;
        }
        pg->handle_initialized = 1;
        pg->timer.data = pg;
    }

    int start_result = uv_timer_start(&pg->timer, on_timer, 10, 10);
    if (start_result != 0) {
        LOG_ERROR("uv_timer_start failed: %s", uv_strerror(start_result));
        pg->is_executing = 0;
        cleanup_and_destroy(pg);
        return;
    }
#else
    int sock = PQsocket(pg->conn);

    if (sock < 0) {
        LOG_ERROR("Invalid PostgreSQL socket");
        pg->is_executing = 0;
        cleanup_and_destroy(pg);
        return;
    }

    if (!pg->handle_initialized) {
        int init_result = uv_poll_init(get_loop(), &pg->poll, sock);
        if (init_result != 0) {
            LOG_ERROR("uv_poll_init failed: %s", uv_strerror(init_result));
            pg->is_executing = 0;
            cleanup_and_destroy(pg);
            return;
        }

        pg->handle_initialized = 1;
        pg->poll.data = pg;
    }

    int start_result = uv_poll_start(&pg->poll, UV_READABLE | UV_WRITABLE, on_poll);
    if (start_result != 0) {
        LOG_ERROR("uv_poll_start failed: %s", uv_strerror(start_result));
        pg->is_executing = 0;
        cleanup_and_destroy(pg);
        return;
    }
#endif
}

static PGquery *pg_query_create(PGconn *conn, Arena *arena)
{
    if (!conn || !arena) {
        LOG_ERROR("pg_query_create: conn or arena is NULL");
        return NULL;
    }

    if (PQstatus(conn) != CONNECTION_OK) {
        LOG_ERROR("pg_query_create: Connection status is not OK");
        return NULL;
    }

    PGquery *pg = arena_alloc(arena, sizeof(PGquery));
    if (!pg) {
        LOG_ERROR("pg_query_create: Failed to allocate from arena");
        return NULL;
    }

    memset(pg, 0, sizeof(PGquery));
    pg->conn = conn;
    pg->arena = arena;
    pg->is_connected = 1;
    pg->is_executing = 0;
    pg->handle_initialized = 0;
    pg->query_queue = NULL;
    pg->query_queue_tail = NULL;
    pg->current_query = NULL;
    pg->pool = NULL;
    pg->arena_owned = false;
    pg->on_complete = NULL;
    pg->complete_data = NULL;

#ifdef _WIN32
    pg->timer.data = pg;
#else
    pg->poll.data = pg;
#endif

    return pg;
}

PGquery *pg_query(PGpool *pool, Arena *arena)
{
    if (!pool) {
        LOG_ERROR("pg_query: pool is NULL");
        return NULL;
    }

    bool arena_owned = false;
    
    if (!arena) {
        arena = arena_borrow();
        if (!arena) {
            LOG_ERROR("pg_query: Failed to borrow arena");
            return NULL;
        }
        arena_owned = true;
    }

    PGconn *conn = pg_pool_borrow(pool);
    if (!conn) {
        LOG_ERROR("pg_query: Could not acquire connection");
        if (arena_owned)
            arena_return(arena);
        return NULL;
    }

    PGquery *pg = pg_query_create(conn, arena);
    if (!pg) {
        pg_pool_return(pool, conn);
        if (arena_owned)
            arena_return(arena);
        return NULL;
    }

    pg->pool = pool;
    pg->arena_owned = arena_owned;
    
    return pg;
}

void pg_query_on_complete(PGquery *pg, pg_complete_cb_t callback, void *data)
{
    if (!pg)
        return;

    pg->on_complete = callback;
    pg->complete_data = data;
}

int pg_query_queue(PGquery *pg,
                   const char *sql,
                   int param_count,
                   const char **params,
                   pg_result_cb_t result_cb,
                   void *query_data)
{
    if (!pg || !sql) {
        LOG_ERROR("pg_query_queue: Invalid parameters");
        return -1;
    }

    pg_query_t *query = arena_alloc(pg->arena, sizeof(pg_query_t));
    if (!query) {
        LOG_ERROR("pg_query_queue: Failed to allocate query");
        return -1;
    }

    memset(query, 0, sizeof(pg_query_t));
    query->next = NULL;

    query->sql = arena_strdup(pg->arena, sql);
    if (!query->sql) {
        LOG_ERROR("pg_query_queue: Failed to copy SQL");
        return -1;
    }

    if (param_count > 0 && params) {
        query->params = arena_alloc(pg->arena, param_count * sizeof(char *));
        if (!query->params) {
            LOG_ERROR("pg_query_queue: Failed to allocate params");
            return -1;
        }

        for (int i = 0; i < param_count; i++) {
            if (params[i]) {
                query->params[i] = arena_strdup(pg->arena, params[i]);
                if (!query->params[i]) {
                    LOG_ERROR("pg_query_queue: Failed to allocate a param");
                    return -1;
                }
            } else {
                query->params[i] = NULL;
            }
        }
    } else {
        query->params = NULL;
    }

    query->param_count = param_count;
    query->result_cb = result_cb;
    query->data = query_data;

    if (!pg->query_queue) {
        pg->query_queue = pg->query_queue_tail = query;
    } else {
        pg->query_queue_tail->next = query;
        pg->query_queue_tail = query;
    }

    return 0;
}

int pg_query_exec(PGquery *pg)
{
    if (!pg) {
        LOG_ERROR("pg_query_exec: pg is NULL");
        return -1;
    }

    if (!pg->is_connected) {
        LOG_ERROR("pg_query_exec: Not connected");
        return -1;
    }

    if (pg->is_executing) {
        LOG_ERROR("pg_query_exec: Already executing");
        return -1;
    }

    if (!pg->query_queue) {
        cleanup_and_destroy(pg);
        return 0;
    }

    pg->is_executing = 1;
    execute_next_query(pg);
    return 0;
}
