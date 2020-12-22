#define _INTERNAL_JOBS_C_DELCARATIONS_

#include "jobs.h"
#include <termios.h>
#include <sys/ioctl.h>
#include <errno.h>

static void handle_sighup(int signal) {
    // TODO: send sighup to all jobs
    exit(1);
}

/* Mark a stopped job chain as being running again. */
void mark_job_as_running(cmdch* chain) {
    for (cmd* cm = chain->first; cm; cm = cm->next)
        cm->status = S_RUNNING;
    chain->notified = 0;
}

/* Continue the job J. */
void continue_job(cmdch* chain, bool foreground) {
    mark_job_as_running(chain);
    if (foreground)
        put_job_in_foreground(chain, true);
    else
        put_job_in_background(chain, true);
}

// Store the status of the process pid that was returned by waitpid.
// Return 0 if all went well, nonzero otherwise.
static int mark_process_status(pid_t pid, int status) {
    if (pid > 0) { // Update the record for the process.
        for (cmdch* chain = first_job; chain; chain = chain->next_job)
            for (cmd* command = chain->first; command; command = command->next)
                if (command->pid == pid) {
                    command->return_status = status;
                    if (WIFSTOPPED (status))
                        command->status |= S_STOPPED;
                    else {
                        command->status |= S_FINISHED;
                        if (WIFSIGNALED (status))
                            fprintf(stderr, "%d: Terminated by signal %d.\n",
                                        (int) pid, WTERMSIG (command->return_status));
                    }
                    return 0;
                }
        fprintf(stderr, "No child process %d.\n", pid);
        return -1;
    }
    else if (pid == 0 || errno == ECHILD)
        /* No processes ready to report. */
        return -1;
    else {
        /* Other weird errors. */
        perror("waitpid");
        return -1;
    }
}

// Check for processes that have status information available, without blocking.
// (the trick is that it calls mark_process_status while there are zombie children)
void update_status() {
    int status;
    pid_t pid;
    do
        pid = waitpid(-1, &status, WUNTRACED|WNOHANG);
    while (!mark_process_status(pid, status));
}

void list_jobs() {
    update_status();
    for (cmdch* ptr = first_job; ptr; ptr = ptr->next_job) {
        printf("[%d]\t%s\t\t%s\n", ptr->internal_job_id, 
        job_is_completed(ptr) ? "Completed" : job_is_stopped(ptr) ?
        "Stopped" : "Running", ptr->line);
    }
}

// Check for processes that have status information available,
// blocking until all processes in the given job have reported.
void wait_for_job(cmdch* chain) {
    int status;
    pid_t pid;
    do
        pid = waitpid(WAIT_ANY, &status, WUNTRACED);
    while (!mark_process_status(pid, status)
            && !job_is_stopped(chain)
            && !job_is_completed(chain));
}

//Format information about job status for the user to look at.
static void format_job_info(cmdch *chain, const char* status) {
    fprintf (stderr, "%ld (%s): %s\n", (long)chain->pgid, status, chain->line);
}
//*/


/* Notify the user about stopped or terminated jobs.
Delete terminated jobs from the active job list. */
void do_job_notification() {
    cmdch* tmp;
    // Update status information for child processes
    update_status();
    for (cmdch **ptr = &first_job; *ptr; ptr = &(*ptr)->next_job) {
        /* If all processes have completed, tell the user the job has
        completed and delete it from the list of active jobs. */
        if (job_is_completed(*ptr)) {
            format_job_info(*ptr, "completed");
            tmp = *ptr;
            *ptr = (*ptr)->next_job;
            freecmdch(tmp);
        }
        /* Notify the user about stopped jobs,
        marking them so that we won’t do this more than once. */
        else if (job_is_stopped(*ptr) && !(*ptr)->notified) {
            format_job_info(*ptr, "stopped");
            (*ptr)->notified = 1;
        }
    }
}


