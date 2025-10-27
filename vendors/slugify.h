// MIT License

// Copyright (c) 2025 Savas Sahin

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef SLUGIFY_H
#define SLUGIFY_H

#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SLUGIFY_SUCCESS 0
#define SLUGIFY_ERROR_BUFFER 1
#define SLUGIFY_ERROR_INVALID 2
#define SLUGIFY_ERROR_EMPTY 3
#define SLUGIFY_ERROR_MEMORY 4

typedef struct
{
    char separator;     /* Default: '-' */
    size_t max_length;  /* Max output length, 0 = no limit */
    bool preserve_case; /* true to preserve case, false to convert to lowercase (default) */
} Slugify;

char *slugify(const char *input, const Slugify *options);

#ifdef __cplusplus
}
#endif

#endif
