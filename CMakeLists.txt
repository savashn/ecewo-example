cmake_minimum_required(VERSION 3.14)
project(ecewo_blog_app VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_BUILD_TYPE Release)

include(FetchContent)

FetchContent_Declare(
    ecewo
    GIT_REPOSITORY https://github.com/savashn/ecewo.git
    GIT_TAG v2.0.0
    GIT_SHALLOW TRUE
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
    src/utils/utils.c
    vendors/cJSON.c
    vendors/dotenv.c
    vendors/slugify.c
    vendors/ecewo-session.c
    vendors/ecewo-postgres.c
    vendors/ecewo-cookie.c
    vendors/ecewo-cors.c
    vendors/ecewo-helmet.c
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

if(MSVC)
    target_compile_options(server PRIVATE /W4)
else()
    target_compile_options(server PRIVATE -Wall -Wextra)
    
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(server PRIVATE -g -O0)
        target_compile_definitions(server PRIVATE DEBUG=1)
    else()
        target_compile_options(server PRIVATE -O3)
        target_compile_definitions(server PRIVATE NDEBUG=1)
    endif()
endif()

if(WIN32 AND TARGET ecewo)
    get_target_property(ECEWO_TYPE ecewo TYPE)
    if(ECEWO_TYPE STREQUAL "SHARED_LIBRARY")
        # Copy ecewo DLL files
        add_custom_command(TARGET server POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ecewo>
            $<TARGET_FILE_DIR:server>
        )
        
        # Copy libuv DLL files
        if(TARGET uv)
            add_custom_command(TARGET server POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:uv>
                $<TARGET_FILE_DIR:server>
            )
        endif()
        
        # Copy llhttp DLL files
        if(TARGET llhttp_shared)
            add_custom_command(TARGET server POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:llhttp_shared>
                $<TARGET_FILE_DIR:server>
            )
        endif()
    endif()
endif()

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
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SODIUM REQUIRED libsodium)

    target_include_directories(server PRIVATE ${SODIUM_INCLUDE_DIRS})
    target_link_libraries(server PRIVATE ${SODIUM_LIBRARIES})
    target_compile_options(server PRIVATE ${SODIUM_CFLAGS_OTHER})
endif()
