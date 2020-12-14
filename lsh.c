#include "util.h"

pthread_mutex_t lock;
pid_t lastpid;
ccvec jobs;
cmdch* lastJob;

// CTRL+C handler
void ctrl_c(int signal) {
    kill(lastpid, signal);
}

// CTRL+Z handler
void ctrl_z(int signal) {
    kill(lastpid, signal);          // stop child
    pthread_mutex_lock(&lock);      // push it to background
    pushchain(&jobs, lastJob);
    pthread_mutex_unlock(&lock);
}

void* hunt(void* vec) {
    while (1) {
        sleep(1);
        pthread_mutex_lock(&lock);
        for (int i = 0; i < ((ccvec*) vec)->count; i++) {
            int status;
            bool alldone = true;
            for (int j = 0; j < ((ccvec*) vec)->arr[i]->count; j++) {
                if (((ccvec*) vec)->arr[i]->cmds[j].pid != 0) {
                    pid_t pid = waitpid(((ccvec*) vec)->arr[i]->cmds[j].pid, &status, WNOHANG);
                    if (pid != 0) {
                        ((ccvec*) vec)->arr[i]->cmds[j].pid = 0;
                    }
                    else {
                        alldone = false;
                    }
                }
            }
            if (alldone) {
                freecmdch(((ccvec*) vec)->arr[i]);
                ((ccvec*) vec)->arr[i] = NULL;
            }
        }
        for (int i = ((ccvec*) vec)->count - 1; i >= 0; i--) {
            if (((ccvec*) vec)->arr[i] != NULL) {
                ((ccvec*) vec)->count = i + 1;
                break;
            }
            if (i == 0) {
                ((ccvec*) vec)->count = 0;
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
    pthread_create(&zombieHunter, NULL, hunt, &jobs);
    pthread_detach(zombieHunter);

    char name[32];          // for prompt
    char host[32];
    getlogin_r(name, 32);
    gethostname(host, 32);
    name[31] = '\0';
    host[31] = '\0';

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
    printf("\x1B[36;1m%s@%s\x1B[0m:\x1B[37;1m%s\x1B[0m$ ", name, host, path);

    // keep getting lines from stdin and do stuff with them
    while(getline(&line, &len, stdin) != -1) {
        words = breakline(&line);
        bool background = strcmp(words->arr[words->count - 1].ptr, "&") == 0;
        if (background) {
            words->count--; // "rmoving" last word, that is "&"
        }
        not_built_in = false;
        if (words->count == 0) {
            goto prompt;
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

        not_built_in = true;
        
        chain = breakcommands(words);
        if (chain == NULL) {
            perror("lsh: syntax error (bad fd managment)\n");
            goto prompt;
        }
        int pipefd[2];
        int pipein;
        bool pipefromprev = false;
        for (int i = 0; i < chain->count; i++) {
            if (chain->cmds[i].pipestonext) {
                if (pipe(pipefd) < 0) {
                    perror("lsh: Error creating a pipe. Comand not executed.\n");
                    goto prompt;
                }
            }
            if ((lastpid = fork()) == 0) {
                // char * errmsg = NULL;
                
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
                if (i != chain->count - 1) { // not last one
                    //pthread_mutex_lock(&lock);
                    chain->cmds[i].pid = lastpid;
                    //pthread_mutex_unlock(&lock);
                }
                else { // last one
                    if (background) {
                        pthread_mutex_lock(&lock);
                        // pushpid(&children, lastpid);
                        chain->cmds[chain->count - 1].pid = lastpid;
                        pushchain(&jobs, chain);
                        pthread_mutex_unlock(&lock);
                    }
                    else {
                        pthread_mutex_lock(&lock);
                        // pushpid(&children, lastpid);
                        chain->cmds[chain->count - 1].pid = lastpid;
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
        printf("\x1B[36;1m%s@%s\x1B[0m:\x1B[37;1m%s\x1B[0m$ ", name, host, path);
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