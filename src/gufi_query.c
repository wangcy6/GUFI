/*
This file is part of GUFI, which is part of MarFS, which is released
under the BSD license.


Copyright (c) 2017, Los Alamos National Security (LANS), LLC
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-----
NOTE:
-----

GUFI uses the C-Thread-Pool library.  The original version, written by
Johan Hanssen Seferidis, is found at
https://github.com/Pithikos/C-Thread-Pool/blob/master/LICENSE, and is
released under the MIT License.  LANS, LLC added functionality to the
original work.  The original work, plus LANS, LLC added functionality is
found at https://github.com/jti-lanl/C-Thread-Pool, also under the MIT
License.  The MIT License can be found at
https://opensource.org/licenses/MIT.


From Los Alamos National Security, LLC:
LA-CC-15-039

Copyright (c) 2017, Los Alamos National Security, LLC All rights reserved.
Copyright 2017. Los Alamos National Security, LLC. This software was produced
under U.S. Government contract DE-AC52-06NA25396 for Los Alamos National
Laboratory (LANL), which is operated by Los Alamos National Security, LLC for
the U.S. Department of Energy. The U.S. Government has rights to use,
reproduce, and distribute this software.  NEITHER THE GOVERNMENT NOR LOS
ALAMOS NATIONAL SECURITY, LLC MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR
ASSUMES ANY LIABILITY FOR THE USE OF THIS SOFTWARE.  If software is
modified to produce derivative works, such modified software should be
clearly marked, so as not to confuse it with the version available from
LANL.

THIS SOFTWARE IS PROVIDED BY LOS ALAMOS NATIONAL SECURITY, LLC AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL LOS ALAMOS NATIONAL SECURITY, LLC OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/



#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <unistd.h>
#include <utime.h>

#include "QueuePerThreadPool.h"
#include "bf.h"
#include "debug.h"
#include "dbutils.h"
#ifndef DEBUG
#include "opendb.h"
#endif
#include "outdbs.h"
#include "outfiles.h"
#include "OutputBuffers.h"
#include "pcre.h"
#include "SinglyLinkedList.h"
#include "utils.h"

extern int errno;
#define AGGREGATE_NAME         "file:aggregate%d?mode=memory&cache=shared"
#define AGGREGATE_ATTACH_NAME  "aggregate"

/* Equivalent to snprintf printing only strings */
/* Varadic arguments should be pairs of strings and their lengths */
size_t SNFORMAT_S(char * dst, const size_t dst_len, size_t count, ...) {
    va_list args;
    size_t max_len = dst_len;

    count *= 2;

    va_start(args, count);
    for(size_t i = 0; i < count; i += 2) {
        char * src = va_arg(args, char *);
        size_t len = va_arg(args, size_t);
        const size_t copy_len = (len < max_len)?len:max_len;
        memcpy(dst, src, copy_len);
        dst += copy_len;
        max_len -= copy_len;
    }
    va_end(args);

    *dst = '\0';
    return dst_len - max_len;
}

#if BENCHMARK
#include <time.h>

static size_t total_files = 0;
static pthread_mutex_t total_files_mutex = PTHREAD_MUTEX_INITIALIZER;

static int total_files_callback(void * unused, int count, char ** data, char ** columns) {
    const size_t files = atol(data[0]);
    pthread_mutex_lock(&total_files_mutex);
    total_files += files;
    pthread_mutex_unlock(&total_files_mutex);
    return 0;
}

#endif

#ifdef DEBUG
#ifdef CUMULATIVE_TIMES
long double total_opendir_time = 0;
long double total_open_time = 0;
long double total_sqlite3_open_time = 0;
long double total_create_tables_time = 0;
long double total_set_pragmas_time = 0;
long double total_attach_time = 0;
long double total_addqueryfuncs_time = 0;
long double total_descend_time = 0;
long double total_check_args_time = 0;
long double total_level_time = 0;
long double total_readdir_time = 0;
long double total_strncmp_time = 0;
long double total_snprintf_time = 0;
long double total_lstat_time = 0;
long double total_isdir_time = 0;
long double total_access_time = 0;
long double total_set_time = 0;
long double total_clone_time = 0;
long double total_pushdir_time = 0;
long double total_exec_time = 0;
long double total_detach_time = 0;
long double total_close_time = 0;
long double total_closedir_time = 0;
long double total_utime_time = 0;
long double total_free_work_time = 0;

long double sll_loop_sum(struct sll * start, struct sll * end) {
    long double sum = 0;
    if (start->size == end->size) {
        struct node * start_node = sll_head_node(start);
        struct node * end_node = sll_head_node(end);

        for(size_t i = 0; i < start->size; i++) {
            struct timespec * start_timestamp = sll_node_data(start_node);
            struct timespec * end_timestamp = sll_node_data(end_node);

            sum += elapsed(start_timestamp, end_timestamp);

            free(start_timestamp);
            free(end_timestamp);

            start_node = sll_next_node(start_node);
            end_node = sll_next_node(end_node);
        }
    }
    else {
        fprintf(stderr, "Different lengths\n");
    }

    return sum;
}

#endif

#ifdef QPTPOOL_TIMESTAMPS
extern uint64_t epoch;
#else
uint64_t epoch = 0;
#endif

static const char GUFI_SQLITE_VFS[] = "unix-none";

int create_table_wrapper(const char *name, sqlite3 * db, const char * sql_name, const char * sql, int (*callback)(void*,int,char**,char**), void * args);
int create_tables(const char *name, sqlite3 *db);
int set_pragmas(sqlite3 * db);

