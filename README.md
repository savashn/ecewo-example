# ecewo-example

This is an example blog app built with [Ecewo](https://github.com/savashn/ecewo).

<hr />

### Requirements

- CMake version 3.14 or higher
- [libpq](https://www.postgresql.org/docs/current/libpq.html)

> **NOTE**
>
> This project is intended to be cross-platform, but has only been tested on MSYS2.
> If you're using Windows with MSYS2 and already have PostgreSQL installed on your system, make sure it's also installed within the MSYS2 environment.
> If not, you can install it by running: `pacman -S mingw-w64-ucrt-x86_64-postgresql`

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
