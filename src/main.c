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
    bool addColor = false;
    char* color = 0;
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
        int saved_in = dup(STDIN_FILENO);
        int saved_out = dup(STDOUT_FILENO);

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

        // check for git directories and will put number of modified files
        char* modified = malloc(sizeof(char*));
        bool git = false;
        int git_fds[2];

        pipe(git_fds);
        pid_t child = fork();

        if (child == 0) {
            dup2(git_fds[1], STDOUT_FILENO); // direct stdout to git_fds[1]
            dup2(git_fds[1], STDERR_FILENO);
            close(git_fds[0]);
            close(git_fds[1]);
            char* direct[] = {"git", "rev-parse", "--is-inside-work-tree", NULL};
            if (execvp(direct[0], direct) == -1) {
                printf(EXEC_NOT_FOUND, direct[0]);
                continue;
            }
        } else if (child < 0) {
            printf(BUILTIN_ERROR, "Could not fork child");
            continue;
        } else {
            // parent
            close(git_fds[1]);
            char* directory = calloc(1, sizeof(char*));

            read(git_fds[0], directory, sizeof(directory));

            if (strcmp(directory, "true\n") == 0) {
                git = true;

            }

            int status;
            free(directory);
            waitpid(child, &status, WUNTRACED);
        }

        if (git) {
            int fds[2];
            pipe(fds);
            // count number of modified files
            pid_t pid = fork();
            if (pid == 0) {
                dup2(fds[1], STDOUT_FILENO); // direct stdout to git_fds[1]
                close(fds[0]);
                close(fds[1]);
                char* args[] = {"git", "diff", "--stat", NULL};

                if (execvp(args[0], args) == -1) {
                    printf(EXEC_NOT_FOUND, args[0]);
                    continue;
                }
            } else if (pid < 0) {
                printf(BUILTIN_ERROR, "Could not fork git child");
                continue;
            } else {
                close(fds[1]);
                char directory[512] = {0};
                read(fds[0], directory, sizeof(directory));

                char* changed = strstr(directory, "changed");
                if (changed == NULL) {
                    modified = "0 ";
                } else {
                    char* numFiles = changed - 7;
                    if (*numFiles == ' ') {
                        numFiles--; // in case it's 'files'
                    }

                    char* num = strtok(numFiles, " \t");

                    strcpy(modified, num);
                    strcat(modified, " ");
                }
                int status;
                waitpid(pid, &status, WUNTRACED);
            }
        }

        dup2(saved_in, STDIN_FILENO);
        dup2(saved_out, STDOUT_FILENO);

        if (addColor) {
            strcat(prompt, color);
        }
        //strcat(prompt, KRED);
        if (git) {
            strcat(prompt, modified);
        }

        strcat(prompt, cwd);
        strcat(prompt, colons);
        strcat(prompt, netid);
        strcat(prompt, arrows);
        //strcat(prompt, KNRM);
        if (addColor) {
            strcat(prompt, KNRM);
        }

        char* home = getenv("HOME");
        char* ret = strstr(prompt, home);
        //char* proptr = prompt;

        if (ret != NULL) { // if substring for HOME variable exists in prompt
            char* dest = ret + 1;
            char* src = ret + strlen(home);
            int length = strlen(prompt) - strlen(home); // length of prompt - length of home path
            *ret = squiggly;
            memmove(dest, src, length);

            // set the rest to nulls
            char* rest = ret + 1 + length; // beginning of prompt + '~' + length(rest of prompt)
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
        bool piper = false;
        // NOTES ~> FD
        // stdin, 0
        // stdout, 1
        // args[index - 1] is file name

        int file;
        int file2;

        // check if things work out
        int numRed = 0; // keep count of how many redirection symbols there are
        int numPipes = 0; // keep count of pipes
        int cnt = 0;
        bool err = false;
        while (args[cnt] != NULL) {
            if (strcmp(args[cnt], LEFT_ANGLE) == 0 ||
                strcmp(args[cnt], RIGHT_ANGLE) == 0 ||
                strcmp(args[cnt], PIPE) == 0) {
                // if we find redirection symbols
                if (strcmp(args[cnt], PIPE) != 0) {
                    numRed++;
                } else {
                    numPipes++;
                }
                if (index < 3) {
                    printf(SYNTAX_ERROR, "No redirection available");
                    err = true;
                    break;
                }
            }
            cnt++;
        }

        if (err) continue;

        if (numRed > 0) {
            redirect = true;
        } else if (numPipes > 0) {
            piper = true;
        }

        int pipes[2*numPipes];

        // redirection
        if (redirect) {
            if (index >= 3) {
                if (numRed == 1) {
                    if (strcmp(args[index - 2], LEFT_ANGLE) == 0) { // prog [args] > output.txt
                        // >
                        file = open(args[index - 1], O_WRONLY | O_TRUNC | O_CREAT,  S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);

                        if (file < 0) {
                            printf(SYNTAX_ERROR, "Error opening file");
                            continue;
                        }

                        if (dup2(file, STDOUT_FILENO) < 0) {
                            printf(SYNTAX_ERROR, "Error with dup2 - >");
                            continue;
                        }

                        cnt = index - 2;
                        // replace stdout with output file

                    } else if (strcmp(args[index - 2], RIGHT_ANGLE) == 0) { // prog [args] < input.txt
                        // <
                        file = open(args[index - 1], O_RDONLY);

                        if (file < 0) {
                            printf(SYNTAX_ERROR, "Error opening file - Does not exist");
                            continue;
                        }

                        if (dup2(file, STDIN_FILENO) < 0) {
                            printf(SYNTAX_ERROR, "Error with dup2 - <");
                            continue;
                        }

                        cnt = index - 2;

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

                                    if (dup2(file, STDOUT_FILENO) < 0 || dup2(file2, STDIN_FILENO) < 0) {
                                        printf(SYNTAX_ERROR, "Error with dup2 - prog [args] > output.txt < input.txt");
                                        continue;
                                    }

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

                                    if (dup2(file, STDOUT_FILENO) < 0 || dup2(file2, STDIN_FILENO) < 0) {
                                        printf(SYNTAX_ERROR, "Error with dup2 - prog [args] < input.txt > output.txt");
                                        continue;
                                    }
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
            // manipulate args
            int i;
            for (i = cnt; i < index; i++) {
                args[i] = NULL;
            }
        } else if (piper) {
            // piping here
            int i;
            for (i = 0; i < (2*numPipes); i += 2) {
                if (pipe(pipes + i) < 0) {
                    printf(SYNTAX_ERROR, "Piping went wrong");
                    continue;
                }
            }

            int times = numPipes + 1;
            int k = 0;
            char** pipeargs[times];

            int track;
            int inner = 0;
            for (track = 0; track < times; track++) {
                int cnt = 0;
                pipeargs[track] = calloc(10, sizeof(char*));
                if (pipeargs[track] == NULL) {
                    printf(SYNTAX_ERROR, "Piping went wrong with pipeargs");
                    continue;
                }

                if (track + 1 == times) {
                    // last one, so no pipe
                    while (args[inner] != NULL) {
                        pipeargs[track][cnt] = args[inner];
                        cnt++; // will reset to 0 for each loop
                        inner++;
                    }

                    pipeargs[track][cnt] = NULL;
                } else {
                    while (strcmp(args[inner], PIPE) != 0) {
                        // while we don't encounter a pipe symbol, add to pipeargs[track]
                        pipeargs[track][cnt] = args[inner];
                        cnt++; // will reset to 0 for each loop
                        inner++;
                    }

                    pipeargs[track][cnt] = NULL;

                    inner++;
                }

            }

            i = 0;
            while (times) {
                // manipulate args here
                pid_t pid = fork();

                if (pid == 0) {
                    // child process
                    /*int k = 0;
                    while (pipeargs[i][k] != NULL) {
                        printf("%d %s\n", i, pipeargs[i][k]);
                        k++;
                    }*/

                    if (times == (numPipes + 1)) {
                        // first fork
                        if (dup2(pipes[1], STDOUT_FILENO) < 0) {
                            printf(SYNTAX_ERROR, "Error with dup2 - piping 1");
                            break;
                        }
                    } else if (times != 1) {
                        // last fork
                        // stdout
                        if (dup2(pipes[2*numPipes - 2], STDIN_FILENO) < 0) {
                            printf(SYNTAX_ERROR, "Error with dup2 - piping fudge");
                            break;
                        }
                    } else {
                        // anything inbetween
                        // input
                        if (dup2(pipes[2*k - 2], STDIN_FILENO) < 0) {
                            printf(SYNTAX_ERROR, "Error with dup2 - piping fudge");
                            break;
                        }
                        // output
                        if (dup2(pipes[2*k + 1], STDOUT_FILENO) < 0) {
                            printf(SYNTAX_ERROR, "Error with dup2 - piping fudge");
                            break;
                        }
                    }

                    // close all ends of pipes
                    for (i = 0; i < 2*numPipes; i++) {
                        close(pipes[i]);
                    }

                    // execution here
                    // holy crap this code is dank
                    if (strcmp(pipeargs[i][0], "help") == 0) {
                        // help menu
                        int stat;
                        pid_t pid = fork();

                        if (pid == 0) {
                            // child process
                            char* help = "[HELP MENU HERE]\n";
                            write(STDOUT_FILENO, help, strlen(help));
                            exit(0);
                        } else if (pid < 0) {
                            // failed
                            dup2(saved_out, STDOUT_FILENO);
                            printf(BUILTIN_ERROR, "Failed fork for help");
                            break;
                        } else {
                            // parent
                            waitpid(pid, &stat, WUNTRACED);
                        }
                    } else if (strcmp(pipeargs[i][0], "exit") == 0) {
                        // exit
                        exited = true;
                        exit(0);
                    } else if (strcmp(pipeargs[i][0], "pwd") == 0) {
                        // pwd
                        pid_t pid = fork();
                        int stat;
                        if (pid == 0) {
                            char* pwdptr = getcwd(NULL, 0); // small trick
                            char* pwd = calloc(1, strlen(pwdptr) + 2);

                            if (pwd == NULL) { // invalid calloc pointer
                                dup2(saved_out, STDOUT_FILENO);
                                printf(BUILTIN_ERROR, "Could not allocate memory for pwd");
                                break;
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
                            break;
                        } else {
                            waitpid(pid, &stat, WUNTRACED);
                        }

                    } else if (strcmp(pipeargs[i][0], "cd") == 0) {
                        // cd builtin
                        if (pipeargs[i][1] == NULL) {
                            // one argument
                            char* cwd = getcwd(NULL, 0);
                            int ret = chdir(getenv("HOME"));

                            if (ret != 0) {
                                dup2(saved_out, STDOUT_FILENO);
                                printf(BUILTIN_ERROR, "HOME variable not set");
                                break;
                            }

                            setenv("PWD", getenv("HOME"), 1);
                            setenv("OLDPWD", cwd, 1);
                        } else if (strcmp(pipeargs[i][1], "-") == 0) {
                            // cd -
                            char* oldpwd = getenv("OLDPWD");
                            char* cwd = getcwd(NULL, 0);

                            if (strcmp(oldpwd, "") == 0) {
                                dup2(saved_out, STDOUT_FILENO);
                                printf(BUILTIN_ERROR, "OLDPWD not set");
                                break;
                            }

                            chdir(oldpwd);

                            setenv("PWD", getenv("OLDPWD"), 1);
                            setenv("OLDPWD", cwd, 1);
                        } else {
                            char* cwd = getcwd(NULL, 0);
                            int ret = chdir(pipeargs[i][1]);
                            if (ret != 0) {
                                dup2(saved_out, STDOUT_FILENO);
                                printf(BUILTIN_ERROR, "No such file or directory");
                                break;
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
                            break;
                        }

                        pid_t pid = fork();
                        if (pid == 0) {
                            // child process
                            if (execvp(pipeargs[i][0], pipeargs[i]) == -1) {
                                dup2(saved_out, STDOUT_FILENO);
                                printf(EXEC_NOT_FOUND, args[0]);
                                break;
                            }
                            exit(0);
                        } else if (pid < 0) {
                            // failed fork returns -1
                            dup2(saved_out, STDOUT_FILENO);
                            printf(EXEC_ERROR, "Failed fork");
                            break;
                        } else {
                            // parent fork
                            //waitpid(pid, &stat, WUNTRACED);
                            sigsuspend(&oldmask);
                            int unblock = sigprocmask(SIG_UNBLOCK, &mask, NULL);
                            if (unblock != 0) {
                                dup2(saved_out, STDOUT_FILENO);
                                printf(EXEC_ERROR, "Couldn't unblock mask signals");
                                break;
                            }
                        }
                    }
                } else if (pid < 0) {
                    // failed process
                    printf(SYNTAX_ERROR, "Piping hot fork");
                    break;
                } else {
                    int status;
                    waitpid(pid, &status, WUNTRACED);
                }

                i++;
                k++;
                times--;
            }
        }


        if (!piper) {
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
            } else if (strcmp(args[0], "color") == 0) {
                // color
                if (strcmp(args[1], "RED") == 0) {
                    addColor = true;
                    color = KRED;
                } else if (strcmp(args[1], "GRN") == 0) {
                    addColor = true;
                    color = KGRN;
                } else if (strcmp(args[1], "YEL") == 0) {
                    addColor = true;
                    color = KYEL;
                } else if (strcmp(args[1], "BLU") == 0) {
                    addColor = true;
                    color = KBLU;
                } else if (strcmp(args[1], "MAG") == 0) {
                    addColor = true;
                    color = KMAG;
                } else if (strcmp(args[1], "CYN") == 0) {
                    addColor = true;
                    color = KCYN;
                } else if (strcmp(args[1], "WHT") == 0) {
                    addColor = true;
                    color = KWHT;
                } else if (strcmp(args[1], "BWN") == 0) {
                    addColor = true;
                    color = KBWN;
                } else {
                    printf(BUILTIN_ERROR, "Could not change color");
                    continue;
                }

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
                    }
                    exit(0);
                } else if (pid < 0) {
                    // failed fork returns -1
                    dup2(saved_out, STDOUT_FILENO);
                    printf(EXEC_ERROR, "Failed fork");
                } else {
                    // parent fork
                    //waitpid(pid, &stat, WUNTRACED);
                    sigsuspend(&oldmask);
                    int unblock = sigprocmask(SIG_UNBLOCK, &mask, NULL);
                    if (unblock != 0) {
                        dup2(saved_out, STDOUT_FILENO);
                        printf(EXEC_ERROR, "Couldn't unblock mask signals");
                    }
                }
            }
        }

        if (redirect) {
            dup2(saved_in, STDIN_FILENO);
            dup2(saved_out, STDOUT_FILENO);
            close(file);
            close(file2);
        }

        if (piper) {
            int i;
            int status;
            for (i = 0; i < (2*numPipes); i++) {
                close(pipes[i]);
            }

            for (i = 0; i < numPipes; i++) {
                wait(&status);
            }
        }

        // Readline mallocs the space for input. You must free it.
        free(args);
        free(cwd);
        free(modified);
        free(cwdptr);
        rl_free(input);

    } while(!exited); // while exited == 0

    debug("%s", "user entered 'exit'");


    return EXIT_SUCCESS;
}

void child_handler(int sig) {
    int status;
    // wnohang
    waitpid((pid_t) -1, &status, WNOHANG);
}