static sqlite3 * opendb2(const char * name, const int rdonly, const int createtables, const int setpragmas
                         #ifdef CUMULATIVE_TIMES
                         , struct timespec * sqlite3_open_start
                         , struct timespec * sqlite3_open_end
                         , struct timespec * create_tables_start
                         , struct timespec * create_tables_end
                         , struct timespec * set_pragmas_start
                         , struct timespec * set_pragmas_end
                         #endif
    ) {
    sqlite3 * db = NULL;

    if (rdonly && createtables) {
        fprintf(stderr, "Cannot open database: readonly and createtables cannot both be set at the same time\n");
        return NULL;
    }

    #ifdef CUMULATIVE_TIMES
    clock_gettime(CLOCK_MONOTONIC, sqlite3_open_start);
    #endif

    int flags = SQLITE_OPEN_URI;
    if (rdonly) {
        flags |= SQLITE_OPEN_READONLY;
    }
    else {
        flags |= SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE;
    }

    if (sqlite3_open_v2(name, &db, flags, GUFI_SQLITE_VFS) != SQLITE_OK) {
        #ifdef CUMULATIVE_TIMES
        clock_gettime(CLOCK_MONOTONIC, sqlite3_open_end);
        #endif
        /* fprintf(stderr, "Cannot open database: %s %s rc %d\n", name, sqlite3_errmsg(db), sqlite3_errcode(db)); */
        sqlite3_close(db); // close db even if it didn't open to avoid memory leaks
        return NULL;
    }
    #ifdef CUMULATIVE_TIMES
    clock_gettime(CLOCK_MONOTONIC, sqlite3_open_end);
    #endif

    #ifdef CUMULATIVE_TIMES
    clock_gettime(CLOCK_MONOTONIC, create_tables_start);
    #endif
    if (createtables) {
        if (create_tables(name, db) != 0) {
            fprintf(stderr, "Cannot create tables: %s %s rc %d\n", name, sqlite3_errmsg(db), sqlite3_errcode(db));
            sqlite3_close(db);
            return NULL;
        }
    }
    #ifdef CUMULATIVE_TIMES
    clock_gettime(CLOCK_MONOTONIC, create_tables_end);
    #endif

    #ifdef CUMULATIVE_TIMES
    clock_gettime(CLOCK_MONOTONIC, set_pragmas_start);
    #endif
    if (setpragmas) {
        // ignore errors
        set_pragmas(db);
    }
    #ifdef CUMULATIVE_TIMES
    clock_gettime(CLOCK_MONOTONIC, set_pragmas_end);
    #endif

    return db;
}
#endif