void put_job_in_background(cmdch* chain, bool cont) {
    pushchain(chain);
    /* Send the job a continue signal, if necessary. */
    if (cont)
        if (kill (- chain->pgid, SIGCONT) < 0)
            perror ("kill (SIGCONT)");
}

void put_job_in_foreground(cmdch* chain, bool cont) {
    // Put the job into the foreground.
    tcsetpgrp(lsh_terminal, chain->pgid);
    // Send the job a continue signal, if necessary.
    if (cont) {
        tcsetattr(lsh_terminal, TCSADRAIN, &chain->tmodes);
        if (kill (- chain->pgid, SIGCONT) < 0)
            perror("kill (SIGCONT)");
    }
    // Wait for it to report
    wait_for_job(chain);
    // Put the shell back in the foreground.
    tcsetpgrp(lsh_terminal, lsh_pgid);
    // Restore the shell’s terminal modes, while saving current
    // ones as assigned to job that created them
    tcgetattr(lsh_terminal, &chain->tmodes);
    tcsetattr(lsh_terminal, TCSADRAIN, &lsh_modes);
    freecmdch(chain); // we won't use it anymore TODO: handle stopped jobs here
}



void launch_process(cmd* command, msvec* words, int pipein, int pipefd[2], 
                            pid_t groupId, bool pipefromprev, bool foreground) {

    if (lsh_is_interactive) {
        if (setpgid(0, groupId) == -1) { // moving next processes to the group. In case of 0 setting group leader
            perror("setpgid");
            exit(47);
        }
        if (foreground) {
            tcsetpgrp(lsh_terminal, getpgid(getpid()));
        }

        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTTOU, SIG_DFL); // I don't know Mark, those signals soon will be overwritten by exec
    }
    
    
    if (pipefromprev) {
        if ((dup2(pipein, 0) != 0)) {
            perror("lsh: [pipein] dup2");
            exit(47);
        }
        close(pipein);
    }
    if (command->next) {
        if (dup2(pipefd[1], 1) != 1) {
            perror("lsh: [pipeout] dup2");
            exit(47);
        }
        close(pipefd[0]);
        close(pipefd[1]);
    }

    if (command->fd0) {
        freopen(command->fd0, "r", stdin);
    }
    if (command->fd1) {
        freopen(command->fd1, "w", stdout);
    }
    if (command->fd2) {
        freopen(command->fd2, "w", stderr);
    }

    char** args = getArgs(words, command->start, command->stop);
    execvp(args[0], args);
    perror("execvp");
    exit(47);
}

void launch_job(cmdch* chain, bool background) {
    int pipefd[2];
    int pipein;
    bool pipefromprev = false;
    pid_t groupId = 0;
    pid_t lastpid;

    for (cmd* command = chain->first; command; command = command->next) {
        if (command->next) {
            if (pipe(pipefd) < 0) {
                perror("lsh: pipe");
                return;
            }
        }
        if ((lastpid = fork()) == 0) { // Ayy, I assume fork() always works.
            launch_process(command, chain->words, pipein, pipefd, groupId, pipefromprev, !background);
        }
        else { // parent
            if (command == chain->first) {
                groupId = lastpid;          // save group leader id
            }
            setpgid(lastpid, groupId);      // set pgid of current child
            command->pid = lastpid;
            if (lsh_is_interactive) {
                if (background) {
                    int fd = open("/dev/tty", O_RDWR);
                    if (ioctl(fd, TIOCNOTTY, NULL)) // u sure dude?
                        perror("ioctl");
                }
                else {
                    tcsetpgrp(lsh_terminal, groupId);
                }
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
        }
    }
    chain->line = getLine(chain->words);
    freemsvec(chain->words);
    chain->words = NULL;
    if (!lsh_is_interactive)
        wait_for_job(chain);
    else if (background)
        put_job_in_background(chain, false);
    else
        put_job_in_foreground(chain, false);
}
