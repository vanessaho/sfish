#ifndef SFISH_H
#define SFISH_H

/* Format Strings */
#define EXEC_NOT_FOUND "sfish: %s: command not found\n"
#define JOBS_LIST_ITEM "[%d] %s\n"
#define STRFTIME_RPRMT "%a %b %e, %I:%M%p"
#define BUILTIN_ERROR  "sfish builtin error: %s\n"
#define SYNTAX_ERROR   "sfish syntax error: %s\n"
#define EXEC_ERROR     "sfish exec error: %s\n"

// REDIRECTION STRINGS
#define LEFT_ANGLE  ">"
#define RIGHT_ANGLE "<"
#define PIPE "|"

// colors
#ifndef COLOR
#define COLOR
#define KNRM "\033[0m"
#define KRED "\033[1;31m"
#define KGRN "\033[1;32m"
#define KYEL "\033[1;33m"
#define KBLU "\033[1;34m"
#define KMAG "\033[1;35m"
#define KCYN "\033[1;36m"
#define KWHT "\033[1;37m"
#define KBWN "\033[0;33m"
#endif

#define COLOR_SIZE(COLOR) strlen(COLOR)

#define HELP() do{                                                        \
fprintf(stdout, "%s\n",                                                         \
        "help: Prints a list of all builtins and basic usage\n" \
        "exit: Exits the shell\n" \
        "cd  : [-|.|..|directory] Changes the current working directory\n" \
        "pwd : Prints absolute path of the current working directory\n");                                                                                                     \
} while(0)

void child_handler(int sig);

#endif
