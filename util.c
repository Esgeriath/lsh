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

static void push_cmd(cmd** to, cmd* val) {

}

static cmd* new_cmd() {
    return calloc(1, sizeof(cmd));
}

static void free_cmds(cmd** c) {

}

cmdch* breakcommands(msvec* words) {
    cmdch* chain = malloc(sizeof(cmdch));

    chain->first = NULL;
    chain->words = words;
    chain->line = NULL;

    cmd** curr = &chain->first;

    bool nextCmd = true;
    bool endsWithRed = false;
    unsigned char usedfds;
    bool pipefromprev = false;

    for (int i = 0 ; i < words->count; i++) {
        if (nextCmd) {
            *curr = new_cmd();
            (*curr)->start = i;
            (*curr)->stop = words->count;
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
            (*curr)->stop = i;
            nextCmd = true;
            do {
                if (strcmp(words->arr[i].ptr, "|") == 0) { // that definitely is end of command
                    //if (usedfds & _mstdoutfd != 0) goto error;
                    //usedfds |= _mstdoutfd;
                    pipefromprev = true; // for next one
                    goto breakinnerloop;
                }
                else if (strcmp(words->arr[i].ptr, "<") == 0) {
                    if (usedfds & _mstdinfd != 0) goto error;
                    usedfds |= _mstdinfd;
                    i++;
                    if (i == words->count) goto error;
                    (*curr)->fd0 = words->arr[i].ptr;
                }
                else if (strcmp(words->arr[i].ptr, ">") == 0) {
                    if (usedfds & _mstdoutfd != 0) goto error;
                    usedfds |= _mstdoutfd;
                    i++;
                    if (i == words->count) goto error;
                    (*curr)->fd1 = words->arr[i].ptr;
                }
                else if (strcmp(words->arr[i].ptr, "2>") == 0) {
                    if (usedfds & _mstderrfd != 0) goto error;
                    usedfds |= _mstderrfd;
                    i++;
                    if (i == words->count) goto error;
                    (*curr)->fd2 = words->arr[i].ptr;
                }
                else {
                    goto breakinnerloop;
                }
            } while (++i < words->count);
breakinnerloop:
            if (pipefromprev && i >= words->count) goto error;
            curr = &(*curr)->next;
        }
    }
    return chain;
error:
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
    if (vec == NULL) return;
    for (int i = 0; i < vec->size; i++) {
        free(vec->arr[i].ptr);
    }
    free(vec->arr);
    free(vec);
}

void freecmdch(cmdch* chain) {
    if (chain == NULL) return;
    cmd* curr = chain->first;
    cmd* nx;
    while (curr) {
        nx = curr->next;
        free(curr);
        curr = nx;
    }
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

char* getLine(msvec* vec) {
    size_t size = 0;
    for (int i = 0; i < vec->count; i++) {
        size += strlen(vec->arr[i].ptr) + 1;
    }
    char* line = malloc((size + 1) * sizeof(char));
    line[0] = '\0';
    for (int i = 0; i < vec->count; i++) {
        strcat(line, vec->arr[i].ptr);
        strcat(line, " ");
    }
    return line;
}