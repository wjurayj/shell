#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "./jobs.h"

#define BUFFER_SIZE 1024
#define DELIM " \t\n"

// global variable
job_list_t *job_list;
int num_jobs = 0;

/*
this function separates a string by white space and stores it in an array

str - the string to be parsed
tokens - an array whose length is the num_tokens, where the tokens from str will
  be stored
num_tokens - the number of tokens in str
*/
void parse(char *str, char **tokens, int num_tokens) {
    tokens[0] = strtok(str, DELIM);
    for (int i = 1; i < num_tokens; i++) {
        tokens[i] = strtok(NULL, DELIM);
    }
}

/*
This function counts the number of tokens in a string without modifying it

str - the string whose tokens will be counted
delim - the delimiting characters that will separate tokens
size - the length of string str, not including the terminating null byte
returns the number of tokens in str
*/
int count_tokens(char *str, char *delim, size_t size) {
    char cpy[size + 1];
    strncpy(cpy, str, size + 1);
    int count = 0;
    if (strtok(cpy, delim)) count++;
    while (strtok(NULL, delim)) count++;

    return count;
}

/*
This function handles builtin function calls.

argc - the number of elements in argv
argv - the array of the users shell arguments, with redirection arguments
  removed
return 1 if the first element of argv was a builtin function, 0 otherwise
*/
int builtin_handler(int argc, char **argv) {
    if (!strcmp(argv[0], "cd")) {
        if (argc < 2) {
            fprintf(stderr, "cd: syntax error\n");
        } else if (chdir(argv[1])) {
            perror("cd");
        }
    } else if (!strcmp(argv[0], "rm")) {
        if (argc < 2) {
            fprintf(stderr, "rm: syntax error\n");
        } else if (unlink(argv[1])) {
            perror("rm");
        }
    } else if (!strcmp(argv[0], "ln")) {
        if (argc < 3) {
            fprintf(stderr, "ln: syntax error\n");
        } else if (link(argv[1], argv[2])) {
            perror("ln");
        }
    } else if (!strcmp(argv[0], "jobs")) {
        jobs(job_list);
    } else if (!strcmp(argv[0], "fg")) {
        if (argc < 2) {
            fprintf(stderr, "fg: syntax error\n");
            return 1;
        }
        argv[1]++;
        int jid = atoi(argv[1]);
        int pid = get_job_pid(job_list, jid);
        if (pid == -1) {
            fprintf(stderr, "job not found\n");
        } else {
            if (update_job_pid(job_list, pid, RUNNING) == -1) {
                fprintf(stderr, "could not update job state");
            }
            if (kill(-pid, SIGCONT) == -1) {
                perror("kill");
            }
            if (tcsetpgrp(0, getpgid(pid))) {
                perror("tcsetpgrp");
                return 1;
            }
            int wstatus = 0;
            int ret = waitpid(pid, &wstatus, WUNTRACED);
            if (ret == -1) {
                perror("waitpid");
            } else if (ret) {
                if (WIFEXITED(wstatus)) {
                    if (remove_job_pid(job_list, pid) == -1) {
                        fprintf(stderr, "could not remove job\n");
                    }
                } else if (WIFSIGNALED(wstatus)) {
                    if (remove_job_pid(job_list, pid) == -1) {
                        fprintf(stderr, "could not remove job\n");
                    }
                    int esig = WTERMSIG(wstatus);
                    printf("[%d] (%d) terminated by signal %d\n", num_jobs, pid,
                           esig);
                } else if (WIFSTOPPED(wstatus)) {
                    if (update_job_pid(job_list, pid, STOPPED) == -1) {
                        fprintf(stderr, "could not update job state");
                    }
                    int ssig = WSTOPSIG(wstatus);
                    printf("[%d] (%d) suspended by signal %d\n", num_jobs, pid,
                           ssig);
                }
            }
            if (tcsetpgrp(0, getpgrp())) {
                perror("tcsetpgrp");
            }
        }
    } else if (!strcmp(argv[0], "bg")) {
        if (argc < 2) {
            fprintf(stderr, "bg: syntax error\n");
            return 1;
        }
        argv[1]++;
        int jid = atoi(argv[1]);
        int pid = get_job_pid(job_list, jid);
        if (pid == -1) {
            fprintf(stderr, "job not found\n");
        } else {
            if (kill(-pid, SIGCONT) == -1) {
                perror("kill");
            }
        }
    } else if (!strcmp(argv[0], "exit")) {
        cleanup_job_list(job_list);
        exit(0);
    } else
        return 0;
    return 1;
}

