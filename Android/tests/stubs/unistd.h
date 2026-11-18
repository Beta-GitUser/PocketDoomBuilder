#pragma once
#include <cstddef>
typedef long ssize_t;
extern "C" ssize_t write(int, const void*, size_t);
extern "C" int close(int);
extern "C" void _exit(int);
