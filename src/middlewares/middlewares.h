#ifndef MIDDLEWARES_H
#define MIDDLEWARES_H

#include "ecewo.h"

void body_checker(Req *req, Res *res, Next next);
void is_auth(Req *req, Res *res, Next next);
void auth_only(Req *req, Res *res, Next next);
void is_authors_self(Req *req, Res *res, Next next);

#endif
