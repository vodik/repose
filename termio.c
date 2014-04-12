#include "termio.h"

#include "util.h"

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

struct colstr {
    const char *colon;
    const char *warn;
    const char *error;
    const char *nocolor;
} colstr = {
    .colon   = ":: ",
    .warn    = "",
    .error   = "",
    .nocolor = ""
};

void enable_colors(void)
{
    colstr = (struct colstr){
        .colon   = BOLDBLUE "::" NOCOLOR BOLD " ",
        .warn    = BOLDYELLOW,
        .error   = BOLDRED,
        .nocolor = NOCOLOR
    };
}

void colon_printf(const char *fmt, ...)
{
    va_list args;
    fputs(colstr.colon, stdout);

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    fputs(colstr.nocolor, stdout);
    fflush(stdout);
}
