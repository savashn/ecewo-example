#ifndef DB_H
#define DB_H

#include "ecewo-postgres.h"

int db_init(void);
void db_cleanup(void);
PGpool *db_get_pool(void);

#endif