// Push the subdirectories in the current directory onto the queue
static size_t descend2(struct QPTPool *ctx,
                       const size_t id,
                       struct work *passmywork,
                       DIR *dir,
                       const size_t max_level
                       #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
                       , struct sll *check_args_starts
                       , struct sll *check_args_ends
                       , struct sll *level_starts
                       , struct sll *level_ends
                       , struct sll *readdir_starts
                       , struct sll *readdir_ends
                       , struct sll *strncmp_starts
                       , struct sll *strncmp_ends
                       , struct sll *snprintf_starts
                       , struct sll *snprintf_ends
                       , struct sll *lstat_starts
                       , struct sll *lstat_ends
                       , struct sll *isdir_starts
                       , struct sll *isdir_ends
                       , struct sll *access_starts
                       , struct sll *access_ends
                       , struct sll *set_starts
                       , struct sll *set_ends
                       , struct sll *clone_starts
                       , struct sll *clone_ends
                       , struct sll *pushdir_starts
                       , struct sll *pushdir_ends
                       #endif
    ) {
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    struct timespec * check_args_start = malloc(sizeof(struct timespec));
    clock_gettime(CLOCK_MONOTONIC, check_args_start);
    sll_push(check_args_starts, check_args_start);
    #endif
    /* passmywork was already checked in the calling thread */
    /* if (!passmywork) { */
    /*     fprintf(stderr, "Got NULL work\n"); */
    /*     #if defined(DEBUG) && defined(CUMULATIVE_TIMES) */
    /*     struct timespec * check_args_end = malloc(sizeof(struct timespec)); */
    /*     clock_gettime(CLOCK_MONOTONIC, check_args_end); */
    /*     sll_push(check_args_ends, check_args_end); */
    /*     #endif */
    /*     return 0; */
    /* } */

    /* dir was already checked in the calling thread */
    /* if (!dir) { */
    /*     fprintf(stderr, "Could not open directory %s: %d %s\n", passmywork->name, errno, strerror(errno)); */
    /*     #if defined(DEBUG) && defined(CUMULATIVE_TIMES) */
    /*     struct timespec * check_args_end = malloc(sizeof(struct timespec)); */
    /*     clock_gettime(CLOCK_MONOTONIC, check_args_end); */
    /*     sll_push(check_args_ends, check_args_end); */
    /*     #endif */
    /*     return 0; */
    /* } */
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    struct timespec * check_args_end = malloc(sizeof(struct timespec));
    clock_gettime(CLOCK_MONOTONIC, check_args_end);
    sll_push(check_args_ends, check_args_end);
    #endif

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    struct timespec * level_start = malloc(sizeof(struct timespec));
    clock_gettime(CLOCK_MONOTONIC, level_start);
    sll_push(level_starts, level_start);
    #endif
    size_t pushed = 0;

    const size_t next_level = passmywork->level + 1;
    const int level_check = (next_level <= max_level);
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    struct timespec * level_end = malloc(sizeof(struct timespec));
    clock_gettime(CLOCK_MONOTONIC, level_end);
    sll_push(level_ends, level_end);
    #endif

    if (level_check) {
        // go ahead and send the subdirs to the queue since we need to look
        // further down the tree.  loop over dirents, if link push it on the
        // queue, if file or link print it, fill up qwork structure for
        // each
        struct dirent *entry = NULL;
        while (1) {
            #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
            struct timespec * readdir_start = malloc(sizeof(struct timespec));
            clock_gettime(CLOCK_MONOTONIC, readdir_start);
            sll_push(readdir_starts, readdir_start);
            #endif
            entry = readdir(dir);
            #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
            struct timespec * readdir_end = malloc(sizeof(struct timespec));
            clock_gettime(CLOCK_MONOTONIC, readdir_end);
            sll_push(readdir_ends, readdir_end);
            #endif
            if (!entry) {
                break;
            }

            #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
            struct timespec * strncmp_start = malloc(sizeof(struct timespec));
            clock_gettime(CLOCK_MONOTONIC, strncmp_start);
            sll_push(strncmp_starts, strncmp_start);
            #endif
            const size_t len = strlen(entry->d_name);
            const int skip = (((len == 1) && (strncmp(entry->d_name, ".",  1) == 0)) ||
                              ((len == 2) && (strncmp(entry->d_name, "..", 2) == 0)));
            #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
            struct timespec * strncmp_end = malloc(sizeof(struct timespec));
            clock_gettime(CLOCK_MONOTONIC, strncmp_end);
            sll_push(strncmp_ends, strncmp_end);
            #endif
            if (skip) {
                continue;
            }

            #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
            struct timespec * snprintf_start = malloc(sizeof(struct timespec));
            clock_gettime(CLOCK_MONOTONIC, snprintf_start);
            sll_push(snprintf_starts, snprintf_start);
            #endif
            struct work qwork;
            SNFORMAT_S(qwork.name, MAXPATH, 3, passmywork->name, strlen(passmywork->name), "/", 1, entry->d_name, strlen(entry->d_name));
            #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
            struct timespec * snprintf_end = malloc(sizeof(struct timespec));
            clock_gettime(CLOCK_MONOTONIC, snprintf_end);
            sll_push(snprintf_ends, snprintf_end);
            #endif

            /* #if defined(DEBUG) && defined(CUMULATIVE_TIMES) */
            /* struct timespec * lstat_start = malloc(sizeof(struct timespec)); */
            /* clock_gettime(CLOCK_MONOTONIC, lstat_start); */
            /* sll_push(lstat_starts, lstat_start); */
            /* #endif */
            /* lstat(qwork.name, &qwork.statuso); */
            /* #if defined(DEBUG) && defined(CUMULATIVE_TIMES) */
            /* struct timespec * lstat_end = malloc(sizeof(struct timespec)); */
            /* clock_gettime(CLOCK_MONOTONIC, lstat_end); */
            /* sll_push(lstat_ends, lstat_end); */
            /* #endif */

            #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
            struct timespec * isdir_start = malloc(sizeof(struct timespec));
            clock_gettime(CLOCK_MONOTONIC, isdir_start);
            sll_push(isdir_starts, isdir_start);
            #endif
            const int isdir = (entry->d_type == DT_DIR);
            /* const int isdir = S_ISDIR(qwork.statuso.st_mode); */
            #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
            struct timespec * isdir_end = malloc(sizeof(struct timespec));
            clock_gettime(CLOCK_MONOTONIC, isdir_end);
            sll_push(isdir_ends, isdir_end);
            #endif

            if (isdir) {
                /* #if defined(DEBUG) && defined(CUMULATIVE_TIMES) */
                /* struct timespec * access_start = malloc(sizeof(struct timespec)); */
                /* clock_gettime(CLOCK_MONOTONIC, access_start); */
                /* sll_push(access_starts, access_start); */
                /* #endif */
                /* const int accessible = !access(qwork.name, R_OK | X_OK); */
                /* #if defined(DEBUG) && defined(CUMULATIVE_TIMES) */
                /* struct timespec * access_end = malloc(sizeof(struct timespec)); */
                /* clock_gettime(CLOCK_MONOTONIC, access_end); */
                /* sll_push(access_ends, access_end); */
                /* #endif */

                /* if (accessible) { */
                    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
                    struct timespec * set_start = malloc(sizeof(struct timespec));
                    clock_gettime(CLOCK_MONOTONIC, set_start);
                    sll_push(set_starts, set_start);
                    #endif
                    qwork.level = next_level;
                    qwork.type[0] = 'd';

                    // this is how the parent gets passed on
                    qwork.pinode = passmywork->statuso.st_ino;
                    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
                    struct timespec * set_end = malloc(sizeof(struct timespec));
                    clock_gettime(CLOCK_MONOTONIC, set_end);
                    sll_push(set_ends, set_end);
                    #endif

                    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
                    struct timespec * clone_start = malloc(sizeof(struct timespec));
                    clock_gettime(CLOCK_MONOTONIC, clone_start);
                    sll_push(clone_starts, clone_start);
                    #endif

                    // make a clone here so that the data can be pushed into the queue
                    // this is more efficient than malloc+free for every single entry
                    struct work * clone = (struct work *) malloc(sizeof(struct work));
                    memcpy(clone, &qwork, sizeof(struct work));

                    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
                    struct timespec * clone_end = malloc(sizeof(struct timespec));
                    clock_gettime(CLOCK_MONOTONIC, clone_end);
                    sll_push(clone_ends, clone_end);
                    #endif

                    // this pushes the dir onto queue - pushdir does locking around queue update
                    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
                    struct timespec * pushdir_start = malloc(sizeof(struct timespec));
                    clock_gettime(CLOCK_MONOTONIC, pushdir_start);
                    sll_push(pushdir_starts, pushdir_start);
                    #endif
                    QPTPool_enqueue(ctx, id, clone);
                    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
                    struct timespec * pushdir_end = malloc(sizeof(struct timespec));
                    clock_gettime(CLOCK_MONOTONIC, pushdir_end);
                    sll_push(pushdir_ends, pushdir_end);
                    #endif

                    pushed++;
                /* } */
                /* else { */
                /*     fprintf(stderr, "couldn't access dir '%s': %s\n", */
                /*             qwork->name, strerror(errno)); */
                /* } */
            }
            /* else { */
            /*     fprintf(stderr, "not a dir '%s': %s\n", */
            /*             qwork->name, strerror(errno)); */
            /* } */
        }
    }

    return pushed;
}

// //////////////////////////////////////////////////////
// these functions need to be moved back into dbutils
static void path2(sqlite3_context *context, int argc, sqlite3_value **argv)
{
    const size_t id = QPTPool_get_index((struct QPTPool *) sqlite3_user_data(context), pthread_self());
    sqlite3_result_text(context, gps[id].gpath, -1, SQLITE_TRANSIENT);

    return;
}

int addqueryfuncs2(sqlite3 *db, struct QPTPool * ctx) {
    return !(sqlite3_create_function(db, "path", 0, SQLITE_UTF8, ctx, &path2, NULL, NULL) == SQLITE_OK);
}
// //////////////////////////////////////////////////////

/* sqlite3_exec callback argument data */
struct CallbackArgs {
    struct OutputBuffers * output_buffers;
    int id;
};

size_t flush_buffer(pthread_mutex_t * print_mutex, struct OutputBuffer * output_buffer, FILE * out) {
    /* /\* skip argument checking *\/ */
    /* if (!print_mutex || !output_buffer || !out) { */
    /*     return 0; */
    /* } */

    output_buffer->buf[output_buffer->filled] = '\0';

    pthread_mutex_lock(print_mutex);
    const size_t rc = fwrite(output_buffer->buf, sizeof(char), output_buffer->filled, out);
    pthread_mutex_unlock(print_mutex);

    output_buffer->filled = 0;
    return rc;
}

