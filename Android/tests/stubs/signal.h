#pragma once
struct siginfo_t { void* si_addr; };
#define SA_SIGINFO 4
#define SA_ONSTACK 0x08000000
#define SIGSEGV 11
#define SIGBUS 7
#define SIGILL 4
#define SIGFPE 8
#define SIGABRT 6
struct sigaction { void (*sa_sigaction)(int, siginfo_t*, void*); int sa_flags; unsigned long sa_mask[16]; };
extern "C" int sigaction(int, const struct sigaction*, struct sigaction*);
extern "C" int sigemptyset(void*);
