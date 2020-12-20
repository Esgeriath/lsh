#include "util.h"

static pthread_mutex_t lock;
static pid_t lastpid;
static ccvec jobs;
static cmdch* lastJob;
static int status;              // last return status

// CTRL+C handler
static void ctrl_c(int signal) {
    //kill(lastpid, signal);
    if (lastJob != NULL) {
        killpg(lastJob->cmds[0].pid, signal);
    }
}

// CTRL+Z handler
static void ctrl_z(int signal) {
    if (lastJob == NULL) {
        return;
    }
    killpg(lastJob->cmds[0].pid, signal);
    pthread_mutex_lock(&lock);      // push it to background
    pushchain(&jobs, lastJob);
    pthread_mutex_unlock(&lock);
}

static void* hunt() {
    while (1) {
        sleep(1);
        pthread_mutex_lock(&lock);
        for (int i = 0; i < jobs.count; i++) {
            bool alldone = true;
            for (int j = 0; j < jobs.arr[i]->count; j++) {
                if (jobs.arr[i]->cmds[j].pid != 0) {
                    pid_t pid = waitpid(jobs.arr[i]->cmds[j].pid, &status, WNOHANG);
                    if (pid != 0) {
                        jobs.arr[i]->cmds[j].pid = 0;
                    }
                    else {
                        alldone = false;
                    }
                }
            }
            if (alldone) {
                freecmdch(jobs.arr[i]);
                jobs.arr[i] = NULL;
            }
        }
        for (int i = jobs.count - 1; i >= 0; i--) {
            if (jobs.arr[i] != NULL) {
                jobs.count = i + 1;
                break;
            }
            if (i == 0) {
                jobs.count = 0;
            }
        }
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

static void launch_process(cmdch* chain, int i, int pipein, int pipefd[2], 
                            pid_t groupId, bool pipefromprev, bool foreground) {

    setpgid(0, groupId); // moving next processes to the group. In case of 0 setting group leader
    if (foreground) {
        tcsetpgrp (STDIN_FILENO, groupId);
    }

    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGCHLD, SIG_DFL);
    
    if (pipefromprev) {
        if ((dup2(pipein, 0) != 0)) {
            perror("lsh: trouble connectting pipein; exiting subprocess... ");
            exit(47);
        }
        close(pipein);
    }
    if (chain->cmds[i].pipestonext) {
        if (dup2(pipefd[1], 1) != 1) {
            perror("lsh: trouble connectting pipeout; exiting subprocess... ");
            exit(47);
        }
        close(pipefd[0]);
        close(pipefd[1]);
    }

    if (chain->cmds[i].fd0 != NULL) {
        freopen(chain->cmds[i].fd0, "r", stdin);
    }
    if (chain->cmds[i].fd1 != NULL) {
        freopen(chain->cmds[i].fd1, "w", stdout);
    }
    if (chain->cmds[i].fd2 != NULL) {
        freopen(chain->cmds[i].fd2, "w", stderr);
    }

    char** args = getArgs(chain->words, chain->cmds[i].start, chain->cmds[i].stop);
    execvp(args[0], args);
    exit(47);
}

static void launch_job(cmdch* chain, bool background) {
    lastJob = chain;
    int pipefd[2];
    int pipein;
    bool pipefromprev = false;
    pid_t groupId = 0;

    for (int i = 0; i < chain->count; i++) {
        if (chain->cmds[i].pipestonext) {
            if (pipe(pipefd) < 0) {
                perror("lsh: Error creating a pipe. Comand not executed.\n");
                return;
            }
        }
        if ((lastpid = fork()) == 0) {
            launch_process(chain,i, pipein, pipefd, groupId, pipefromprev, !background);
        }
        else {
            if (i == 0) {
                groupId = lastpid;          // save group leader id
            }
            setpgid(lastpid, groupId);      // set pgid of current child
            if (!background) {
                tcsetpgrp(STDIN_FILENO, groupId);
            }

            if (pipefromprev) {
                close(pipein);
            }
            if (chain->cmds[i].pipestonext) {
                pipein = pipefd[0];
                close(pipefd[1]);
                pipefromprev = true;
            }
            else {
                pipefromprev = false;
            }
            chain->cmds[i].pid = lastpid;
            if (i == chain->count - 1) { // last one
                if (background) {
                    lastJob = NULL;
                    pthread_mutex_lock(&lock);
                    pushchain(&jobs, chain);
                    pthread_mutex_unlock(&lock);
                }
                else {
                    pthread_mutex_lock(&lock);
                    pushchain(&jobs, chain);
                    waitpid(lastpid, &status, WUNTRACED);
                    pthread_mutex_unlock(&lock);
                    if (status == 12032) {
                        perror("lsh: command execution failure ");
                    }
                }
            }
        }
    }
}

int main(int argc, char** argv) {
    signal(SIGINT, ctrl_c);
    signal(SIGTSTP, ctrl_z);
    
    jobs.size = 16;
    jobs.count = 0;
    jobs.arr = malloc(jobs.size * sizeof(cmdch*));

    pthread_mutex_init(&lock, NULL);

    pthread_t zombieHunter;
    pthread_create(&zombieHunter, NULL, hunt, NULL);
    pthread_detach(zombieHunter);

    struct passwd *pw = getpwuid(getuid());
    char homedir[PATH_MAX];
    strcpy(homedir, pw->pw_dir);

    char path[PATH_MAX];
    getcwd(path, PATH_MAX);

    msvec* words = NULL;        // comand line splitted into a vector of words

    char *line = NULL;          // line from stdin
    size_t len = 0;

    bool built_in = true;


    if (setpgid(getpid(), getpid()) < 0) {
        perror ("Couldn't put the shell in its own process group");
        exit (1);
    }
    tcsetpgrp (STDIN_FILENO, getpid());

    signal(SIGINT, SIG_IGN);
    //signal (SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    // initial prompt
    printf("\x1B[01;38;5;51m%s\x1B[0m >>> ", path);

    // keep getting lines from stdin and do stuff with them
    while(getline(&line, &len, stdin) != -1) {
        words = breakline(&line);
        
        // built-in commands:
        built_in = true;
        if (words->count == 0) {
            goto prompt;
        }
        bool background = strcmp(words->arr[words->count - 1].ptr, "&") == 0;
        if (background) {
            words->count--; // "removing" last word, that is "&"
        }
        if (strcmp(words->arr[0].ptr, "exit") == 0) {
            freemsvec(words);
            goto end;
        }
        if (strcmp(words->arr[0].ptr, "cd") == 0) {
            if (words->count == 1) { // no path
                strcpy(path, homedir);
                chdir(path);
            }
            else if (chdir(words->arr[1].ptr) == 0) {
                getcwd(path, PATH_MAX);
            }
            else {
                perror("lsh: cd"); // here chdir will tell us there is no such dir :3
            }
            goto prompt;
        }
        if (strcmp(words->arr[0].ptr, "jobs") == 0) {
            for (int i = 0; i < jobs.count; i++) {
                if (jobs.arr[i] != NULL) {
                    printf("[%d]\t%s\n", i, jobs.arr[i]->words->arr[jobs.arr[i]->cmds->start].ptr);
                }
            }
            goto prompt;
        }

        built_in = false;
        
        cmdch* chain = breakcommands(words);
        if (chain == NULL) {
            perror("lsh: syntax error (bad fd management)\n");
            goto prompt;
        }
        launch_job(chain, background);

prompt:
        if (built_in) {
            freemsvec(words);
        }
        printf("\x1B[01;38;5;51m%s\x1B[0m >>> ", path);
    } // end of while(getline)

    printf("\n");
end:
    pthread_cancel(zombieHunter);
    // last lines can be commented, as we are exiting the process
    for (int i = 0; i < jobs.count; i++) {
        freecmdch(jobs.arr[i]);
    }
    free(jobs.arr);
    free(line);
    return 0;
}