static int print_callback(void * args, int count, char **data, char **columns) {
    /* skip argument checking */
    /* if (!args) { */
    /*     return 1; */
    /* } */

    struct CallbackArgs * ca = (struct CallbackArgs *) args;
    const int id = ca->id;
    /* if (gts.outfd[id]) { */
        size_t * lens = malloc(count * sizeof(size_t));
        size_t row_len = count + 1; // one delimiter per column + newline
        for(int i = 0; i < count; i++) {
            lens[i] = strlen(data[i]);
            row_len += lens[i];
        }

        // if theres not enough space in the buffer to fit the whole row, flush it first
        if ((ca->output_buffers->buffers[id].filled + row_len + 1) >= ca->output_buffers->buffers[id].capacity) {
            flush_buffer(&ca->output_buffers->mutex, &ca->output_buffers->buffers[id], gts.outfd[id]);
        }

        char * buf = ca->output_buffers->buffers[id].buf;
        size_t filled = ca->output_buffers->buffers[id].filled;
        for(int i = 0; i < count; i++) {
            memcpy(&buf[filled], data[i], lens[i]);
            filled += lens[i];

            buf[filled] = in.delim[0];
            filled++;
        }

        buf[filled] = '\n';
        filled++;

        ca->output_buffers->buffers[id].filled = filled;
        ca->output_buffers->buffers[id].count++;

        // if the new data filled up the buffer, flush it
        if ((ca->output_buffers->buffers[id].filled + 1) >= ca->output_buffers->buffers[id].capacity) {
            flush_buffer(&ca->output_buffers->mutex, &ca->output_buffers->buffers[id], gts.outfd[id]);
        }

        free(lens);
    /* } */
    return 0;
}

struct ThreadArgs {
    struct OutputBuffers output_buffers;
    int (*print_callback_func)(void*,int,char**,char**);
};

