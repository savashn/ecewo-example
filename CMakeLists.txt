cmake_minimum_required(VERSION 3.14)
project(ecewo_blog_app VERSION 1.0.0 LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -flto)
    add_link_options(-flto)
    add_compile_definitions(NDEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g3)
    add_compile_definitions(DEBUG=1)
endif()

add_compile_options(-Wall -Wextra)

include(FetchContent)

FetchContent_Declare(
    ecewo
    GIT_REPOSITORY https://github.com/savashn/ecewo.git
    GIT_TAG v3.0.1
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(ecewo)

add_executable(server
    src/main.c
    src/db/db.c
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
    vendors/modules/ecewo-cookie.c
    vendors/modules/ecewo-cors.c
    vendors/modules/ecewo-helmet.c
    vendors/modules/ecewo-postgres.c
    vendors/modules/ecewo-session.c
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
    ${CMAKE_CURRENT_SOURCE_DIR}/vendors/modules
)

if(WIN32 AND TARGET ecewo)
    get_target_property(ECEWO_TYPE ecewo TYPE)
    if(ECEWO_TYPE STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET server POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:ecewo>
            $<TARGET_FILE_DIR:server>
        )
        
        if(TARGET uv)
            add_custom_command(TARGET server POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:uv>
                $<TARGET_FILE_DIR:server>
            )
        endif()
        
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
find_package(mimalloc CONFIG QUIET)

if(mimalloc_FOUND)
    message(STATUS "mimalloc found: ${mimalloc_VERSION}")

    set(MI_OVERRIDE ON CACHE BOOL "" FORCE)

    if(TARGET mimalloc-static)
        target_link_libraries(server PRIVATE mimalloc-static)
    else()
        target_link_libraries(server PRIVATE mimalloc)
    endif()
endif()

target_include_directories(server PRIVATE
    ${PostgreSQL_INCLUDE_DIRS}
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

target_link_libraries(server PRIVATE
    ecewo
    ${PostgreSQL_LIBRARIES}
    mimalloc-static
)

message(STATUS "=== Build Configuration ===")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "Optimizations: Enabled (O3/O2 + LTO)")
else()
    message(STATUS "Optimizations: Disabled (Debug mode)")
endif()

message(STATUS "===========================")
