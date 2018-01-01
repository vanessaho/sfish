#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <readline/readline.h>

#include "sfish.h"
#include "debug.h"

int main(int argc, char *argv[], char* envp[]) {
    char* input;
    bool exited = false;
    setenv("OLDPWD", "", 1); // to set oldpwd environmental variable to null

    if(!isatty(STDIN_FILENO)) { // returns 1 if terminal, 0 if not (ie: file)
        // If your shell is reading from a piped file
        // Don't have readline write anything to that file.
        // Such as the prompt or "user input"
        if((rl_outstream = fopen("/dev/null", "w")) == NULL){
            perror("Failed trying to open DEVNULL");
            exit(EXIT_FAILURE);
        }

    }

    do {

        char* cwdptr = getcwd(NULL, 0); // small trick
        char* cwd = malloc(strlen(cwdptr) + 1);
        if (cwd == NULL) {
            printf(BUILTIN_ERROR, "Could not allocate memory for current working directory");
            continue;
        }
        strncpy(cwd, cwdptr, strlen(cwdptr));
        memset(cwd + strlen(cwdptr), '\0', 1);
        char* colons = " :: ";
        char* netid = "pho";
        char* arrows = " >> ";
        char  squiggly = '~';

        char prompt[512];
        char* ptr = prompt;
        while (*ptr != 0) {
            *ptr = 0;
            ptr++;
        }

        strcat(prompt, cwd);
        strcat(prompt, colons);
        strcat(prompt, netid);
        strcat(prompt, arrows);

        char* home = getenv("HOME");
        char* ret = strstr(prompt, home);
        char* proptr = prompt;

        if (ret != NULL) { // if substring for HOME variable exists in prompt
            char* dest = proptr + 1;
            char* src = proptr + strlen(home);
            int length = strlen(prompt) - strlen(home); // length of prompt - length of home path
            *proptr = squiggly;
            memmove(dest, src, length);

            // set the rest to nulls
            char* rest = proptr + 1 + length; // beginning of prompt + '~' + length(rest of prompt)
            memset(rest, 0, length);
        }

        input = readline(prompt);

        // If EOF is read (aka ^D) readline returns NULL
        if (input == NULL || strcmp(input, "") == 0) {
            continue;
        }

        // write(1 is stdout, str, strlen(str))
        // create args variable
        char** args = calloc(1, sizeof(char*)); // pointer to args string
        // checking if args is null
        if (args == NULL) {
            printf(BUILTIN_ERROR, "Could not allocate memory for argv");
            continue;
        }

        char* token = strtok(input, " \t");
        int index = 0;

        while (token != NULL) {
            args[index] = token; // put token string at args[index]
            index++;
            args = realloc(args, (index+1)*sizeof(char*));

            if (args == NULL) {
                printf(BUILTIN_ERROR, "Could not reallocate memory for argv");
            }

            token = strtok(NULL, " \t"); // transition to next token
        }

        args[index] = NULL; // null terminator at end of string; index becomes the size of args :)

        // check for redirectional symbols
        //char* redirections[3] = {LEFT_ANGLE, RIGHT_ANGLE, PIPE};
        bool redirect = false;
        // NOTES ~> FD
        // stdin, 0
        // stdout, 1
        // args[index - 1] is file name
        int saved_in = dup(STDIN_FILENO);
        int saved_out = dup(STDOUT_FILENO);
        int file;
        int file2;

        // check if things work out
        int num = 0; // keep count of how many redirection symbols there are
        int cnt = 0;
        bool err = false;
        while (args[cnt] != NULL) {
            if (strcmp(args[cnt], LEFT_ANGLE) == 0 ||
                strcmp(args[cnt], RIGHT_ANGLE) == 0 ||
                strcmp(args[cnt], PIPE) == 0) {
                // if we find redirection symbols
                if (strcmp(args[cnt], PIPE) != 0) num++;
                if (index < 3) {
                    printf(SYNTAX_ERROR, "No redirection available");
                    err = true;
                    break;
                }
            }
            cnt++;
        }

        if (err) continue;

        // find out if there's redirection going on :)
        if (index >= 3) {
            if (num == 1) {
                if (strcmp(args[index - 2], LEFT_ANGLE) == 0) { // prog [args] > output.txt
                    // >
                    file = open(args[index - 1], O_WRONLY | O_TRUNC | O_CREAT,  S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);

                    if (file < 0) {
                        printf(SYNTAX_ERROR, "Error opening file");
                        continue;
                    }

                    dup2(file, STDOUT_FILENO);

                    cnt = index - 2;
                    // replace stdout with output file
                    redirect = true;
                } else if (strcmp(args[index - 2], RIGHT_ANGLE) == 0) { // prog [args] < input.txt
                    // <
                    file = open(args[index - 1], O_RDONLY);

                    if (file < 0) {
                        printf(SYNTAX_ERROR, "Error opening file - Does not exist");
                        continue;
                    }

                    dup2(file, STDIN_FILENO);

                    cnt = index - 2;

                    redirect = true;
                } else {
                    printf(SYNTAX_ERROR, "Misplacement of redirection symbol");
                    continue;
                }
            } else { // more than one redirection symbol
                // prog [args] < input.txt > output.txt
                // prog [args] > output.txt < input.txt
                // find indexes of both redirection symbols

                cnt = 0; // reset count
                bool err = false;
                while (args[cnt] != NULL) {
                    if (strcmp(args[cnt], LEFT_ANGLE) == 0) {
                        // >
                        if (cnt + 3 < index) {
                            if (strcmp(args[cnt + 2], RIGHT_ANGLE) == 0) {
                                // prog [args] > output.txt < input.txt
                                // output file
                                file = open(args[cnt + 1], O_WRONLY | O_TRUNC | O_CREAT,  S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
                                if (file < 0) {
                                    printf(SYNTAX_ERROR, "Error opening file with double redirection");
                                    continue;
                                }
                                // input file
                                file2 = open(args[cnt + 3], O_RDONLY);
                                if (file2 < 0) {
                                    printf(SYNTAX_ERROR, "Error opening file2 with double redirection");
                                    continue;
                                }

                                dup2(file, STDOUT_FILENO);
                                dup2(file2, STDIN_FILENO);

                                redirect = true;
                            } else {
                                printf(SYNTAX_ERROR, "Bad placement of redirection symbol - >");
                                err = true;
                            }
                        } else {
                            // bad syntax
                            printf(SYNTAX_ERROR, "Misplacement of redirection symbol - >");
                            err = true;
                        }

                        break;
                    } else if (strcmp(args[cnt], RIGHT_ANGLE) == 0) {
                        // <
                        if (cnt + 3 < index) {
                            if (strcmp(args[cnt + 2], LEFT_ANGLE) == 0) {
                                // prog [args] < input.txt > output.txt
                                // output file
                                file = open(args[cnt + 3], O_WRONLY | O_TRUNC | O_CREAT,  S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
                                if (file < 0) {
                                    printf(SYNTAX_ERROR, "Error opening file with double redirection");
                                    continue;
                                }
                                // input file
                                file2 = open(args[cnt + 1], O_RDONLY);
                                if (file2 < 0) {
                                    printf(SYNTAX_ERROR, "Error opening file2 with double redirection");
                                    continue;
                                }

                                dup2(file, STDOUT_FILENO);
                                dup2(file2, STDIN_FILENO);

                                redirect = true;
                            } else {
                                printf(SYNTAX_ERROR, "Bad placement of redirection symbol - <");
                                err = true;
                            }
                        } else {
                            // bad syntax
                            printf(SYNTAX_ERROR, "Misplacement of redirection symbol - <");
                            err = true;
                        }

                        break;
                    }

                    cnt++;
                }

                if (err) continue;
            }
        }
        // execute args
        if (redirect) {
            int i;
            for (i = cnt; i < index; i++) {
                args[i] = NULL;
            }
        }

        if (strcmp(args[0], "help") == 0) {
            // help menu
            int stat;
            pid_t pid = fork();

            if (pid == 0) {
                // child process
                //char* help = "[HELP MENU HERE]\n";
                //write(STDOUT_FILENO, help, strlen(help));
                HELP();
                exit(0);
            } else if (pid < 0) {
                // failed
                dup2(saved_out, STDOUT_FILENO);
                printf(BUILTIN_ERROR, "Failed fork for help");
                continue;
            } else {
                // parent
                waitpid(pid, &stat, WUNTRACED);
            }
        } else if (strcmp(args[0], "exit") == 0) {
            // exit
            exited = true;
            exit(0);
        } else if (strcmp(args[0], "pwd") == 0) {
            // pwd
            pid_t pid = fork();
            int stat;
            if (pid == 0) {
                char* pwdptr = getcwd(NULL, 0); // small trick
                char* pwd = calloc(1, strlen(pwdptr) + 2);

                if (pwd == NULL) { // invalid calloc pointer
                    dup2(saved_out, STDOUT_FILENO);
                    printf(BUILTIN_ERROR, "Could not allocate memory for pwd");
                    continue;
                }

                strncpy(pwd, pwdptr, strlen(pwdptr));
                memset(pwd + strlen(pwdptr), '\n', 1); // add new line
                memset(pwd + strlen(pwdptr) + 1, '\0', 1); // add null char to end

                write(STDOUT_FILENO, pwd, strlen(pwd)); // write to stdout

                free(pwd); // free up calloc call
                exit(0);
            } else if (pid < 0) {
                dup2(saved_out, STDOUT_FILENO);
                printf(BUILTIN_ERROR, "Failed fork for pwd");
                continue;
            } else {
                waitpid(pid, &stat, WUNTRACED);
            }

        } else if (strcmp(args[0], "cd") == 0) {
            // cd builtin
            if (args[1] == NULL) {
                // one argument
                char* cwd = getcwd(NULL, 0);
                int ret = chdir(getenv("HOME"));

                if (ret != 0) {
                    dup2(saved_out, STDOUT_FILENO);
                    printf(BUILTIN_ERROR, "HOME variable not set");
                    continue;
                }

                setenv("PWD", getenv("HOME"), 1);
                setenv("OLDPWD", cwd, 1);
            } else if (strcmp(args[1], "-") == 0) {
                // cd -
                char* oldpwd = getenv("OLDPWD");
                char* cwd = getcwd(NULL, 0);

                if (strcmp(oldpwd, "") == 0) {
                    dup2(saved_out, STDOUT_FILENO);
                    printf(BUILTIN_ERROR, "OLDPWD not set");
                    continue;
                }

                chdir(oldpwd);

                setenv("PWD", getenv("OLDPWD"), 1);
                setenv("OLDPWD", cwd, 1);
            } else {
                char* cwd = getcwd(NULL, 0);
                int ret = chdir(args[1]);
                if (ret != 0) {
                    dup2(saved_out, STDOUT_FILENO);
                    printf(BUILTIN_ERROR, "No such file or directory");
                    continue;
                }

                setenv("PWD", getcwd(NULL, 0), 1);
                setenv("OLDPWD", cwd, 1);
            }

        } else {
            // executables
            signal(SIGCHLD, child_handler);

            sigset_t mask, oldmask;

            sigemptyset(&mask);
            sigaddset(&mask, SIGCHLD);

            int block = sigprocmask(SIG_BLOCK, &mask, &oldmask);
            if (block != 0) {
                printf(EXEC_ERROR, "Couldn't block mask signals");
                continue;
            }

            pid_t pid = fork();
            if (pid == 0) {
                // child process
                if (execvp(args[0], args) == -1) {
                    dup2(saved_out, STDOUT_FILENO);
                    printf(EXEC_NOT_FOUND, args[0]);
                    continue;
                }
                exit(0);
            } else if (pid < 0) {
                // failed fork returns -1
                dup2(saved_out, STDOUT_FILENO);
                printf(EXEC_ERROR, "Failed fork");
                continue;
            } else {
                // parent fork
                //waitpid(pid, &stat, WUNTRACED);
                sigsuspend(&oldmask);
                int unblock = sigprocmask(SIG_UNBLOCK, &mask, NULL);
                if (unblock != 0) {
                    dup2(saved_out, STDOUT_FILENO);
                    printf(EXEC_ERROR, "Couldn't unblock mask signals");
                    continue;
                }
            }
        }

        dup2(saved_in, STDIN_FILENO);
        dup2(saved_out, STDOUT_FILENO);
        close(file);
        close(file2);

        // Readline mallocs the space for input. You must free it.
        free(args);
        free(cwd);
        free(cwdptr);
        rl_free(input);

    } while(!exited); // while exited == 0

    debug("%s", "user entered 'exit'");


    return EXIT_SUCCESS;
}

void child_handler(int sig) {
    int status;
    waitpid((pid_t) -1, &status, WNOHANG);
}