int processdir(struct QPTPool * ctx, void * data , const size_t id, void * args) {
    sqlite3 *db = NULL;
    int recs;
    int trecs;
    char shortname[MAXPATH];
    char endname[MAXPATH];
    DIR * dir = NULL;

    /* /\* Can probably skip this *\/ */
    /* if (!data) { */
    /*     return 1; */
    /* } */

    /* /\* Can probably skip this *\/ */
    /* if (!ctx || (id >= ctx->size) || !args) { */
    /*     free(data); */
    /*     return 1; */
    /* } */

    struct work * work = (struct work *) data;
    const size_t work_name_len = strlen(work->name);

    char dbname[MAXSQL];
    SNFORMAT_S(dbname, MAXSQL, 2, work->name, work_name_len, "/" DBNAME, DBNAME_LEN + 1);

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    struct timespec opendir_start;
    struct timespec opendir_end;
    struct timespec open_start;
    struct timespec open_end;
    struct timespec sqlite3_open_start;
    struct timespec sqlite3_open_end;
    struct timespec create_tables_start;
    struct timespec create_tables_end;
    struct timespec set_pragmas_start;
    struct timespec set_pragmas_end;
    struct timespec attach_start;
    struct timespec attach_end;
    struct timespec addqueryfuncs_start;
    struct timespec addqueryfuncs_end;
    struct timespec descend_start;
    struct timespec descend_end;
    struct sll check_args_starts;
    struct sll check_args_ends;
    struct sll level_starts;
    struct sll level_ends;
    struct sll readdir_starts;
    struct sll readdir_ends;
    struct sll strncmp_starts;
    struct sll strncmp_ends;
    struct sll snprintf_starts;
    struct sll snprintf_ends;
    struct sll lstat_starts;
    struct sll lstat_ends;
    struct sll isdir_starts;
    struct sll isdir_ends;
    struct sll access_starts;
    struct sll access_ends;
    struct sll set_starts;
    struct sll set_ends;
    struct sll clone_starts;
    struct sll clone_ends;
    struct sll pushdir_starts;
    struct sll pushdir_ends;
    struct timespec exec_start;
    struct timespec exec_end;
    struct timespec detach_start;
    struct timespec detach_end;
    struct timespec close_start;
    struct timespec close_end;
    struct timespec closedir_start;
    struct timespec closedir_end;
    struct timespec utime_start;
    struct timespec utime_end;
    struct timespec free_work_start;
    struct timespec free_work_end;

    sll_init(&check_args_starts);
    sll_init(&check_args_ends);
    sll_init(&level_starts);
    sll_init(&level_ends);
    sll_init(&readdir_starts);
    sll_init(&readdir_ends);
    sll_init(&strncmp_starts);
    sll_init(&strncmp_ends);
    sll_init(&snprintf_starts);
    sll_init(&snprintf_ends);
    sll_init(&lstat_starts);
    sll_init(&lstat_ends);
    sll_init(&isdir_starts);
    sll_init(&isdir_ends);
    sll_init(&access_starts);
    sll_init(&access_ends);
    sll_init(&set_starts);
    sll_init(&set_ends);
    sll_init(&clone_starts);
    sll_init(&clone_ends);
    sll_init(&pushdir_starts);
    sll_init(&pushdir_ends);
    #endif

    // keep track of the mtime and atime
    struct utimbuf dbtime = {};
    if (in.keep_matime) {
        struct stat st;
        if (lstat(dbname, &st) != 0) {
            perror("stat");
            goto out_free;
        }

        dbtime.actime  = st.st_atime;
        dbtime.modtime = st.st_mtime;
    }

    // open directory
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &opendir_start);
    #endif
    // keep opendir near opendb to help speed up sqlite3_open_v2
    dir = opendir(work->name);
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &opendir_end);
    #endif

    // if the directorty can't be opened, don't bother with anything else
    if (!dir) {
        fprintf(stderr, "Could not open directory %s: %d %s\n", work->name, errno, strerror(errno));
        goto out_free;
    }

    // if we have out db then we have that db open so we just attach the gufi db
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &open_start);
    #endif
    if (in.outdb > 0) {
      db = gts.outdbd[id];
      attachdb(dbname, db, "tree");
    } else {
      db = opendb2(dbname, 1, 0, 0
                   #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
                   , &sqlite3_open_start
                   , &sqlite3_open_end
                   , &create_tables_start
                   , &create_tables_end
                   , &set_pragmas_start
                   , &set_pragmas_end
                   #endif
          );
    }
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &open_end);
    #endif

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &attach_start);
    #endif
    if (in.aggregate_or_print == AGGREGATE) {
        // attach in-memory result aggregation database
        char intermediate_name[MAXSQL];
        SNPRINTF(intermediate_name, MAXSQL, AGGREGATE_NAME, (int) id);
        if (db && !attachdb(intermediate_name, db, AGGREGATE_ATTACH_NAME)) {
            fprintf(stderr, "Could not attach database\n");
            closedb(db);
            goto out_dir;
        }
    }
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &attach_end);
    #endif

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &addqueryfuncs_start);
    #endif
    // this is needed to add some query functions like path() uidtouser() gidtogroup()
    if (db) {
        addqueryfuncs2(db, ctx);
    }
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &addqueryfuncs_end);
    #endif

    recs=1; /* set this to one record - if the sql succeeds it will set to 0 or 1 */
             /* if it fails then this will be set to 1 and will go on */

    // if AND operation, and sqltsum is there, run a query to see if there is a match.
    // if this is OR, as well as no-sql-to-run, skip this query
    if (in.sqltsum_len > 1) {

       if (in.andor == 0) {      // AND
           trecs=rawquerydb(work->name, 0, db, (char *) "select name from sqlite_master where type=\'table\' and name=\'treesummary\';", 0, 0, 0, id);
         if (trecs<1) {
           recs=-1;
         } else {
           recs=rawquerydb(work->name, 0, db, in.sqltsum, 0, 0, 0, id);
         }
      }
      // this is an OR or we got a record back. go on to summary/entries
      // queries, if not done with this dir and all dirs below it
      // this means that no tree table exists so assume we have to go on
      if (recs < 0) {
        recs=1;
      }
    }
    // so we have to go on and query summary and entries possibly
    if (recs > 0) {
        #ifdef DEBUG
        #ifdef CUMULATIVE_TIMES
        clock_gettime(CLOCK_MONOTONIC, &descend_start);
        #endif
        #ifdef SUBDIRECTORY_COUNTS
        const size_t pushed =
        #endif
        #endif
        // push subdirectories into the queue
        descend2(ctx, id, work, dir, in.max_level
                 #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
                 , &check_args_starts
                 , &check_args_ends
                 , &level_starts
                 , &level_ends
                 , &readdir_starts
                 , &readdir_ends
                 , &strncmp_starts
                 , &strncmp_ends
                 , &snprintf_starts
                 , &snprintf_ends
                 , &lstat_starts
                 , &lstat_ends
                 , &isdir_starts
                 , &isdir_ends
                 , &access_starts
                 , &access_ends
                 , &set_starts
                 , &set_ends
                 , &clone_starts
                 , &clone_ends
                 , &pushdir_starts
                 , &pushdir_ends
                 #endif
            );
        #ifdef DEBUG
        #ifdef CUMULATIVE_TIMES
        clock_gettime(CLOCK_MONOTONIC, &descend_end);
        #endif
        #ifdef SUBDIRECTORY_COUNTS
        {
            static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
            pthread_mutex_lock(&print_mutex);
            fprintf(stderr, "%s %zu\n", work->name, pushed);
            pthread_mutex_unlock(&print_mutex);
        }
        #endif
        #endif

        if (db) {
            // only query this level if the min_level has been reached
            if (work->level >= in.min_level) {
                // run query on summary, print it if printing is needed, if returns none
                // and we are doing AND, skip querying the entries db
                // memset(endname, 0, sizeof(endname));
                shortpath(work->name,shortname,endname);
                SNFORMAT_S(gps[id].gepath, MAXPATH, 1, endname, strlen(endname));

                if (in.sqlsum_len > 1) {
                    recs=1; /* set this to one record - if the sql succeeds it will set to 0 or 1 */
                    // for directories we have to take off after the last slash
                    // and set the path so users can put path() in their queries
                    SNFORMAT_S(gps[id].gpath, MAXPATH, 1, shortname, strlen(shortname));
                    //printf("processdir: setting gpath = %s and gepath %s\n",gps[mytid].gpath,gps[mytid].gepath);
                    realpath(work->name,gps[id].gfpath);
                    recs = rawquerydb(work->name, 1, db, in.sqlsum, 1, 0, 0, id);
                    //printf("summary ran %s on %s returned recs %d\n",in.sqlsum,work->name,recs);
                } else {
                    recs = 1;
                }
                if (in.andor > 0)
                    recs = 1;

                // if we have recs (or are running an OR) query the entries table
                if (recs > 0) {
                    if (in.sqlent_len > 1) {
                        // set the path so users can put path() in their queries
                        //printf("****entries len of in.sqlent %lu\n",strlen(in.sqlent));
                        SNFORMAT_S(gps[id].gpath, MAXPATH, 1, work->name, work_name_len);
                        realpath(work->name,gps[id].gfpath);

                        struct ThreadArgs * ta = (struct ThreadArgs *) args;
                        struct CallbackArgs ca;
                        ca.output_buffers = &ta->output_buffers;
                        ca.id = id;

                        /* // make a copy of the print arguments to allow for changing of output file */
                        /* struct print_args pa = * (struct print_args *) args; */
                        /* pa.out = out; */

                        #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
                        clock_gettime(CLOCK_MONOTONIC, &exec_start);
                        #endif
                        char *err = NULL;
                        if (sqlite3_exec(db, in.sqlent, ta->print_callback_func, &ca, &err) != SQLITE_OK) {
                            fprintf(stderr, "Error: %s: %s\n", err, dbname);
                            sqlite3_free(err);
                        }
                        #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
                        clock_gettime(CLOCK_MONOTONIC, &exec_end);
                        #endif

                        #if BENCHMARK
                        // get the total number of files in this database, regardless of whether or not the query was successful
                        if (in.outdb > 0) {
                            sqlite3_exec(db, "SELECT COUNT(*) FROM tree.entries", total_files_callback, NULL, NULL);
                        }
                        else {
                            sqlite3_exec(db, "SELECT COUNT(*) FROM entries", total_files_callback, NULL, NULL);
                        }
                        #endif
                    }
                }
            }
        }
    }

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &detach_start);
    #endif
    if (in.aggregate_or_print == AGGREGATE) {
        // detach in-memory result aggregation database
        if (db && !detachdb(AGGREGATE_NAME, db, AGGREGATE_ATTACH_NAME)) {
            closedb(db);
            goto out_dir;
        }
    }
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &detach_end);
    #endif

    // if we have an out db we just detach gufi db
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &close_start);
    #endif
    if (in.outdb > 0) {
      detachdb(dbname, db, "tree");
    } else {
      closedb(db);
    }
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &close_end);
    #endif

  out_dir:
    ;

    // close dir
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &closedir_start);
    #endif
    closedir(dir);
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &closedir_end);
    #endif

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &utime_start);
    #endif
    // restore mtime and atime
    if (in.keep_matime) {
        utime(dbname, &dbtime);
    }
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &utime_end);
    #endif

  out_free:
    ;

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &free_work_start);
    #endif
    free(work);
    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    clock_gettime(CLOCK_MONOTONIC, &free_work_end);
    #endif

    #ifdef DEBUG
    #ifdef CUMULATIVE_TIMES
    {
        static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&print_mutex);
        total_opendir_time           += elapsed(&opendir_start, &opendir_end);
        total_open_time              += elapsed(&open_start, &open_end);
        total_sqlite3_open_time      += elapsed(&sqlite3_open_start, &sqlite3_open_end);
        total_create_tables_time     += elapsed(&create_tables_start, &create_tables_end);
        total_set_pragmas_time       += elapsed(&set_pragmas_start, &set_pragmas_end);
        total_attach_time            += elapsed(&attach_start, &attach_end);
        total_addqueryfuncs_time     += elapsed(&addqueryfuncs_start, &addqueryfuncs_end);
        total_descend_time           += elapsed(&descend_start, &descend_end);
        total_check_args_time        += sll_loop_sum(&check_args_starts, &check_args_ends);
        total_level_time             += sll_loop_sum(&level_starts, &level_ends);
        total_readdir_time           += sll_loop_sum(&readdir_starts, &readdir_ends);
        total_strncmp_time           += sll_loop_sum(&strncmp_starts, &strncmp_ends);
        total_snprintf_time          += sll_loop_sum(&snprintf_starts, &snprintf_ends);
        total_lstat_time             += sll_loop_sum(&lstat_starts, &lstat_ends);
        total_isdir_time             += sll_loop_sum(&isdir_starts, &isdir_ends);
        total_access_time            += sll_loop_sum(&access_starts, &access_ends);
        total_set_time               += sll_loop_sum(&set_starts, &set_ends);
        total_clone_time             += sll_loop_sum(&clone_starts, &clone_ends);
        total_pushdir_time           += sll_loop_sum(&pushdir_starts, &pushdir_ends);
        total_closedir_time          += elapsed(&closedir_start, &closedir_end);
        total_exec_time              += elapsed(&exec_start, &exec_end);
        total_detach_time            += elapsed(&detach_start, &detach_end);
        total_close_time             += elapsed(&close_start, &close_end);
        total_utime_time             += elapsed(&utime_start, &utime_end);
        total_free_work_time         += elapsed(&free_work_start, &free_work_end);
        pthread_mutex_unlock(&print_mutex);

        sll_destroy(&check_args_starts);
        sll_destroy(&check_args_ends);
        sll_destroy(&level_starts);
        sll_destroy(&level_ends);
        sll_destroy(&readdir_starts);
        sll_destroy(&readdir_ends);
        sll_destroy(&strncmp_starts);
        sll_destroy(&strncmp_ends);
        sll_destroy(&snprintf_starts);
        sll_destroy(&snprintf_ends);
        sll_destroy(&lstat_starts);
        sll_destroy(&lstat_ends);
        sll_destroy(&isdir_starts);
        sll_destroy(&isdir_ends);
        sll_destroy(&access_starts);
        sll_destroy(&access_ends);
        sll_destroy(&set_starts);
        sll_destroy(&set_ends);
        sll_destroy(&clone_starts);
        sll_destroy(&clone_ends);
        sll_destroy(&pushdir_starts);
        sll_destroy(&pushdir_ends);
    }
    #endif

    #ifdef PER_THREAD_STATS
    {
        static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock(&print_mutex);
        fprintf(stderr, "%zu ", id);
        fprintf(stderr, "%s ", work->name);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&opendir_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&opendir_end) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&open_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&open_end) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&sqlite3_open_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&sqlite3_open_end) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&create_tables_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&create_tables_end) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&set_pragmas_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&set_pragmas_end) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&attach_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&attach_end) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&addqueryfuncs_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&addqueryfuncs_end) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&descend_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&descend_end) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&exec_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&exec_end) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&detach_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&detach_end) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&close_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&close_end) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&closedir_start) - epoch);
        fprintf(stderr, "%" PRIu64 " ", timestamp(&closedir_end) - epoch);
        fprintf(stderr, "\n");
        pthread_mutex_unlock(&print_mutex);
    }
    #endif
    #endif

    return 0;
}