/*
this function handles calls to executable files

argv - the array of the users shell arguments, with redirection arguments
  removed and the last element being the NULL pointer
input - the name of the input file, NULL if there is no specified input file
output - the name of the output file, NULL if there is no specified output file
append - a boolean indicating whether output should be appended to the end of
  the output file (1 if append, 0 if truncate). Ignored if output == NULL.
*/
void executable_handler(char **argv, char *input, char *output, int append,
                        int foreground) {

    int pid = fork();
    if (pid == -1) {
        perror("fork");
        return;
    }
    if (!pid) {
        if (setpgid(0, 0) == -1) {
            perror("setpgid");
            cleanup_job_list(job_list);
            exit(1);
        }
        if (foreground) {
            if (tcsetpgrp(0, getpgrp())) {
                perror("tcsetpgrp");
                cleanup_job_list(job_list);
                exit(1);
            }
        }
        signal(SIGINT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        if (input) {
            if (close(STDIN_FILENO) == -1) {
                perror("close");
                cleanup_job_list(job_list);
                exit(1);
            }
            if (open(input, O_RDONLY) == -1) {
                perror("open");
                cleanup_job_list(job_list);
                exit(1);
            }
        }

        if (output) {
            if (close(STDOUT_FILENO) == -1) {
                perror("close");
                cleanup_job_list(job_list);
                exit(1);
            }
            int flags = O_CREAT | O_WRONLY;
            if (append) {
                flags = flags | O_APPEND;
            } else {
                flags = flags | O_TRUNC;
            }

            if (open(output, flags, 0600) == -1) {
                perror("open");
                cleanup_job_list(job_list);
                exit(1);
            }
        }

        char path[strlen(argv[0]) + 1];
        strcpy(path, argv[0]);
        char *token = strtok(argv[0], "/");
        char *iter = token;
        while (iter) {
            token = iter;
            iter = strtok(NULL, "/");
        }
        argv[0] = token;
        if (execv(path, argv) == -1) {
            perror("execv");
            cleanup_job_list(job_list);
            exit(1);
        }
        // kills the child process
        cleanup_job_list(job_list);
        exit(0);
    }
    if (foreground) {
        int wstatus = 0;
        int ret = waitpid(pid, &wstatus, WUNTRACED);
        if (ret == -1) {
            perror("waitpid");
        } else if (ret) {
            if (WIFSIGNALED(wstatus)) {
                int esig = WTERMSIG(wstatus);
                num_jobs++;
                printf("[%d] (%d) terminated by signal %d\n", num_jobs, pid,
                       esig);
            } else if (WIFSTOPPED(wstatus)) {
                if (add_job(job_list, ++num_jobs, pid, STOPPED, argv[0]) ==
                    -1) {
                    fprintf(stderr, "could not add job to job_list");
                }
                int ssig = WSTOPSIG(wstatus);
                printf("[%d] (%d) suspended by signal %d\n", num_jobs, pid,
                       ssig);
            }
        }
        // send signals back to this process
        if (tcsetpgrp(0, getpgrp())) {
            perror("tcsetpgrp");
        }
    } else {
        if (add_job(job_list, ++num_jobs, pid, RUNNING, argv[0]) == -1) {
            fprintf(stderr, "could not add job to job_list");
        } else {
            printf("[%d] (%d)\n", num_jobs, pid);
        }
    }
}

/*
This function redirects input & output, and then executes the shell commmand
appropriately

argc - the length of the array argv
argv - the array of the user's shell command arguments
returns 0 if no errors occurred, 1 otherwise
*/
int redirect(int argc, char **argv) {
    int argc2 = argc + 1;

    int found_in = 0;
    char *input_file = NULL;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "<")) {
            if (found_in) {
                fprintf(stderr, "syntax error: multiple input files\n");
                return 1;
            }
            if (i + 1 == argc) {
                fprintf(stderr, "syntax error: no input file\n");
                return 1;
            }
            if (!strcmp(argv[i + 1], "<") || !strcmp(argv[i + 1], ">") ||
                !strcmp(argv[i + 1], ">>")) {
                fprintf(stderr,
                        "syntax error: input file is redirection symbol\n");
                return 1;
            }
            input_file = argv[i + 1];
            found_in = 1;
            argc2 += -2;
        }
    }

    int found_out = 0;
    char *output_file = NULL;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], ">")) {
            if (found_out) {
                fprintf(stderr, "syntax error: multiple output files\n");
                return 1;
            }
            if (i + 1 == argc) {
                fprintf(stderr, "syntax error: no output file\n");
                return 1;
            }
            if (!strcmp(argv[i + 1], "<") || !strcmp(argv[i + 1], ">") ||
                !strcmp(argv[i + 1], ">>")) {
                fprintf(stderr,
                        "syntax error: output file is redirection symbol\n");
                return 1;
            }
            output_file = argv[i + 1];
            found_out = 1;
            argc2 += -2;
        }
    }

    int append = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], ">>")) {
            if (found_out) {
                fprintf(stderr, "syntax error: multiple output files\n");
                return 1;
            }
            if (i + 1 == argc) {
                fprintf(stderr, "syntax error: no output file\n");
                return 1;
            }
            if (!strcmp(argv[i + 1], "<") || !strcmp(argv[i + 1], ">") ||
                !strcmp(argv[i + 1], ">>")) {
                fprintf(stderr,
                        "syntax error: output file is redirection symbol\n");
                return 1;
            }

            output_file = argv[i + 1];
            found_out = 1;
            append = 1;
            argc2 += -2;
        }
    }

    char *argv2[argc2];
    int j = 0;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "<") || !strcmp(argv[i], ">") ||
            !strcmp(argv[i], ">>")) {
            i += 1;
        } else {
            argv2[j] = argv[i];
            j++;
        }
    }
    int foreground = 1;
    if (!strcmp(argv2[argc2 - 2], "&")) {
        foreground = 0;
        // to cut the & out of the argv array
        argc2 += -1;
    }
    // in case we don't have any arguments
    if (argc2 < 2) {
        return 1;
    }
    argv2[argc2 - 1] = NULL;
    // note: by passing argc2 - 1 to builtin_handler, I effectively subtract 1
    //      from the length of argv2 in this call, truncating the NULL element
    if (!builtin_handler(argc2 - 1, argv2)) {
        executable_handler(argv2, input_file, output_file, append, foreground);
    }
    return 0;
}

