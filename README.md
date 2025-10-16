# Ecewo Example Blog App

This is an example blog app built with [Ecewo](https://github.com/savashn/ecewo) and PostgreSQL.

> [!WARNING]
>
> This is not a real-world app. It is built to show what Ecewo looks like.

Using dependencies:
- [ecewo-postgres](https://github.com/savashn/ecewo-modules/tree/main/postgres) for integration of async PostgreSQL queries with Ecewo, based on [libuv](https://libuv.org/) and [libpq](https://www.postgresql.org/docs/current/libpq.html)
- [ecewo-cookie](https://github.com/savashn/ecewo-modules/tree/main/cookie) for cookie management
- [ecewo-session](https://github.com/savashn/ecewo-modules/tree/main/session) for session-based authentication
- [ecewo-cors](https://github.com/savashn/ecewo-modules/tree/main/postgres) for CORS implementation
- [ecewo-helmet](https://github.com/savashn/ecewo-modules/tree/main/postgres) for security headers
- [cJSON](https://github.com/DaveGamble/cJSON) for handling JSON objects
- [slugify-c](https://github.com/savashn/slugify-c) for creating URL-friendly ASCII characters
- [dotenv-c](https://github.com/Isty001/dotenv-c) for managing environment variables
- [libsodium](https://github.com/jedisct1/libsodium) for password hashing with `argon2`

## Requirements

- CMake version 3.14 or higher
- [libpq](https://www.postgresql.org/docs/current/libpq.html)
- [libsodium](https://github.com/jedisct1/libsodium) (Not required on Windows, as it's already included in the `vendors/libsodium-win64` folder)

## Installation

### 1. Clone the repo:

```shell
git clone https://github.com/savashn/ecewo-example.git
cd ecewo-example
```

### 2. Configure .env

Before compiling the program, create a `.env` file in the project's root directory and define the following environment variables. Otherwise, you may encounter a segmentation fault at startup.

```
PORT
DB_HOST
DB_PORT
DB_NAME
DB_USER
DB_PASSWORD
```

### 3. Build and run the project

### 3.1 Build via Bash Script

You can build with the following command:

```shell
./build.sh
```

If you make some changes on the project and would lite to build from scratch:

```shell
./build.sh rebuild
```

### 3.2 Build Manually

If you prefer to build manually, run the suitable command:

Manually building on Windows:

```shell
mkdir build && cd build && cmake .. && cmake --build . && ./server.exe
```

Manually building on Linux/macOS:

```shell
mkdir build && cd build && cmake .. && cmake --build . && ./server
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
