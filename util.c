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

cmdch* breakcommands(msvec* words) {
    cmdch* chain = malloc(sizeof(cmdch));

    chain->size = 16;
    chain->count = 0;
    chain->cmds = malloc(chain->size * sizeof(cmd));
    chain->words = words;

    bool nextCmd = true;
    bool endsWithRed = false;
    unsigned char usedfds;
    bool pipefromprev = false;

    for (int i = 0 ; i < words->count; i++) {
        if (nextCmd) {
            if (chain->count == chain->size) {
                chain->size += 16;
                chain->cmds = realloc(chain->cmds, chain->size * sizeof(cmd));
            }
            chain->cmds[chain->count].fd0 = NULL;
            chain->cmds[chain->count].fd1 = NULL;
            chain->cmds[chain->count].fd2 = NULL;
            chain->cmds[chain->count].start = i;
            chain->cmds[chain->count].stop = 0;
            chain->cmds[chain->count].pipestonext = false;
            nextCmd = false;
            if (pipefromprev) {
                usedfds = _mstdinfd;
            }
            else {
                usedfds = 0;
            }
            pipefromprev = false;
            endsWithRed = false;
        }
        if (isredirector(words->arr[i].ptr)) {
            endsWithRed = true;
            chain->cmds[chain->count].stop = i;
            nextCmd = true;
            do {
                if (strcmp(words->arr[i].ptr, "|") == 0) { // that definitely is end of command
                    //if (usedfds & _mstdoutfd != 0) goto error;
                    //usedfds |= _mstdoutfd;
                    chain->cmds[chain->count].pipestonext = true;
                    pipefromprev = true; // for next one
                    goto breakinnerloop;
                }
                else if (strcmp(words->arr[i].ptr, "<") == 0) {
                    if (usedfds & _mstdinfd != 0) goto error;
                    usedfds |= _mstdinfd;
                    i++;
                    if (i == words->count) goto error;
                    chain->cmds[chain->count].fd0 = words->arr[i].ptr;
                    /*(*cmds)[cmdcount].fd0 = malloc(strlen(words->arr[i].ptr) * sizeof(char));
                    strcpy((*cmds)[cmdcount].fd0, words->arr[i].ptr);*/
                }
                else if (strcmp(words->arr[i].ptr, ">") == 0) {
                    if (usedfds & _mstdoutfd != 0) goto error;
                    usedfds |= _mstdoutfd;
                    i++;
                    if (i == words->count) goto error;
                    chain->cmds[chain->count].fd1 = words->arr[i].ptr;
                    /*(*cmds)[cmdcount].fd1 = malloc(strlen(words->arr[i].ptr) * sizeof(char));
                    strcpy((*cmds)[cmdcount].fd1, words->arr[i].ptr);*/
                }
                else if (strcmp(words->arr[i].ptr, "2>") == 0) {
                    if (usedfds & _mstderrfd != 0) goto error;
                    usedfds |= _mstderrfd;
                    i++;
                    if (i == words->count) goto error;
                    chain->cmds[chain->count].fd2 = words->arr[i].ptr;
                    /*(*cmds)[cmdcount].fd2 = malloc(strlen(words->arr[i].ptr) * sizeof(char));
                    strcpy((*cmds)[cmdcount].fd2, words->arr[i].ptr);*/
                }
                else {
                    goto breakinnerloop;
                }
            } while (++i < words->count);
breakinnerloop:
            if (pipefromprev && i >= words->count) goto error;
            chain->count++;
        }
    }
    if (!endsWithRed) { // chain->cmds[chain->count].stop == 0 && words->count > 1
        chain->cmds[chain->count].stop = words->count;
        chain->count++;
    }
    //
    return chain;
error:
    //printf("ERROR in cmd production");
    chain->count++; // error can only occur when we started adding command
    freecmdch(chain);
    return NULL;
}




// this function breaks line into separete words, which are later stroed in
// vec variable. Words are splitted on ' ', '\n' or '\t'. Substring inside " "
// is considered one word.
msvec* breakline(char** stringptr) {
    msvec* vec = newmsvec();

	char *ptr = strtok(*stringptr, " \t\n");
    int pos = 0;
    mlstr quotebuff;
    quotebuff.bytes = 32;
    quotebuff.ptr = malloc(quotebuff.bytes * sizeof(char));
    quotebuff.ptr[0] = '\0';

    bool quotemode = false;

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
    return vec;
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

void pushchain(ccvec* vec, cmdch* chain) {
    if (vec->count == vec->size) {
        vec->size += 16;
        vec->arr = realloc(vec->arr, vec->size * sizeof(cmdch*));
        for (int i = vec->count; i < vec->size; i++) {
            vec->arr[i] = NULL;
        }
    }
    vec->arr[vec->count] = chain;
    vec->count++;
}

void pushstring(msvec* vec, const char* str) {
    if (vec->count == vec->size) {
        vec->size += 16;
        vec->arr = realloc(vec->arr, vec->size * sizeof(mlstr));
        for (int i = vec->count; i < vec->size; i++) {
            vec->arr[i].bytes = 0;
            vec->arr[i].ptr = NULL;
        }
    }
    mstrcpy(&(vec->arr[vec->count]), str);
    vec->count++;
}

void mstrcat(mlstr* to, const char* from) {
    fittosize(to, strlen(from));
    strcat(to->ptr, from);
}

void mstrcpy(mlstr* to, const char* from) {
    fittosize(to, strlen(from));
    strcpy(to->ptr, from);
}

void freemsvec(msvec* vec) {
    for (int i = 0; i < vec->size; i++) {
        free(vec->arr[i].ptr);
    }
    free(vec->arr);
    free(vec);
}

void freecmdch(cmdch* chain) {
    if (chain == NULL) return;
    free(chain->cmds);
    freemsvec(chain->words);
    free(chain);
}
/*
void freelast(msvec* vec) {
    free(vec->arr[vec->count - 1].ptr);
    vec->count--;
}
*/
msvec* newmsvec() {
    msvec* vec = malloc(sizeof(msvec));
    vec->count = 0;
    vec->size = 0;
    vec->arr = NULL;
    return vec;
}

void fittosize(mlstr* str, size_t size) {
    if (str->bytes <= size) {
        str->bytes = size + 10;
        str->ptr = realloc(str->ptr, str->bytes * sizeof(char));
    }
}