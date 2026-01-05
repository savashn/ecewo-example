#include "handlers.h"
#include "db.h"
#include "ecewo-session.h"
#include "context.h"
#include "slugify.h"
#include <stdio.h>
#include <stdlib.h>

void hello_world(Req *req, Res *res)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "message", "Hello World!");
    char *json_string = cJSON_PrintUnformatted(json);
    send_json(res, 200, json_string);
    cJSON_Delete(json);
    free(json_string);
}

void get_all_users(Req *req, Res *res)
{
    PGpool *pool = db_get_pool();
    if (!pool) {
        send_text(res, 500, "Database pool unavailable");
        return;
    }
    
    PGconn *conn = pg_pool_borrow(pool);
    if (!conn) {
        send_text(res, 500, "Failed to acquire database connection");
        return;
    }
    
    const char *sql = "SELECT id, name, username FROM users;";
    PGresult *result = PQexec(conn, sql);
    
    if (PQresultStatus(result) != PGRES_TUPLES_OK) {
        fprintf(stderr, "DB select failed: %s", PQerrorMessage(conn));
        PQclear(result);
        pg_pool_return(pool, conn);
        send_text(res, 500, "DB select failed");
        return;
    }
    
    int rows = PQntuples(result);
    cJSON *json_array = cJSON_CreateArray();
    
    for (int i = 0; i < rows; i++) {
        int id = atoi(PQgetvalue(result, i, 0));
        const char *name = PQgetvalue(result, i, 1);
        const char *username = PQgetvalue(result, i, 2);
        
        cJSON *user_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(user_json, "id", id);
        cJSON_AddStringToObject(user_json, "name", name);
        cJSON_AddStringToObject(user_json, "username", username);
        cJSON_AddItemToArray(json_array, user_json);
    }
    
    PQclear(result);
    pg_pool_return(pool, conn);
    
    char *json_string = cJSON_PrintUnformatted(json_array);
    send_json(res, 200, json_string);
    cJSON_Delete(json_array);
    free(json_string);
}

void logout(Req *req, Res *res)
{
    Session *sess = session_get(req);

    if (!sess)
        send_text(res, 400, "You have to login");
    else
    {
        // The cookie_options should be the same as what we use in the login handler
        Cookie cookie_options = {
            .max_age = 3600, // 1 hour
            .path = "/",
            .same_site = "Lax",
            .http_only = true,
            .secure = true,
        };

        session_destroy(res, sess, &cookie_options);
        send_text(res, 302, "Logged out");
    }
}
