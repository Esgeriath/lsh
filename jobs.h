#ifndef _JOBS_H_
#define _JOBS_H_

#include "util.h"


pid_t lastpid;
cmdch* lastJob;
ccvec jobs;
int status;             // last return status

void init_job_control();
void background(cmdch* job, int cont);

void launch_process(cmd* command, msvec* words, int pipein, int pipefd[2], 
                            pid_t groupId, bool pipefromprev, bool foreground);
void launch_job(cmdch* chain, bool background);

#endif