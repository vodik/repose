#pragma once

#define NOCOLOR     "\033[0m"
#define BOLD        "\033[1m"

#define BLACK       "\033[0;30m"
#define RED         "\033[0;31m"
#define GREEN       "\033[0;32m"
#define YELLOW      "\033[0;33m"
#define BLUE        "\033[0;34m"
#define MAGENTA     "\033[0;35m"
#define CYAN        "\033[0;36m"

#define BOLDBLACK   "\033[1;30m"
#define BOLDRED     "\033[1;31m"
#define BOLDGREEN   "\033[1;32m"
#define BOLDYELLOW  "\033[1;33m"
#define BOLDBLUE    "\033[1;34m"
#define BOLDMAGENTA "\033[1;35m"
#define BOLDCYAN    "\033[1;36m"

#define _unused_       __attribute__((unused))
#define _noreturn_     __attribute__((noreturn))
#define _printf_(a,b)  __attribute__((format (printf, a, b)))
#define _cleanup_(x)   __attribute__((cleanup(x)))
#define _destructor_   __attribute__((destructor))
#define _likely_(x)    __builtin_expect(!!(x), 0)
#define _unlikely_(x)  __builtin_expect(!!(x), 1)