void sub_help() {
   printf("GUFI_tree         find GUFI index-tree here\n");
   printf("\n");
}

static void cleanup_intermediates(sqlite3 **intermediates, const size_t count) {
    for(size_t i = 0; i < count; i++) {
        closedb(intermediates[i]);
    }
    free(intermediates);
}

int main(int argc, char *argv[])
{
    #if (defined(DEBUG) && defined(CUMULATIVE_TIMES)) || BENCHMARK
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    #endif

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    epoch = timestamp(&start);
    #endif

    // process input args - all programs share the common 'struct input',
    // but allow different fields to be filled at the command-line.
    // Callers provide the options-string for get_opt(), which will
    // control which options are parsed for each program.
    int idx = parse_cmd_line(argc, argv, "hHT:S:E:Papn:o:d:O:I:F:y:z:G:J:e:m:B:", 1, "GUFI_tree ...", &in);
    if (in.helped)
        sub_help();
    if (idx < 0)
        return -1;

    // load the pcre extension every time a database is opened
    const int rc = sqlite3_auto_extension((void (*)(void)) sqlite3_extension_init);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "sqlite3_auto_extension error: %d\n", rc);
        return -1;
    }

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    struct timespec setup_globals_start;
    clock_gettime(CLOCK_MONOTONIC, &setup_globals_start);
    #endif

    struct ThreadArgs args;

    // initialize globals
    if (!outfiles_init(gts.outfd,  in.outfile, in.outfilen, in.maxthreads)              ||
        !outdbs_init  (gts.outdbd, in.outdb,   in.outdbn,   in.maxthreads, in.sqlinit)  ||
        !OutputBuffers_init(&args.output_buffers, in.maxthreads, in.output_buffer_size)) {
        return -1;
    }

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    struct timespec setup_globals_end;
    clock_gettime(CLOCK_MONOTONIC, &setup_globals_end);
    const long double setup_globals_time = elapsed(&setup_globals_start, &setup_globals_end);
    #endif

    #if BENCHMARK
    fprintf(stderr, "Querying GUFI Index");
    for(int i = idx; i < argc; i++) {
        fprintf(stderr, " %s", argv[i]);
    }
    fprintf(stderr, " with %d threads\n", in.maxthreads);
    #endif

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    struct timespec setup_aggregate_start;
    clock_gettime(CLOCK_MONOTONIC, &setup_aggregate_start);
    #endif

    sqlite3 *aggregate = NULL;
    sqlite3 **intermediates = NULL;
    char aggregate_name[MAXSQL] = {};
    if (in.aggregate_or_print == AGGREGATE) {
        // modify in.sqlent to insert the results into the aggregate table
        char orig_sqlent[MAXSQL];
        SNFORMAT_S(orig_sqlent, MAXSQL, 1, in.sqlent, strlen(in.sqlent));
        SNFORMAT_S(in.sqlent, MAXSQL, 4, "INSERT INTO ", 12, AGGREGATE_ATTACH_NAME, strlen(AGGREGATE_ATTACH_NAME), ".entries ", 9, orig_sqlent, strlen(orig_sqlent));

        // create the aggregate database
        SNPRINTF(aggregate_name, MAXSQL, AGGREGATE_NAME, -1);
        if (!(aggregate = open_aggregate(aggregate_name, AGGREGATE_ATTACH_NAME, orig_sqlent))) {
            return -1;
        }

        intermediates = (sqlite3 **) malloc(sizeof(sqlite3 *) * in.maxthreads);
        for(int i = 0; i < in.maxthreads; i++) {
            char intermediate_name[MAXSQL];
            SNPRINTF(intermediate_name, MAXSQL, AGGREGATE_NAME, (int) i);
            if (!(intermediates[i] = open_aggregate(intermediate_name, AGGREGATE_ATTACH_NAME, orig_sqlent))) {
                fprintf(stderr, "Could not open %s\n", intermediate_name);
                cleanup_intermediates(intermediates, i);
                closedb(aggregate);
                return -1;
            }
        }
    }

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    struct timespec setup_aggregate_end;
    clock_gettime(CLOCK_MONOTONIC, &setup_aggregate_end);
    const long double setup_aggregate_time = elapsed(&setup_aggregate_start, &setup_aggregate_end);
    #endif

    #if (defined(DEBUG) && defined(CUMULATIVE_TIMES)) || BENCHMARK
    long double total_time = 0;

    struct timespec work_start;
    clock_gettime(CLOCK_MONOTONIC, &work_start);
    #endif

    struct QPTPool * pool = QPTPool_init(in.maxthreads);
    if (!pool) {
        fprintf(stderr, "Failed to initialize thread pool\n");
        return -1;
    }

    // enqueue all input paths
    for(int i = idx; i < argc; i++) {
        struct work * mywork = calloc(1, sizeof(struct work));

        // check that the top level path is an accessible directory
        SNFORMAT_S(mywork->name, MAXPATH, 1, argv[i], strlen(argv[i]));

        lstat(mywork->name,&mywork->statuso);
        /* if (access(mywork->name, R_OK | X_OK)) { */
        /*     fprintf(stderr, "couldn't access input dir '%s': %s\n", */
        /*             mywork->name, strerror(errno)); */
        /*     free(mywork); */
        /*     continue; */
        /* } */
        if (!S_ISDIR(mywork->statuso.st_mode) ) {
            fprintf(stderr,"input-dir '%s' is not a directory\n", mywork->name);
            free(mywork);
            continue;
        }

        // push the path onto the queue
        QPTPool_enqueue(pool, i % in.maxthreads, mywork);
    }

    // provide a function to print if PRINT is set
    args.print_callback_func = (((in.aggregate_or_print == PRINT) && in.printdir)?print_callback:NULL);
    if (QPTPool_start(pool, 0, processdir, &args) != (size_t) in.maxthreads) {
        fprintf(stderr, "Failed to start all threads\n");
        return -1;
    }

    QPTPool_wait(pool);

    #if (defined(DEBUG) && defined(CUMULATIVE_TIMES)) || BENCHMARK
    const size_t thread_count =
    #endif
    QPTPool_threads_started(pool);

    QPTPool_destroy(pool);

    // clear out buffered data
    size_t rows = 0;
    for(int i = 0; i < in.maxthreads; i++) {
        flush_buffer(&args.output_buffers.mutex, &args.output_buffers.buffers[i], gts.outfd[i]);
        rows += args.output_buffers.buffers[i].count;
    }

    #if (defined(DEBUG) && defined(CUMULATIVE_TIMES)) || BENCHMARK
    struct timespec work_end;
    clock_gettime(CLOCK_MONOTONIC, &work_end);
    const long double work_time = elapsed(&work_start, &work_end);
    total_time += work_time;
    #endif

    #if (defined(DEBUG) && defined(CUMULATIVE_TIMES)) || BENCHMARK
    long double aggregate_time = 0;
    long double cleanup_time = 0;
    long double output_time = 0;
    #endif

    if (in.aggregate_or_print == AGGREGATE) {
        // prepend the intermediate database query with "INSERT INTO" to move
        // the data from the databases into the final aggregation database
        char intermediate[MAXSQL];
        SNFORMAT_S(intermediate, MAXSQL, 4, "INSERT INTO ", 12, AGGREGATE_ATTACH_NAME, strlen(AGGREGATE_ATTACH_NAME), ".entries ", 9, in.intermediate, strlen(in.intermediate));

        #if (defined(DEBUG) && defined(CUMULATIVE_TIMES)) || BENCHMARK
        struct timespec aggregate_start;
        clock_gettime(CLOCK_MONOTONIC, &aggregate_start);
        #endif

        // aggregate the intermediate results
        for(int i = 0; i < in.maxthreads; i++) {
            if (!attachdb(aggregate_name, intermediates[i], AGGREGATE_ATTACH_NAME)            ||
                (sqlite3_exec(intermediates[i], intermediate, NULL, NULL, NULL) != SQLITE_OK)) {
                printf("Final aggregation error: %s\n", sqlite3_errmsg(intermediates[i]));
            }
         }

        #if (defined(DEBUG) && defined(CUMULATIVE_TIMES)) || BENCHMARK
        struct timespec aggregate_end;
        clock_gettime(CLOCK_MONOTONIC, &aggregate_end);
        aggregate_time = elapsed(&aggregate_start, &aggregate_end);
        total_time += aggregate_time;
        #endif

        #if (defined(DEBUG) && defined(CUMULATIVE_TIMES)) || BENCHMARK
        struct timespec cleanup_start;
        clock_gettime(CLOCK_MONOTONIC, &cleanup_start);
        #endif

        // cleanup the intermediate databases outside of the timing (no need to detach)
        cleanup_intermediates(intermediates, in.maxthreads);

        #if (defined(DEBUG) && defined(CUMULATIVE_TIMES)) || BENCHMARK
        struct timespec cleanup_end;
        clock_gettime(CLOCK_MONOTONIC, &cleanup_end);
        cleanup_time = elapsed(&cleanup_start, &cleanup_end);
        total_time += cleanup_time;
        #endif

        #if (defined(DEBUG) && defined(CUMULATIVE_TIMES)) || BENCHMARK
        struct timespec output_start;
        clock_gettime(CLOCK_MONOTONIC, &output_start);
        #endif

        // run the aggregate query on the aggregated results
        sqlite3_stmt *res = NULL;
        if (sqlite3_prepare_v2(aggregate, in.aggregate, MAXSQL, &res, NULL) == SQLITE_OK) {
            rows = print_results(res, stdout, 1, 0, in.printing, in.delim);
        }
        else {
            fprintf(stderr, "%s\n", sqlite3_errmsg(aggregate));
        }
        sqlite3_finalize(res);

        #if (defined(DEBUG) && defined(CUMULATIVE_TIMES)) || BENCHMARK
        struct timespec output_end;
        clock_gettime(CLOCK_MONOTONIC, &output_end);
        output_time = elapsed(&output_start, &output_end);
        total_time += output_time;
        #endif

        closedb(aggregate);
    }

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    struct timespec cleanup_globals_start;
    clock_gettime(CLOCK_MONOTONIC, &cleanup_globals_start);
    #endif

    // clean up globals
    OutputBuffers_destroy(&args.output_buffers, in.maxthreads);
    outdbs_fin  (gts.outdbd, in.maxthreads, in.sqlfin);
    outfiles_fin(gts.outfd,  in.maxthreads);

    #if defined(DEBUG) && defined(CUMULATIVE_TIMES)
    struct timespec cleanup_globals_end;
    clock_gettime(CLOCK_MONOTONIC, &cleanup_globals_end);
    const long double cleanup_globals_time = elapsed(&cleanup_globals_start, &cleanup_globals_end);

    fprintf(stderr, "set up globals:                              %.2Lfs\n", setup_globals_time);
    fprintf(stderr, "set up intermediate databases:               %.2Lfs\n", setup_aggregate_time);
    fprintf(stderr, "thread pool:                                 %.2Lfs\n", work_time);
    fprintf(stderr, "     open directories:                       %.2Lfs\n", total_opendir_time);
    fprintf(stderr, "     open databases:                         %.2Lfs\n", total_open_time);
    fprintf(stderr, "         sqlite3_open_v2:                    %.2Lfs\n", total_sqlite3_open_time);
    fprintf(stderr, "         create tables:                      %.2Lfs\n", total_create_tables_time);
    fprintf(stderr, "         set pragmas:                        %.2Lfs\n", total_set_pragmas_time);
    fprintf(stderr, "     attach intermediate databases:          %.2Lfs\n", total_attach_time);
    fprintf(stderr, "     addqueryfuncs:                          %.2Lfs\n", total_addqueryfuncs_time);
    fprintf(stderr, "     descend:                                %.2Lfs\n", total_descend_time);
    fprintf(stderr, "         check args:                         %.2Lfs\n", total_check_args_time);
    fprintf(stderr, "         check level:                        %.2Lfs\n", total_level_time);
    fprintf(stderr, "         readdir:                            %.2Lfs\n", total_readdir_time);
    fprintf(stderr, "         strncmp:                            %.2Lfs\n", total_strncmp_time);
    fprintf(stderr, "         snprintf:                           %.2Lfs\n", total_snprintf_time);
    fprintf(stderr, "         lstat:                              %.2Lfs\n", total_lstat_time);
    fprintf(stderr, "         isdir:                              %.2Lfs\n", total_isdir_time);
    fprintf(stderr, "         access:                             %.2Lfs\n", total_access_time);
    fprintf(stderr, "         set:                                %.2Lfs\n", total_set_time);
    fprintf(stderr, "         clone:                              %.2Lfs\n", total_clone_time);
    fprintf(stderr, "         pushdir:                            %.2Lfs\n", total_pushdir_time);
    fprintf(stderr, "     sqlite3_exec                            %.2Lfs\n", total_exec_time);
    fprintf(stderr, "     detach intermediate databases:          %.2Lfs\n", total_detach_time);
    fprintf(stderr, "     close databases:                        %.2Lfs\n", total_close_time);
    fprintf(stderr, "     close directories:                      %.2Lfs\n", total_closedir_time);
    fprintf(stderr, "     restore timestamps:                     %.2Lfs\n", total_utime_time);
    fprintf(stderr, "     free work:                              %.2Lfs\n", total_free_work_time);
    fprintf(stderr, "aggregate into final databases:              %.2Lfs\n", aggregate_time);
    fprintf(stderr, "clean up intermediate databases:             %.2Lfs\n", cleanup_time);
    fprintf(stderr, "print aggregated results:                    %.2Lfs\n", output_time);
    fprintf(stderr, "clean up globals:                            %.2Lfs\n", cleanup_globals_time);
    fprintf(stderr, "\n");
    fprintf(stderr, "Rows returned:                               %zu\n",    rows);
    fprintf(stderr, "Queries performed:                           %zu\n",    (size_t) (thread_count + in.maxthreads + 1));
    fprintf(stderr, "Real time:                                   %.2Lfs\n", total_time);
    #endif

    #if BENCHMARK
    /* struct timespec end; */
    /* clock_gettime(CLOCK_MONOTONIC, &end); */

    /* const long double main_time = elapsed(&start, &end); */

    fprintf(stderr, "Total Dirs:            %zu\n",    thread_count);
    fprintf(stderr, "Total Files:           %zu\n",    total_files);
    fprintf(stderr, "Time Spent Querying:   %.2Lfs\n", total_time);
    fprintf(stderr, "Dirs/Sec:              %.2Lf\n",  thread_count / total_time);
    fprintf(stderr, "Files/Sec:             %.2Lf\n",  total_files / total_time);
    #endif

    return 0;
}
