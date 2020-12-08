#include "util.h"

pthread_mutex_t lock;
pid_t lastpid;
bcvec children;

// CTRL+C handler
void ctrl_c(int signal) {
    kill(lastpid, signal);
}

// CTRL+Z handler
void ctrl_z(int signal) {
    kill(lastpid, signal);          // stop child
    pthread_mutex_lock(&lock);      // push it to background
    pushpid(&children, lastpid);
    pthread_mutex_unlock(&lock);
}

void* hunt(void* vec) {
    while (1) {
        sleep(1);
        pthread_mutex_lock(&lock);
        for (int i = 0; i < ((bcvec*) vec)->count; i++) {
            int status;
            if (((bcvec*) vec)->pids[i] != 0) {
                pid_t pid = waitpid(((bcvec*) vec)->pids[i], &status, WNOHANG);
                if (pid != 0) {
                    ((bcvec*) vec)->pids[i] = 0;
                }
            }
        }
        for (int i = ((bcvec*) vec)->count - 1; i >= 0; i--) {
            if (((bcvec*) vec)->pids[i] != 0) {
                ((bcvec*) vec)->count = i + 1;
                break;
            }
            if (i == 0) {
                ((bcvec*) vec)->count = 0;
            }
        }
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

int main(int argc, char** argv) {
    signal(SIGINT, ctrl_c);
    signal(SIGTSTP, ctrl_z);
    
    children.size = 16;
    children.count = 0;
    children.pids = malloc(children.size * sizeof(pid_t));

    pthread_mutex_init(&lock, NULL);

    pthread_t zombieHunter;
    pthread_create(&zombieHunter, NULL, hunt, &children);
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
        bool pipefromprev = false;
        int pipeout;
        for (int i = 0; i < chain->count; i++) {
            if (chain->cmds[i].pipestonext) {
                if (pipe(pipefd) < 0) {
                    perror("lsh: Error creating a pipe. Comand not executed.\n");
                    goto prompt;
                }
            }
            if ((lastpid = fork()) == 0) {
                // char * errmsg = NULL;
                if (pipefromprev && (dup2(pipeout, 0) != -1)) {
                    close(pipeout);
                }
                else if (pipefromprev) {
                    perror("lsh: trouble connetting pipe; exiting subprocess...\n");
                    exit(47);
                }
                if (chain->cmds[i].pipestonext) {
                    if (dup2(pipefd[1], 1) != 1) {
                        perror("lsh: trouble connetting pipe; exiting subprocess...\n");
                        exit(47);
                    }
                    close(pipefd[0]);
                    close(pipefd[1]);
                }
                if (chain->cmds[i].fd0 != NULL) {
                    /*int fd0;
                    if ((fd0 = open(chain->cmds[i].fd0, O_RDONLY)) == -1) {
                        perror("lsh: file not found; exiting subprocess...\n");
                        exit(47);
                    }
                    if (dup2(fd0, 0) != 1) {
                        perror("lsh: trouble redirecting fd0; exiting subprocess...\n");
                        exit(47);
                    }
                    close(fd0);*/
                    freopen(chain->cmds[i].fd0, "r", stdin);
                }
                if (chain->cmds[i].fd1 != NULL) {
                    int fd1;
                    if ((fd1 = open(chain->cmds[i].fd1, O_WRONLY | O_CREAT | O_TRUNC)) == -1) {
                        perror("lsh: error opening file; exiting subprocess...\n");
                        exit(47);
                    }
                    if (dup2(fd1, 1) != 1) {
                        perror("lsh: trouble redirecting fd1; exiting subprocess...\n");
                        exit(47);
                    }
                    close(fd1);
                }
                if (chain->cmds[i].fd2 != NULL) {
                    int fd2;
                    if ((fd2 = open(chain->cmds[i].fd2, O_WRONLY | O_CREAT | O_TRUNC)) == -1) {
                        perror("lsh: error opening file; exiting subprocess...\n");
                        exit(47);
                    }
                    if (dup2(fd2, 2) != 1) {
                        perror("lsh: trouble redirecting fd2; exiting subprocess...\n");
                        exit(47);
                    }
                    close(fd2);
                }

                char** args = getArgs(words, chain->cmds[i].start, chain->cmds[i].stop);
                execvp(words->arr[0].ptr, args);
                exit(47);
            }
            else {
                if (pipefromprev) {
                    close(pipeout);
                }
                if (chain->cmds[i].pipestonext) {
                    pipeout = pipefd[0];
                    close(pipefd[1]);
                    pipefromprev = true;
                }
                else {
                    pipefromprev = false;
                }
                if (i != chain->count - 1) { // not last one
                    pthread_mutex_lock(&lock);
                    pushpid(&children, lastpid);
                    pthread_mutex_unlock(&lock);
                }
                else { // last one
                    if (background) {
                        pthread_mutex_lock(&lock);
                        pushpid(&children, lastpid);
                        pthread_mutex_unlock(&lock);
                    }
                    else {
                        waitpid(lastpid, &status, WUNTRACED);
                        if (status == 12032) {
                            perror("lsh: command execution failure\n");
                        }
                    }
                }
            }
        //*/
        }
prompt:
        if (not_built_in) {
            freecmdch(chain);
        }
        else {
            freemsvec(words);
        }
        printf("\x1B[36;1m%s@%s\x1B[0m:\x1B[37;1m%s\x1B[0m$ ", name, host, path);
    }

    printf("\n");
end:
    pthread_cancel(zombieHunter);
    free(children.pids);
    free(line);
    return 0;
}