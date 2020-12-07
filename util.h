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
#include <pthread.h>

struct mallstring {
    unsigned short bytes;   // how many char's were allocated
    char* ptr;              // pointer to string
};
typedef struct mallstring mlstr;

struct mallstringvector {
    unsigned short size;    // how many mlstr's were allocated
    unsigned short count;   // how many are used
    mlstr* arr;             // pointer to array of mlstr
};
typedef struct mallstringvector msvec;

struct backgroundchildrenvector {
    size_t size;
    size_t count;
    pid_t* pids;
};
typedef struct backgroundchildrenvector bcvec;

struct command {
    unsigned short start;   // index of the beginning of args
    unsigned short stop;    // index of first non-arg word
    char* fd0;              // stdin
    char* fd1;              // stdout
    char* fd2;              // stderr
    bool pipestonext;
};
typedef struct command cmd;

struct commandchain {
    msvec* words;
    cmd* cmds;
    size_t size;
    size_t count;
};
typedef struct commandchain cmdch;


cmdch* breakcommands(msvec* words);

void pushpid(bcvec* vec, pid_t pid);
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
void freecmdch(cmdch* chain);
void freemsvec(msvec* vec);
// this procedure deallocates last word in vector
void freelast(msvec* vec);
void fittosize(mlstr* str, size_t size);
msvec* newmsvec(); // empty vector

#endif