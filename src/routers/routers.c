#include "handlers.h"
#include "middlewares.h"

void register_routers(void)
{
    get("/user/:user/filter/posts", is_authors_self, get_posts_by_cat);

    get("/user/:user/posts/:post", is_authors_self, get_post);
    put("/user/:user/posts/:post", auth_only, is_authors_self, edit_post);
    del("/user/:user/posts/:post", auth_only, is_authors_self, del_post);

    del("/user/:user/categories/:category", auth_only, is_authors_self, del_category);
    put("/user/:user/categories/:category", auth_only, is_authors_self, edit_category);

    get("/user/:user/posts", is_authors_self, get_all_posts);
    get("/user/:user", is_authors_self, get_profile);

    post("/create/category", body_checker, auth_only, create_category);
    post("/create/post", body_checker, auth_only, create_post);
    post("/login", body_checker, login);
    post("/register", body_checker, add_user);

    get("/logout", logout);
    get("/users", get_all_users);
    get("/users-async", get_all_users_async);
    get("/", hello_world);
}
