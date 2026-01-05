#ifndef DB_H
#define DB_H

#include "ecewo-postgres.h"

// Synchronous initialization
int db_init(void);

PGpool *db_get_pool(void);
void db_cleanup(void);

#endif