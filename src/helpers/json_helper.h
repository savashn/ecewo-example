#ifndef JSON_HELPER_H
#define JSON_HELPER_H

// For single row
#define ADD_STR(obj, col) \
    cJSON_AddStringToObject(obj, #col, PQgetvalue(result, 0, PQfnumber(result, #col)))

#define ADD_INT(obj, col) \
    cJSON_AddNumberToObject(obj, #col, atoi(PQgetvalue(result, 0, PQfnumber(result, #col))))

#define ADD_BOOL(obj, col) \
    cJSON_AddBoolToObject(obj, #col, strcmp(PQgetvalue(result, 0, PQfnumber(result, #col)), "t") == 0)

// For multiple rows
#define ADD_STR_ROW(obj, col, row) \
    cJSON_AddStringToObject(obj, #col, PQgetvalue(result, row, PQfnumber(result, #col)))

#define ADD_INT_ROW(obj, col, row) \
    cJSON_AddNumberToObject(obj, #col, atoi(PQgetvalue(result, row, PQfnumber(result, #col))))

#define ADD_BOOL_ROW(obj, col, row) \
    cJSON_AddBoolToObject(obj, #col, strcmp(PQgetvalue(result, row, PQfnumber(result, #col)), "t") == 0)

#endif
