#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>  //chck
#include <dirent.h>     //chck
#include <sys/wait.h>
#include <pwd.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <termios.h>

#define S_UNKNOWN   ((unsigned char)0b00000000)
#define S_RUNNING   ((unsigned char)0b00000001)
#define S_STOPPED   ((unsigned char)0b00000010)
#define S_FINISHED  ((unsigned char)0b00000100)

typedef struct mallocstring {
    unsigned short bytes;   // how many char's were allocated
    char* ptr;              // pointer to string
} mlstr;

typedef struct mallstringvector {
    unsigned short size;    // how many mlstr's were allocated
    unsigned short count;   // how many are used
    mlstr* arr;             // pointer to array of mlstr
} msvec;

typedef struct command {
    unsigned short start;   // index of the beginning of args
    unsigned short stop;    // index of first non-arg word
    char* fd0;              // stdin
    char* fd1;              // stdout
    char* fd2;              // stderr
    struct command* next;
    unsigned char status;   // stopped/finished... up to 8, but i'll set less ig
    int return_status;
    pid_t pid;
} cmd;

typedef struct commandchain {
    struct commandchain* next_job;
    cmd* first;
    msvec* words;
    char* line;
    pid_t pgid;
    char notified;           /* true if user told about stopped job */
    struct termios tmodes;  /* saved terminal modes */ // TODO: no asterisk here ?
    unsigned short internal_job_id;
} cmdch;

// The head of linked list of jobs (cmdch objects)
cmdch* first_job;

// GNU-inspired utils
// Find the job with the indicated pgid
cmdch* find_job (pid_t pgid);
// Return true if all processes in the job have stopped or completed.
bool job_is_stopped (cmdch* chain);
// Return true if all processes in the job have completed.
bool job_is_completed (cmdch* chain);



cmdch* breakcommands(msvec* words);

void pushchain(cmdch* chain);
// pushes COPY of str to vec
void pushstring(msvec* vec, const char* str);

// this function breaks line into separete words, which are later stroed in
// vec variable. Words are splitted on ' ', '\n' or '\t'
msvec* breakline(char** stringptr);

// caution: thins function performs shallow copy
char** getArgs(msvec* vec, int start, int end);

void mstrcat(mlstr* to, const char* from);
void mstrcpy(mlstr* to, const char* from);

// this procedure deallocates everything recursively
void freeall(cmdch* chain);
// this procedure deallocates one job (does not take care of next_job)
void freecmdch(cmdch* chain);
void freemsvec(msvec* vec);
// this procedure deallocates last word in vector
//void freelast(msvec* vec);
void fittosize(mlstr* str, size_t size);
msvec* newmsvec(); // empty vector

// returns line allocated on heap, created as space separated words from msvec
char* getLine(msvec* vec);

#endif