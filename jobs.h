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

void launch_process(cmd* command, msvec* words, int pipein, int pipefd[2], 
                            pid_t groupId, bool pipefromprev, bool foreground);
void launch_job(cmdch* chain, bool background);

void put_job_in_foreground(cmdch* chain, bool cont);
void put_job_in_background(cmdch* chain, bool cont);

void do_job_notification();

#endif