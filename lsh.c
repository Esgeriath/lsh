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
//test
int main(int argc, char** argv) {
    signal(SIGINT, ctrl_c);
    signal(SIGTSTP, ctrl_z);
    
    children.size = 16;
    children.count = 0;
    children.pids = malloc(children.size * sizeof(pid_t));

    pthread_mutex_init(&lock, NULL);

    pthread_t zombieHunter;
    pthread_create(&zombieHunter, NULL, hunt, &children);

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

    msvec words;            // comand line splitted into a vector of words
    words.count = 0;
    words.size = 24;
    words.arr = malloc(words.size * sizeof(mlstr));
    for (int i = 0; i < words.size; i++) {
        words.arr[i].bytes = 16;
        words.arr[i].ptr = malloc(words.arr[i].bytes * sizeof(char));
    }

    cmd* cmds = NULL;       // pointer to array of cmd objects

    char *line = NULL;      // line from stdin
    size_t len = 0;

    int status; // exit status of last command

    // initial prompt
    printf("\x1B[36;1m%s@%s\x1B[0m:\x1B[37;1m%s\x1B[0m$ ", name, host, path);

    // keep getting lines from stdin and do stuff with them
    while(getline(&line, &len, stdin) != -1) {
        breakline(&words, &line);
        bool background = strcmp(words.arr[words.count - 1].ptr, "&") == 0;
        if (background) {
            words.count--;
        }

        int cmdcount = breakcommands(&cmds, &words);
        for (int i = 0; i < cmdcount; i++ ) {
            printf("cmd start: %s\n", words.arr[cmds[i].start].ptr);
            char ** tmp = getArgs(&words, cmds[i].start, cmds[i].stop);
            while(*tmp != NULL) {
                printf(" %s\n", *tmp);
                tmp++;
            }
        }
        if (cmdcount < 0) {
            perror("lsh: syntax error (bad fd managment)\n");
            goto prompt;
        }
        
        if (words.count == 0) {
            goto prompt;
        }
        if (strcmp(words.arr[0].ptr, "exit") == 0) {
            goto end;
        }
        if (strcmp(words.arr[0].ptr, "cd") == 0) {
            if (words.count == 1) { // no path
                strcpy(path, homedir);
                chdir(path);
            }
            else {
                // cd to parent dir
                if (strcmp(words.arr[1].ptr, "..") == 0) {
                    if (strcmp(path, "/") != 0) {
                        for (int i = strlen(path);; i--) {
                            if (path[i] == '/') {
                                path[i] = '\0';
                                break;
                            }
                        }
                        chdir(path);
                    }
                }
                // absolute path
                else if (words.arr[1].ptr[0] == '/') {
                    if (chdir(words.arr[1].ptr) == 0) {
                        strcpy(path, words.arr[1].ptr);
                    }
                    else {
                        perror("lsh: cd: no such directory\n");
                    }
                }
                // cd relative to current dir
                else {
                    int bytes = strlen(path) + strlen(words.arr[1].ptr) + 1;
                    char* tmpstr = malloc(bytes * sizeof(char));
                    strcpy(tmpstr, path);
                    strcat(tmpstr, "/");
                    strcat(tmpstr, words.arr[1].ptr);
                    if (chdir(tmpstr) == 0) {
                        strcpy(path, tmpstr);
                    }
                    else {
                        perror("lsh: cd: no such directory\n");
                    }
                    free(tmpstr);
                }
            }
            goto prompt;
        }
        
        printf("cmdcount: %d \n", cmdcount);
        cmdcount--;
        int pipefd[2];
        bool pipefromprev = false;
        int pipeout;
        for (int i = 0; i < cmdcount; i++) {
            /*if (cmds[i].pipestonext) {
                if (pipe(pipefd) < 0) {
                    perror("lsh: Error creating a pipe. Comand not executed.\n");
                    goto prompt;
                }
            }*/
            if ((lastpid = fork()) == 0) {
                /*freopen(cmds[i].fd0, "r", stdin);
                freopen(cmds[i].fd1, "w", stdout);
                freopen(cmds[i].fd2, "w", stderr);*/
                /*if (pipefromprev && dup2(pipeout, 0)) {
                    close(pipeout);
                }
                else if (pipefromprev) {
                    perror("lsh: trouble connetting pipe; exiting subprocess...\n");
                    exit(47);
                }
                if (cmds[i].pipestonext) {
                    if (dup2(pipefd[1], 1) != 1) {
                        perror("lsh: trouble connetting pipe; exiting subprocess...\n");
                        exit(47);
                    }
                    close(pipefd[0]);
                    close(pipefd[1]);
                }*/

                char** args = getArgs(&words, cmds[i].start, cmds[i].stop);
                printf("Hello from child!");
                execvp(words.arr[0].ptr, args);
                exit(47);
            }
            else {
                /*if (pipefromprev) {
                    close(pipeout);
                }
                if (cmds[i].pipestonext) {
                    pipeout = pipefd[0];
                    close(pipefd[1]);
                }*/
                if (i != cmdcount - 1) { // not last one
                    pthread_mutex_lock(&lock);
                    pushpid(&children, lastpid);
                    pthread_mutex_unlock(&lock);
                }
                else { // last one
                    cmdcount++;
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
        }
        
prompt:
        free(cmds);
        printf("\x1B[36;1m%s@%s\x1B[0m:\x1B[37;1m%s\x1B[0m$ ", name, host, path);
    }

    printf("\n");
end:
    // since it is the end of process, it is not nescesarry to free ram,
    // so I don't.
    //free(line);
    //free(path);
    //  free all strings in msvec words ...
    return 0;
}