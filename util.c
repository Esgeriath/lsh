#include "util.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define _mstdinfd  (unsigned char)0b0001U
#define _mstdoutfd (unsigned char)0b0010U
#define _mstderrfd (unsigned char)0b0100U
#define _usedallfd (unsigned char)0b0111U

bool isredirector(const char* str) {
    return (strcmp(str, "|") == 0) || (strcmp(str, "<") == 0)
        || (strcmp(str, ">") == 0) || (strcmp(str, "2>") == 0);
}

int breakcommands(cmd** cmds, msvec* words) {
    int cmdsize = 16;
    *cmds = malloc(cmdsize * sizeof(cmd));
    int cmdcount = 0;
    bool nextCmd = true;
    unsigned char usedfds;
    bool pipefromprev = false;

    for (int i = 0 ; i < words->count; i++) {
        if (nextCmd) {
            if (cmdcount == cmdsize) {
                cmdsize += 16;
                *cmds = realloc(*cmds, cmdsize * sizeof(cmd));
            }
            (*cmds)[cmdcount].fd0 = NULL;
            (*cmds)[cmdcount].fd1 = NULL;
            (*cmds)[cmdcount].fd2 = NULL;
            (*cmds)[cmdcount].start = i;
            nextCmd = false;
            if (pipefromprev) {
                usedfds = _mstdinfd;
            }
            else {
                usedfds = 0;
            }
            pipefromprev = false;;
        }
        if (isredirector(words->arr[i].ptr)) {
            (*cmds)[cmdcount].stop = i;
            nextCmd = true;
            do {
                if (strcmp(words->arr[i].ptr, "|") == 0) { // that definitely is end of command
                    if (usedfds & _mstdoutfd != 0) goto error;
                    //usedfds |= _mstdoutfd;
                    (*cmds)[cmdcount].pipestonext = true;
                    pipefromprev = true; // for next one
                    goto breakinnerloop;
                }
                else if (strcmp(words->arr[i].ptr, "<") == 0) {
                    if (usedfds & _mstdinfd != 0) goto error;
                    usedfds |= _mstdinfd;
                    i++;
                    if (i == words->count) goto error;
                    (*cmds)[cmdcount].fd0 = words->arr[i].ptr;
                    /*(*cmds)[cmdcount].fd0 = malloc(strlen(words->arr[i].ptr) * sizeof(char));
                    strcpy((*cmds)[cmdcount].fd0, words->arr[i].ptr);*/
                }
                else if (strcmp(words->arr[i].ptr, ">") == 0) {
                    if (usedfds & _mstdoutfd != 0) goto error;
                    usedfds |= _mstdoutfd;
                    i++;
                    if (i == words->count) goto error;
                    (*cmds)[cmdcount].fd1 = words->arr[i].ptr;
                    /*(*cmds)[cmdcount].fd1 = malloc(strlen(words->arr[i].ptr) * sizeof(char));
                    strcpy((*cmds)[cmdcount].fd1, words->arr[i].ptr);*/
                }
                else if (strcmp(words->arr[i].ptr, "2>") == 0) {
                    if (usedfds & _mstderrfd != 0) goto error;
                    usedfds |= _mstderrfd;
                    i++;
                    if (i == words->count) goto error;
                    (*cmds)[cmdcount].fd2 = words->arr[i].ptr;
                    /*(*cmds)[cmdcount].fd2 = malloc(strlen(words->arr[i].ptr) * sizeof(char));
                    strcpy((*cmds)[cmdcount].fd2, words->arr[i].ptr);*/
                }
                else {
                    goto breakinnerloop;
                }
            } while (++i < words->count);
breakinnerloop:
            if (usedfds == _usedallfd && pipefromprev && i < words->count) goto error;
            cmdcount++;
        }
    }
    if (usedfds == 0) {
        cmdcount++;         // last cmd was not redirected anwhere, so
    }
    return cmdcount;
error:
    /*for (int i = 0; i <= cmdcount; i++) {
        free((*cmds)[i].fd0);
        free((*cmds)[i].fd1);
        free((*cmds)[i].fd2);
    }*/
    free(*cmds);
    return -1;
}



// this function breaks line into separete words, which are later stroed in
// vec variable. Words are splitted on ' ', '\n' or '\t'. Substring inside " "
// is considerd one word.
void breakline(msvec* vec, char** stringptr) {
	char *ptr = strtok(*stringptr, " \t\n");
    int pos = 0;
    mlstr quotebuff;
    quotebuff.bytes = 32;
    quotebuff.ptr = malloc(quotebuff.bytes * sizeof(char));
    quotebuff.ptr[0] = '\0';
    bool quotemode = false;
    vec->count = 0;

	while(ptr != NULL) {
        if (quotemode || ptr[0] == '"') {
            if (!quotemode) { // start of quotes
                quotemode = true;
                mstrcpy(&quotebuff, ptr + 1); // omiting '"'
                int len = strlen(ptr);
                if (len > 1 && ptr[len - 1] == '"') {
                    quotemode = false;
                    quotebuff.ptr[len - 2] = '\0'; // omiting '"'
                    pushstring(vec, quotebuff.ptr);
                }
            }
            else { // continuing
                mstrcat(&quotebuff, " ");
                mstrcat(&quotebuff, ptr);
                if (ptr[strlen(ptr) - 1] == '"') { // end of quotes
                    quotemode = false;
                    int len = strlen(quotebuff.ptr);
                    quotebuff.ptr[len - 1] = '\0'; // omiting '"'
                    pushstring(vec, quotebuff.ptr);
                }
            }
        }
        else {
            pushstring(vec, ptr);
        }
		ptr = strtok(NULL, " \t\n");
	}
    free(quotebuff.ptr);
}

// caution: thins function performs shallow copy
char** getArgs(msvec* vec, int start, int end) {
    int num = end - start + 1; // additional slot for NULL
    char** arr = malloc(num * sizeof(char*));
    if (arr == NULL) {
        return NULL;
    }
    for (int i = start; i < end; i++) {
        arr[i - start] = vec->arr[i].ptr;
    }
    arr[num - 1] = NULL;
    return arr;
}

void pushpid(bcvec* vec, pid_t pid) {
    if (vec->count == vec->size) {
        vec->size += 16;
        vec->pids = realloc(vec->pids, vec->size * sizeof(pid_t));
    }
    vec->pids[vec->count] = pid;
    vec->count++;
}

void pushstring(msvec* vec, const char* str) {
    if (vec->count >= vec->size) {
        vec->size += 16;
        vec->arr = realloc(vec->arr, vec->size * sizeof(mlstr));
    }
    mstrcpy(&(vec->arr[vec->count]), str);
    vec->count++;
}

void mstrcat(mlstr* to, const char* from) {
    int len = strlen(to->ptr) + strlen(from);
    if (to->bytes <= len) {
        to->bytes = len + 10;
        to->ptr = realloc(to->ptr, to->bytes * sizeof(char));
    }
    strcat(to->ptr, from);
}

void mstrcpy(mlstr* to, const char* from) {
    int len = strlen(from);
    if (to->bytes <= len) {
        to->bytes = len + 10;
        to->ptr = realloc(to->ptr, to->bytes * sizeof(char));
    }
    strcpy(to->ptr, from);
}