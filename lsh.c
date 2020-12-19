#include "util.h"

pthread_mutex_t lock;
pid_t lastpid;
ccvec jobs;
cmdch* lastJob;

// CTRL+C handler
void ctrl_c(int signal) {
    //kill(lastpid, signal);
    if (lastJob != NULL) {
        killpg(lastJob->cmds[0].pid, signal);
    }
}

// CTRL+Z handler
void ctrl_z(int signal) {
    if (lastJob == NULL) {
        return;
    }
    killpg(lastJob->cmds[0].pid, signal);
    pthread_mutex_lock(&lock);      // push it to background
    pushchain(&jobs, lastJob);
    pthread_mutex_unlock(&lock);
}

void* hunt() {
    while (1) {
        sleep(1);
        pthread_mutex_lock(&lock);
        for (int i = 0; i < jobs.count; i++) {
            int status;
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
    cmdch* chain = NULL;        // pointer to array of cmd objects

    char *line = NULL;          // line from stdin
    size_t len = 0;

    int status; // exit status of last command
    bool not_built_in = false;

    // initial prompt
    printf("\x1B[01;38;5;51m%s\x1B[0m >>> ", path);

    // keep getting lines from stdin and do stuff with them
    while(getline(&line, &len, stdin) != -1) {
        words = breakline(&line);
        
        // built-in commands:
        not_built_in = false;
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

        not_built_in = true;
        
        chain = breakcommands(words);
        if (chain == NULL) {
            perror("lsh: syntax error (bad fd management)\n");
            goto prompt;
        }
        lastJob = chain;
        int pipefd[2];
        int pipein;
        bool pipefromprev = false;
        pid_t groupId;
        for (int i = 0; i < chain->count; i++) {
            if (chain->cmds[i].pipestonext) {
                if (pipe(pipefd) < 0) {
                    perror("lsh: Error creating a pipe. Comand not executed.\n");
                    goto prompt;
                }
            }
            if ((lastpid = fork()) == 0) {
                if (i == 0) {
                    setpgid(0, 0); // making first process group leader
                }
                else {
                    setpgid(0, groupId); // moving next processes to the group
                }
                
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

                char** args = getArgs(words, chain->cmds[i].start, chain->cmds[i].stop);
                execvp(args[0], args);
                exit(47);
            }
            else {
                if (i == 0) {
                    groupId = lastpid;          // save group leader id
                }
                setpgid(lastpid, groupId);      // set pgid of current child

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
        //*/
        }
prompt:
        if (not_built_in) {
            //freecmdch(chain);
        }
        else {
            freemsvec(words);
        }
        printf("\x1B[01;38;5;51m%s\x1B[0m >>> ", path); // 35 - pink; 33 -yellow; 32 - green; 31 -red "\x1B[01;38m%s\x1B[0m >>> "
    }

    printf("\n");
end:
    pthread_cancel(zombieHunter);
    for (int i = 0; i < jobs.count; i++) {
        freecmdch(jobs.arr[i]);
    }
    free(jobs.arr);
    free(line);
    return 0;
}