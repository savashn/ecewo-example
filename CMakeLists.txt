cmake_minimum_required(VERSION 3.14)
project(ecewo_blog_app VERSION 1.0.0 LANGUAGES C)

include(FetchContent)

FetchContent_Declare(
    ecewo
    GIT_REPOSITORY https://github.com/savashn/ecewo.git
    GIT_TAG v0.29.0
)

FetchContent_MakeAvailable(ecewo)

add_executable(server
    src/main.c
    src/db/db.c
    src/db/connection.c
    src/handlers/sync_handlers.c
    src/handlers/post_handlers/login.c
    src/handlers/post_handlers/register.c
    src/handlers/post_handlers/create_post.c
    src/handlers/post_handlers/create_category.c
    src/handlers/get_handlers/get_all_users.c
    src/handlers/get_handlers/get_post.c
    src/handlers/get_handlers/get_all_posts.c
    src/handlers/get_handlers/get_profile.c
    src/handlers/get_handlers/get_posts_by_cat.c
    src/handlers/del_handlers/del_post.c
    src/handlers/del_handlers/del_category.c
    src/handlers/put_handlers/edit_post.c
    src/handlers/put_handlers/edit_category.c
    src/routers/routers.c
    src/middlewares/middlewares.c
    src/contexts/context.c
    src/utils/utils.c
    vendors/cJSON.c
    vendors/dotenv.c
    vendors/slugify.c
    vendors/session.c
    vendors/pquv.c
)

target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db
    ${CMAKE_CURRENT_SOURCE_DIR}/src/handlers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/handlers/get_handlers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/handlers/post_handlers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/handlers/del_handlers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/handlers/put_handlers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/middlewares
    ${CMAKE_CURRENT_SOURCE_DIR}/src/routers
    ${CMAKE_CURRENT_SOURCE_DIR}/src/contexts
    ${CMAKE_CURRENT_SOURCE_DIR}/src/utils
    ${CMAKE_CURRENT_SOURCE_DIR}/src/helpers
    ${CMAKE_CURRENT_SOURCE_DIR}/vendors
)

find_package(PostgreSQL REQUIRED)

target_include_directories(server PRIVATE
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(server PRIVATE
    ecewo
    ${PostgreSQL_LIBRARIES}
)

if(WIN32)
    set(SODIUM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendors/libsodium-win64")

    target_include_directories(server PRIVATE
        ${SODIUM_ROOT}/include
    )

    target_link_libraries(server PRIVATE
        "${SODIUM_ROOT}/lib/libsodium.a"
    )
else()
    find_package(Sodium REQUIRED)

    target_link_libraries(server PRIVATE
        ecewo
        Sodium::Sodium
    )
endif()