/*
This function reads shel input from the user and executes it accordingly
*/
void read_input() {
    char buffer[BUFFER_SIZE];
#ifdef PROMPT
    printf(strcat(getcwd(buffer, 255), " $ "));
    fflush(stdout);
#endif
    /* initialized the buffer to all nulls */
    memset(buffer, 0, sizeof(char) * BUFFER_SIZE);
    ssize_t size = read(STDIN_FILENO, buffer, BUFFER_SIZE);
    if (!size) {
        cleanup_job_list(job_list);
        exit(0);
    } else if (size < 0) {
        fprintf(stderr, "failed to read from standard input\n");
    } else {
        int num_tokens = count_tokens(buffer, DELIM, (size_t)size);
        char *tokens[num_tokens];
        if (num_tokens) {
            parse(buffer, tokens, num_tokens);
            redirect(num_tokens, tokens);
        }
    }
}

/*
This function iterates through the job list, checking each jobs' status and
killing zombie processes, just like Abe Lincoln used to.
*/
void reap() {
    int pid = get_next_pid(job_list);
    while (pid != -1) {
        int wstatus;
        int ret = waitpid(pid, &wstatus, WNOHANG | WUNTRACED | WCONTINUED);
        int jid = get_job_jid(job_list, pid);
        if (jid == -1) {
            fprintf(stderr, "could not get job ID");
        }
        if (ret == -1) {
            perror("waitpid");
        } else if (ret) {
            if (WIFEXITED(wstatus)) {
                if (remove_job_pid(job_list, pid) == -1) {
                    fprintf(stderr, "could not remove job");
                }
                int estatus = WEXITSTATUS(wstatus);
                printf("[%d] (%d) terminated with exit status %d\n", jid, pid,
                       estatus);
            } else if (WIFSIGNALED(wstatus)) {
                if (remove_job_pid(job_list, pid) == -1) {
                    fprintf(stderr, "could not remove job");
                }
                int esig = WTERMSIG(wstatus);
                printf("[%d] (%d) terminated by signal %d\n", jid, pid, esig);
            } else if (WIFSTOPPED(wstatus)) {
                if (update_job_pid(job_list, pid, STOPPED) == -1) {
                    fprintf(stderr, "could not update job state");
                }
                int ssig = WSTOPSIG(wstatus);
                printf("[%d] (%d) suspended by signal %d\n", jid, pid, ssig);
            } else if (WIFCONTINUED(wstatus)) {
                if (update_job_pid(job_list, pid, RUNNING) == -1) {
                    fprintf(stderr, "could not update job state");
                }
                printf("[%d] (%d) resumed\n", jid, pid);
            }
        }
        pid = get_next_pid(job_list);
    }
}
/*
This function contains a repl for the shell. Returns 0 if successfully exited
*/
int main() {
    job_list = init_job_list();
    signal(SIGINT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    while (1) {
        reap();
        read_input();
    }
    cleanup_job_list(job_list);
    return 0;
}
