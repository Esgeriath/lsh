#include "util.h"
#include "jobs.h"

//static pthread_mutex_t lock;

// CTRL+C handler
static void ctrl_c(int signal) {
    //kill(lastpid, signal);
    if (lastJob != NULL && lastJob->first != NULL) {
        killpg(lastJob->first->pid, signal);
    }
}

// CTRL+Z handler
static void ctrl_z(int signal) {
    if (lastJob == NULL) {
        return;
    }
    killpg(lastJob->first->pid, signal);
    pushchain(&jobs, lastJob);
}

static void handle_sighup(int signal) {
    // TODO: send sighup to all jobs
    exit(1); // maybe 0 ?
}

static void chld_handler(int signal) {
    
    for (int i = 0; i < jobs.count; i++) {
        if (jobs.arr[i] != NULL && jobs.arr[i]->status == S_RUNNING) { // something happend to foreground process
            waitpid(- jobs.arr[i]->first->pid, &status, WNOHANG);
            if (WIFSTOPPED(status)) {
                jobs.arr[i]->status = S_STOPPED;
            }
/*
            for (cmd* curr = jobs.arr[i]->first; curr; curr = curr->next) {
                if (curr->status & S_RUNNING)
                waitpid(curr->pid, &status, WNOHANG);
                
                if (WIFEXITED(status)) {
                    //todo: removing block from this job
                }
            }*/
        }
    }
}
/*
static void* hunt() {
    while (1) {
        sleep(1);
        //pthread_mutex_lock(&lock);
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
        //pthread_mutex_unlock(&lock);
    }
    return NULL;
}
*/
//#include <termios.h>

int main(int argc, char** argv) {
    signal(SIGINT, ctrl_c);
    signal(SIGTSTP, ctrl_z);
    
    jobs.size = 16;
    jobs.count = 0;
    jobs.arr = malloc(jobs.size * sizeof(cmdch*));

    //pthread_mutex_init(&lock, NULL);

    /*pthread_t zombieHunter;
    pthread_create(&zombieHunter, NULL, hunt, NULL);
    pthread_detach(zombieHunter);*/

    struct passwd *pw = getpwuid(getuid());
    char homedir[PATH_MAX];
    strcpy(homedir, pw->pw_dir);

    char path[PATH_MAX];
    getcwd(path, PATH_MAX);

    msvec* words = NULL;        // comand line splitted into a vector of words

    char *line = NULL;          // line from stdin
    size_t len = 0;

    bool built_in = true;


    //*
    if (setpgid(getpid(), getpid()) < 0) {
        perror ("Couldn't put the shell in its own process group");
        exit (1);
    }
    tcsetpgrp (STDIN_FILENO, getpid());
    //*/

    /*
    if (fork() != 0)
        exit(EXIT_SUCCESS);
    setsid();
    //*/

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);

    signal(SIGCHLD, chld_handler);
    //*/
    signal(SIGHUP, handle_sighup);

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
                    printf("[%d]\t%s\n", i, jobs.arr[i]->line);
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
    //pthread_cancel(zombieHunter);
    // last lines can be commented, as we are exiting the process
    for (int i = 0; i < jobs.count; i++) {
        freecmdch(jobs.arr[i]);
    }
    free(jobs.arr);
    free(line);
    return 0;
}