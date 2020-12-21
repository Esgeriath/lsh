#include "jobs.h"
#include <termios.h>
#include <sys/ioctl.h>

void launch_process(cmd* command, msvec* words, int pipein, int pipefd[2], 
                            pid_t groupId, bool pipefromprev, bool foreground) {

    if (setpgid(0, groupId) == -1) { // moving next processes to the group. In case of 0 setting group leader
        perror("setpgid");
        exit(47);
    }
    if (foreground) {
        tcsetpgrp (STDIN_FILENO, groupId);
    }

    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    
    if (pipefromprev) {
        if ((dup2(pipein, 0) != 0)) {
            perror("lsh: trouble connectting pipein; exiting subprocess... ");
            exit(47);
        }
        close(pipein);
    }
    if (command->next) {
        if (dup2(pipefd[1], 1) != 1) {
            perror("lsh: trouble connectting pipeout; exiting subprocess... ");
            exit(47);
        }
        close(pipefd[0]);
        close(pipefd[1]);
    }

    if (command->fd0 != NULL) {
        freopen(command->fd0, "r", stdin);
    }
    if (command->fd1 != NULL) {
        freopen(command->fd1, "w", stdout);
    }
    if (command->fd2 != NULL) {
        freopen(command->fd2, "w", stderr);
    }

    char** args = getArgs(words, command->start, command->stop);
    execvp(args[0], args);
    exit(47);
}

void launch_job(cmdch* chain, bool background) {
    int pipefd[2];
    int pipein;
    bool pipefromprev = false;
    pid_t groupId = 0;

    for (cmd* command = chain->first;command; command = command->next) {
        if (command->next) {
            if (pipe(pipefd) < 0) {
                perror("lsh: Error creating a pipe. Comand not executed.\n");
                return;
            }
        }
        if ((lastpid = fork()) == 0) {
            launch_process(command, chain->words, pipein, pipefd, groupId, pipefromprev, !background);
        }
        else {
            if (command == chain->first) {
                groupId = lastpid;          // save group leader id
            }
            setpgid(lastpid, groupId);      // set pgid of current child
            if (background) {
                int fd = open("/dev/tty", O_RDWR);
                if (ioctl(fd, TIOCNOTTY, NULL))
                    perror("ioctl");

            }
            else {
                tcsetpgrp(STDIN_FILENO, groupId);
                tcsetpgrp(STDERR_FILENO, groupId);
            }

            if (pipefromprev) {
                close(pipein);
            }
            if (command->next) {
                pipein = pipefd[0];
                close(pipefd[1]);
                pipefromprev = true;
            }
            else {
                pipefromprev = false;
            }
            command->pid = lastpid;
            if (!command->next) { // last one
                pushchain(&jobs, chain);
                if (background) {
                    lastJob = NULL;
                    chain->line = getLine(chain->words);
                    freemsvec(chain->words);
                    chain->words = NULL;
                }
                else {
                    lastJob->status = S_RUNNING;
                    waitpid(lastpid, &status, WUNTRACED);   // after process exits
                    tcsetpgrp(STDIN_FILENO, getpid());      // make lsh foreground process again
                    tcsetpgrp(STDERR_FILENO, getpid());
                    // if stopped then put into jobs TODO:
                    /*if (status & WUNTRACED) {

                    }*/
                    if (status == 12032) {
                        perror("lsh: command execution failure ");
                    }
                }
            }
        }
    }
}