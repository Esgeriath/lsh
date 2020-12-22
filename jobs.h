#ifndef _JOBS_H_
#define _JOBS_H_

#include "util.h"

#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

int lsh_terminal;
int lsh_is_interactive;
pid_t lsh_pgid;
struct termios lsh_modes;

#ifdef _INTERNAL_JOBS_C_DELCARATIONS_

void launch_process(cmd* command, msvec* words, int pipein, int pipefd[2], 
                            pid_t groupId, bool pipefromprev, bool foreground);
void put_job_in_foreground(cmdch* chain, bool cont);
void put_job_in_background(cmdch* chain, bool cont);

#endif

// Public functions
void launch_job(cmdch* chain, bool background);
void continue_job(cmdch* chain, bool foreground);
void do_job_notification();
void list_jobs();

#endif