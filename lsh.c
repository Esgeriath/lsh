#include "util.h"
#include "jobs.h"

void init() {
    /* See if we are running interactively. */
    lsh_terminal = STDIN_FILENO;
    lsh_is_interactive = isatty(lsh_terminal);
    if (lsh_is_interactive) {
        /* Loop until we are in the foreground. */
        while (tcgetpgrp(lsh_terminal) != (lsh_pgid = getpgrp()))
            kill(- lsh_pgid, SIGTTIN);
        /* Ignore interactive and job-control signals. */
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN); // I don't know Mark, seems fake to me
        signal(SIGTTOU, SIG_IGN);
        signal(SIGCHLD, SIG_IGN); // btw, we will need it later, so wtf
        // Ayy, we won't need SIGCHLD, because we're doing the easy way.
        /* Put ourselves in our own process group. */
        lsh_pgid = getpid();
        if (setpgid(0, 0) < 0) {
            perror ("Couldn't put lsh in its own process group");
            exit(1);
        }
        /* Grab control of the terminal. */
        tcsetpgrp(lsh_terminal, lsh_pgid);
        /* Save default terminal attributes for shell. */
        tcgetattr(lsh_terminal, &lsh_modes);
        first_job = NULL;
    }
}

int main(int argc, char** argv) {
    init();                     // the magic happens here

    struct passwd *pw = getpwuid(getuid());
    char homedir[PATH_MAX];
    strcpy(homedir, pw->pw_dir);// saving homedir path

    char path[PATH_MAX];
    getcwd(path, PATH_MAX);     // get current working directory

    msvec* words = NULL;        // comand line splitted into a vector of words

    char *line = NULL;          // line from stdin
    size_t len = 0;             // used in getline, dont mind it

    bool built_in = true;       // if entered command is built in lsh

    // initial prompt. CAUTION: I assume 256-color support
    if (lsh_is_interactive)
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
            if (words->count == 1) {        // no path - go to home directory
                strcpy(path, homedir);
                chdir(path);
            }
            else if (chdir(words->arr[1].ptr) == 0) { // chdir success
                getcwd(path, PATH_MAX);     // save new current working directory for prompt
            }
            else {  // chdir fail
                perror("lsh: cd");          // here chdir will tell us there is no such dir :3
            }
            goto prompt;
        }
        if (strcmp(words->arr[0].ptr, "jobs") == 0) {
            // TODO: jobs command
            goto prompt;
        }

        built_in = false;
        // exec commands
        cmdch* chain = breakcommands(words);
        if (chain == NULL) {
            fprintf(stderr, "lsh: syntax error (bad fd management)\n");
            goto prompt;
        }
        launch_job(chain, background);      // all funny stuff happens here

prompt:
        if (built_in) {
            freemsvec(words);
        }
        if (lsh_is_interactive) {
            do_job_notification();
            printf("\x1B[01;38;5;51m%s\x1B[0m >>> ", path);
        }
    } // end of while(getline)

    printf("\n");
end:
    // last lines can be commented, as we are exiting the process
    freeall(first_job);
    free(line);
    return 0;
}