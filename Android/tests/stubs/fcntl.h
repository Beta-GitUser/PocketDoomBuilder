#pragma once
#define O_WRONLY 1
#define O_CREAT 64
#define O_TRUNC 512
extern "C" int open(const char*, int, ...);
