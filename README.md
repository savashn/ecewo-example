# ecewo-example

This is an example blog app built with [Ecewo](https://github.com/savashn/ecewo) and PostgreSQL.

Using dependencies:
- [pquv](https://github.com/savashn/pquv) for integration of async PostgreSQL queries with Ecewo, based on [libuv](https://libuv.org/) and [libpq](https://www.postgresql.org/docs/current/libpq.html)
- [ecewo-session](https://github.com/savashn/ecewo-session) for session-based authentication
- [cJSON](https://github.com/DaveGamble/cJSON) for handling JSON objects
- [slugify-c](https://github.com/savashn/slugify-c) for creating URL-friendly ASCII characters
- [dotenv-c](https://github.com/Isty001/dotenv-c) for managing environment variables
- [libsodium](https://github.com/jedisct1/libsodium) for password hashing with `argon2`

<hr />

## Table of Contents
- [Requirements](#requirements)
- [Installation](#installation)
- [Environment Variables](#environment-variables)
- [Endpoints](#endpoints)

## Requirements

- CMake version 3.14 or higher
- [libpq](https://www.postgresql.org/docs/current/libpq.html)
- [libsodium](https://github.com/jedisct1/libsodium) (Not required on Windows, as it's already included in the `vendors/libsodium-win64` folder)

<br />

## Installation

Clone the repo:

```shell
git clone https://github.com/savashn/ecewo-example.git
```

Build:

```shell
./build.sh
```

Build from scratch:

```shell
./build.sh rebuild
```

Manually building on Windows:

```shell
mkdir build && cd build && cmake .. && cmake --build . && ./server.exe
```

Manually building on Linux/macOS:

```shell
mkdir build && cd build && cmake .. && cmake --build . && ./server
```

> [!WARNING]
> Before compiling the program, create a `.env` file in the project's root directory and define the following environment variables. Otherwise, you may encounter a segmentation fault at startup.

### Environment Variables

```
PORT
CORS_ORIGIN
DB_HOST
DB_PORT
DB_NAME
DB_USER
DB_PASSWORD
```

## Endpoints

You can see all the endpoints in `src/routers/routers.c` file.

Here are what `POST` and `PUT` endpoints wait for:

`POST /register`
```json
{
    "name": "John Doe",
    "username": "johndoe",
    "password": "123123",
    "email": "noone@nowhere.com",
    "about": "About John Doe"
}
```

`POST /login`
```json
{
    "username": "johndoe",
    "password": "123123"
}
```

**OR**

```json
{
    "email": "noone@nowhere.com",
    "password": "123123"
}
```

`POST /create/category`
```json
{
    "category": "John Doe's Test Category"
}
```

`POST /create/post`
```json
{
    "header": "John Doe's Example Post",
    "content": "John Doe's example post content",
    "categories": [1]
}
```

`PUT /user/:user/categories/:category`
```json
{
    "category": "John Doe's Edited Test Category"
}
```

`PUT /user/:user/posts/:post`
```json
{
    "header": "John Doe's Edited Example Post",
    "content": "John Doe's edited example post content",
    "categories": []
}
```
