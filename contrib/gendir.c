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



/*
This code generates directories in a well defined manner. The root
directory is created first if it does not already exist, and is not
counted in the statistics.  Generation starts under the root
directory, aka starts at level 1. The provided fixed number
subdirectories and files are generated at every directory. At the
bottom level, there are files but no subdirectories.

Each directory has the name d.<number>, where number is in [0, branching factor - 1].
Each file has the name f.<number>, where number is in [0, # of files - 1].

The number of directories generated is

             depth - 1
               ---
           s = \   bf ** i = ((bf ** (depth - 1)) - 1) / (bf - 1)
               /
               ---
              i = 0

The number of files generated is

           # of files * s
*/

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "QueuePerThreadPool.h"
#include "debug.h"
#include "utils.h"

extern int errno;

#define DIR_PERMS (S_IRWXU | S_IRWXG | S_IRWXO)

enum StatType {
    CREATE_DIR = 0,
    OPEN_FILE  = 1,
    CLOSE_FILE = 2,
    MKNOD_FILE = 3,
    STAT_DIR   = 4,
    STAT_FILE  = 5,
    RM_DIR     = 6,
    RM_FILE    = 7,

    MAX_STAT_TYPE
};

struct Stat {
    size_t count;
    long double secs;  // sum of individual calls
    long double min;   // minimum time
    long double max;   // maximum time
};

void Stat_init(struct Stat *stat) {
    stat->count = 0;
    stat->secs = 0;
    stat->min = LDBL_MAX;
    stat->max = -LDBL_MAX;
}

void Stat_add(struct Stat *stats, enum StatType type, const long double secs) {
    stats[type].count++;
    stats[type].secs += secs;

    if (secs < stats[type].min) {
        stats[type].min = secs;
    }

    if (secs > stats[type].max) {
        stats[type].max = secs;
    }
}

void Stat_print(FILE *out, const char *name, struct Stat *stat, const long double realtime) {
    fprintf(out, "%-13s %10zu %16.2Lf %18Lf %19Lf %19Lf\n", name, stat->count, realtime, stat->secs, stat->min, stat->max);
}

// struct passed into QPTPool as extra arguments
struct Args {
    // values extracted from argv
    int create;                   /* -C */
    int remove;                   /* -R */
    int stat;                     /* -T */
    size_t branching_factor;      /* -b */
    size_t max_depth;             /* -d */
    size_t files_per_dir;         /* -f */
    size_t iterations;            /* -i */
    int use_mknod;                /* -k */
    size_t thread_count;          /* -n */

    // run time variables
    struct Stat **stats;          // array of timestamps for each thread
                                  // use thread id to index into this array
                                  // and StatType to index into the timestamp
};

struct path;
struct rm_data {
    struct path *parent;
    size_t subdirs;
    pthread_mutex_t mutex;
};

// work for each thread
struct path {
    char name[MAXPATH];
    size_t current_level;

    struct rm_data *rm;
};

// forward declare metatdata operation functions
// these functions only operate on the provided path
// subdirectories are queued for processing
int create_dir     (struct QPTPool *ctx, const size_t id, void *data, void *extra);
int open_close_file(struct QPTPool *ctx, const size_t id, void *data, void *extra);
int mknod_file     (struct QPTPool *ctx, const size_t id, void *data, void *extra);
int stat_dir       (struct QPTPool *ctx, const size_t id, void *data, void *extra);
int stat_file      (struct QPTPool *ctx, const size_t id, void *data, void *extra);
int rm_dir         (struct QPTPool *ctx, const size_t id, void *data, void *extra);
int rm_file        (struct QPTPool *ctx, const size_t id, void *data, void *extra);

// common code used to run a metadata operation in parallel
int run(struct QPTPool *ctx, const char *path,
        QPTPoolFunc_t func, struct Args *args,
        long double *realtime, enum StatType type) {
    struct path *root = calloc(1, sizeof(struct path));
    if (!root) {
        fprintf(stderr, "Could not allocate root work: %s\n", path);
        return -1;
    }
    memcpy(root->name, path, strlen(path));

    // add work so processing starts immediately
    QPTPool_enqueue(ctx, 0, func, root);

    struct start_end rt;
    timestamp_start(rt);

    // start threads
    if (QPTPool_start(ctx, args) != args->thread_count) {
        fprintf(stderr, "Failed to start threads\n");
        return -1;
    }

    // wait for work to complete
    QPTPool_wait(ctx);

    timestamp_end(rt);
    realtime[type] = elapsed(&rt);

    return 0;
}

int parse_args(int argc, char **argv,
               const char *getopt_str,
               struct Args *args) {
    if (!argv || !getopt_str || !args) {
        return -1;
    }

    memset(args, 0, sizeof(*args));
    args->create = 0;
    args->stat = 0;
    args->branching_factor = 0;
    args->files_per_dir = 0;
    args->iterations = 1;
    args->use_mknod = 0;
    args->thread_count = 1;
    args->max_depth = 0;

    int retval = 0;
    int ch;
    optind = 1; // reset to 1, not 0 (man 3 getopt)
    while ( (ch = getopt(argc, argv, getopt_str)) != -1) {
        switch (ch) {

            case 'C':
                args->create = 1;
                break;

            case 'R':
                args->remove = 1;
                break;

            case 'T':
                args->stat = 1;
                break;

            case 'b':
                INSTALL_UINT(args->branching_factor, optarg, (size_t) 0, (size_t) -1, "-b");
                break;

            case 'd':
                INSTALL_UINT(args->max_depth, optarg, (size_t) 0, (size_t) -1, "-d");
                break;

            case 'f':
                INSTALL_UINT(args->files_per_dir, optarg, (size_t) 0, (size_t) -1, "-f");
                break;

            case 'i':
                INSTALL_UINT(args->iterations, optarg, (size_t) 0, (size_t) -1, "-i");
                break;

            case 'k':
                args->use_mknod = 1;
                break;

            case 'n':
                INSTALL_UINT(args->thread_count, optarg, (size_t) 0, (size_t) -1, "-n");
                break;

            default:
                retval = -1;
                fprintf(stderr, "?? getopt returned character code 0%o ??\n", ch);
                break;
        };
    }

    return (retval != -1)?optind:-1;
}

int main(int argc, char *argv[]) {
    struct Args args;

    int idx = parse_args(argc, argv, "CRTb:d:f:i:kn:", &args);
    if (idx >= argc) {
        fprintf(stderr, "Missing target path argument\n");
        return 1;
    }

    char *rootdir = argv[idx++];

    printf("Target:                     %s\n", rootdir);
    printf("Thread Count:               %zu\n", args.thread_count);
    printf("Levels:                     %zu\n", args.max_depth);
    printf("Branching Factor:           %zu\n", args.branching_factor);
    printf("Files under each directory: %zu\n", args.files_per_dir);
    printf("Iterations:                 %zu\n", args.iterations);

    size_t sum = 1;
    for(size_t i = 0; i < args.max_depth; i++) {
        sum *= args.branching_factor;
    }
    sum = (sum - 1) / (args.branching_factor - 1);

    const size_t expected_dirs = sum * args.iterations;
    const size_t expected_files = expected_dirs * args.files_per_dir;

    printf("Expected Dir Count:         %zu\n", expected_dirs);
    printf("Expected File Count:        %zu\n", expected_files);

    // generate the top level
    if (args.create) {
        struct stat top_st;
        memset(&top_st, 0, sizeof(struct stat));
        top_st.st_mode = DIR_PERMS;
        top_st.st_uid = geteuid();
        top_st.st_uid = getegid();
        if (dupdir(rootdir, &top_st) != 0) {
            fprintf(stderr, "Root directory creation failed for %s: %d %s\n", rootdir, errno, strerror(errno));
            return 1;
        }

        // remove only the root directory so that it can be created by the test
        if (rmdir(rootdir) != 0) {
            fprintf(stderr, "Root directory removeal for %s: %d %s\n", rootdir, errno, strerror(errno));
            return 1;
        }
    }

    // start up threads and push root into the queue for processing
    struct QPTPool *pool = QPTPool_init(args.thread_count);
    if (!pool) {
        fprintf(stderr, "Failed to initialize thread pool\n");
        return 1;
    }

    // initialize stats array
    struct Stat **thread_stats = calloc(args.thread_count, sizeof(struct Stat *));
    if (!thread_stats) {
        QPTPool_destroy(pool);
        return 1;
    }

    for(size_t i = 0; i < args.thread_count; i++) {
        thread_stats[i] = calloc(MAX_STAT_TYPE, sizeof(struct Stat));
        for(size_t j = 0; j < MAX_STAT_TYPE; j++) {
            Stat_init(&thread_stats[i][j]);
        }
    }
    args.stats = thread_stats;

    long double realtime[MAX_STAT_TYPE] = {0};

    int rc = 0;

    struct start_end total_time;
    clock_gettime(CLOCK_MONOTONIC, &total_time.start);

    for(size_t i = 0; (rc == 0) && (i < args.iterations); i++) {
        // create directories
        if (args.create && args.branching_factor) {
            rc = run(pool, rootdir, create_dir, &args, realtime, CREATE_DIR);
        }

        // create files
        if ((rc == 0) && args.create && args.files_per_dir) {
            if (args.use_mknod) {
                rc = run(pool, rootdir, mknod_file, &args, realtime, MKNOD_FILE);
            }
            else {
                rc = run(pool, rootdir, open_close_file, &args, realtime, OPEN_FILE);
                /* realtime[CLOSE_FILE] = realtime[OPEN_FILE]; */
            }
        }

        // stat directories
        if ((rc == 0) && args.stat && args.branching_factor) {
            rc = run(pool, rootdir, stat_dir, &args, realtime, STAT_DIR);
        }

        // stat files
        if ((rc == 0) && args.stat && args.files_per_dir) {
            rc = run(pool, rootdir, stat_file, &args, realtime, STAT_FILE);
        }

        // remove files first so they don't have to be checked when removing directories
        if ((rc == 0) && args.remove && args.files_per_dir) {
            rc = run(pool, rootdir, rm_file, &args, realtime, RM_DIR);
        }

        // remove directories
        if ((rc == 0) && args.remove && args.branching_factor) {
            rc = run(pool, rootdir, rm_dir, &args, realtime, RM_FILE);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &total_time.end);

    const size_t threads_started = QPTPool_threads_started(pool);
    QPTPool_destroy(pool);

    // combine stats
    struct Stat all[MAX_STAT_TYPE];
    for(int type = CREATE_DIR; type < MAX_STAT_TYPE; type++) {
        Stat_init(&all[type]);
    }

    for(size_t i = 0; i < args.thread_count; i++) {
        for(int type = CREATE_DIR; type < MAX_STAT_TYPE; type++) {
            all[type].count += thread_stats[i][type].count;
            all[type].secs += thread_stats[i][type].secs;

            if (all[type].min > thread_stats[i][type].min) {
                all[type].min = thread_stats[i][type].min;
            }

            if (all[type].max < thread_stats[i][type].max) {
                all[type].max = thread_stats[i][type].max;
            }
        }
    }

    const long double run_time = elapsed(&total_time);

    fprintf(stdout, "Total Run Time:             %.2Lfs\n", run_time);
    fprintf(stdout, "Total Threads Run:          %zu\n", threads_started);
    fprintf(stdout, "|    MD Op    |    Count     |   Realtime (s)  |  Linear Time (s)  |      Min (s)      |      Max (s)      |\n");
    fprintf(stdout, "------------------------------------------------------------------------------------------------------------\n");

    if (args.create) {
        if (args.branching_factor) {
            Stat_print(stdout, "mkdir",       &all[CREATE_DIR], realtime[CREATE_DIR]);
        }

        if (args.files_per_dir) {
            if (args.use_mknod) {
                Stat_print(stdout, "mknod",       &all[MKNOD_FILE], realtime[MKNOD_FILE]);
            }
            else {
                Stat_print(stdout, "open",        &all[OPEN_FILE], realtime[OPEN_FILE]);
                Stat_print(stdout, "close",       &all[CLOSE_FILE], realtime[OPEN_FILE]); // cannot separate close from open
            }
        }
    }

    if (args.stat) {
        if (args.branching_factor) {
            Stat_print(stdout, "stat (dir)",  &all[STAT_DIR], realtime[STAT_DIR]);
        }

        if (args.files_per_dir) {
            Stat_print(stdout, "stat (file)", &all[STAT_FILE], realtime[STAT_FILE]);
        }
    }

    if (args.remove) {
        Stat_print(stdout, "rm (dir)",    &all[RM_DIR], realtime[RM_DIR]);
        Stat_print(stdout, "rm (file)",   &all[RM_FILE], realtime[RM_FILE]);
    }

    for(size_t i = 0; i < args.thread_count; i++) {
        free(thread_stats[i]);
    }
    free(thread_stats);

    return rc;
}

// create a directory name
size_t dir_name(char *dst, const size_t dst_size,
                const char *parent, const size_t index) {
    return SNPRINTF(dst, dst_size, "%s/d.%zu", parent, index);
}

// create a file name
size_t file_name(char *dst, const size_t dst_size,
                 const char *parent, const size_t index) {
    return SNPRINTF(dst, dst_size, "%s/f.%zu", parent, index);
}

// common code for pushing work into the thread pool
size_t enqueue_subdirs(struct QPTPool *ctx, const size_t id,
                       struct path *dir, struct Args *args,
                       QPTPoolFunc_t func) {
    size_t pushed = 0;

    const size_t next_level = dir->current_level + 1;
    for(size_t i = 0; i < args->branching_factor; i++) {
        struct path *subdir = calloc(1, sizeof(struct path));
        if (!subdir) {
            continue;
        }

        dir_name(subdir->name, MAXPATH, dir->name, i);
        subdir->current_level = next_level;
        QPTPool_enqueue(ctx, id, func, subdir);
        pushed++;
    }

    return pushed;
}

// generate the given directory
int create_dir(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *dir = (struct path *) data;

    if (dir->current_level >= args->max_depth) {
        free(dir);
        return 0;
    }

    struct start_end create_dir_time;
    timestamp_start(create_dir_time);
    const int rc = mkdir(dir->name, DIR_PERMS);
    timestamp_end(create_dir_time);

    if (rc < 0) {
        if (errno != EEXIST) {
            const int err = errno;
            fprintf(stderr, "mkdir failed for %s: %d %s\n", dir->name, err, strerror(err));
            free(dir);
            return 1;
        }
    }

    // must enqueue after mkdir succeeds to guarantee that
    // the parent exists when the work is processed
    enqueue_subdirs(ctx, id, dir, args, create_dir);

    Stat_add(args->stats[id], CREATE_DIR, elapsed(&create_dir_time));

    free(dir);
    return 0;
}

// function that actually creates the files
// this allows for parallel creates in a single directory
int open_close_file_actual(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *file = (struct path *) data;

    // overwrite any existing data
    // assumes that the parent directory exists
    struct start_end open_time;
    timestamp_start(open_time);
    const int fd = open(file->name, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    timestamp_end(open_time);
    if (fd < 0) {
        fprintf(stderr, "open failed for %s: %d %s\n",file->name, errno, strerror(errno));
        free(file);
        return 1;
    }

    Stat_add(args->stats[id], OPEN_FILE, elapsed(&open_time));

    struct start_end close_time;
    timestamp_start(close_time);
    const int rc = close(fd);
    timestamp_end(close_time);

    if (rc == 0) {
        Stat_add(args->stats[id], CLOSE_FILE, elapsed(&close_time));
    }

    free(file);

    return 0;
}

// generate files in the given directory with open and close
int open_close_file(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *dir = (struct path *) data;

    if (dir->current_level >= args->max_depth) {
        free(dir);
        return 0;
    }

    // queue up the files in this directory for creation
    for(size_t i = 0; i < args->files_per_dir; i++) {
        struct path *file = calloc(1, sizeof(struct path));
        if (!file) {
            continue;
        }

        file_name(file->name, MAXPATH, dir->name, i);
        QPTPool_enqueue(ctx, id, open_close_file_actual, file);
    }

    enqueue_subdirs(ctx, id, dir, args, open_close_file);

    free(dir);
    return 0;
}

// function that actually creates the files
// this allows for parallel creates in a single directory
int mknod_file_actual(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *file = (struct path *) data;

    // assumes that the parent directory exists
    struct start_end mknod_time;
    timestamp_start(mknod_time);
    const int rc = mknod(file->name, S_IFREG | S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH, 0);
    timestamp_end(mknod_time);

    if (rc != 0) {
        const int err = errno;
        if (err != EEXIST) {
            fprintf(stderr, "Failed to create %s: %d %s\n", file->name, err, strerror(err));
            free(file);
        }
    }

    Stat_add(args->stats[id], MKNOD_FILE, elapsed(&mknod_time));

    free(file);
    return 0;
}

// generate files in the given directory with mknod
int mknod_file(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *dir = (struct path *) data;

    if (dir->current_level >= args->max_depth) {
        free(dir);
        return 0;
    }

    // create the files in this directory
    for(size_t i = 0; i < args->files_per_dir; i++) {
        struct path *file = calloc(1, sizeof(struct path));
        if (!file) {
            continue;
        }

        file_name(file->name, MAXPATH, dir->name, i);
        QPTPool_enqueue(ctx, id, mknod_file_actual, file);
    }

    enqueue_subdirs(ctx, id, dir, args, mknod_file);

    free(dir);
    return 0;
}

// call stat on the subdirectories in the given directory
int stat_dir(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *dir = (struct path *) data;

    if (dir->current_level >= args->max_depth) {
        free(dir);
        return 0;
    }

    struct stat st;
    struct start_end stat_time;
    timestamp_start(stat_time);
    const int rc = stat(dir->name, &st);
    timestamp_end(stat_time);

    if (rc < 0) {
        const int err = errno;
        fprintf(stderr, "stat failed for %s: %d %s\n", dir->name, err, strerror(err));
        free(dir);
        return 1;
    }

    enqueue_subdirs(ctx, id, dir, args, stat_dir);

    Stat_add(args->stats[id], STAT_DIR, elapsed(&stat_time));

    free(dir);
    return 0;
}

// function that actually stats the files
// this allows for parallel stats in a single directory
int stat_file_actual(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *file = (struct path *) data;
    struct stat st;

    struct start_end stat_time;
    timestamp_start(stat_time);
    const int rc = stat(file->name, &st);
    timestamp_end(stat_time);

    if (rc < 0) {
        const int err = errno;
        fprintf(stderr, "Failed to stat %s: %d %s\n", file->name, err, strerror(err));
        free(file);
        return 1;
    }

    Stat_add(args->stats[id], STAT_FILE, elapsed(&stat_time));

    free(file);
    return 0;
}

// call stat on the files in the given directory
int stat_file(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *dir = (struct path *) data;

    if (dir->current_level >= args->max_depth) {
        free(dir);
        return 0;
    }

    for(size_t i = 0; i < args->files_per_dir; i++) {
        struct path *file = calloc(1, sizeof(struct path));
        if (!file) {
            continue;
        }

        file_name(file->name, MAXPATH, dir->name, i);
        QPTPool_enqueue(ctx, id, stat_file_actual, file);
    }

    enqueue_subdirs(ctx, id, dir, args, stat_file);

    free(dir);
    return 0;
}

// perform the actual rmdir while coming up from the bottom
int rm_dir_up(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *dir = (struct path *) data;

    pthread_mutex_lock(&dir->rm->mutex);
    const size_t subdirs = dir->rm->subdirs;
    pthread_mutex_unlock(&dir->rm->mutex);

    if (((dir->current_level + 1) >= args->max_depth) ||  // at bottom
        !subdirs) {                                       // or can delete

        struct start_end rm_time;
        timestamp_start(rm_time);
        const int rc = rmdir(dir->name);
        timestamp_end(rm_time);

        if (rc == 0) {
            Stat_add(args->stats[id], RM_DIR, elapsed(&rm_time));
        }
        if (rc < 0) {
            const int err = errno;
            fprintf(stderr, "Failed to rm %s: %d %s\n", dir->name, err, strerror(err));
        }

        // update parent
        if (dir->rm->parent) {
            pthread_mutex_lock(&dir->rm->parent->rm->mutex);
            dir->rm->parent->rm->subdirs--;
            const size_t parent_subdirs = dir->rm->parent->rm->subdirs;
            pthread_mutex_unlock(&dir->rm->parent->rm->mutex);

            if (!parent_subdirs) {
                QPTPool_enqueue(ctx, id, rm_dir_up, dir->rm->parent);
            }
        }
        free(dir->rm);
        free(dir);

        return !!rc;
    }

    return 1;
}

// traverse downwards to register all directories for deletion
int rm_dir_down(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *dir = (struct path *) data;

    if ((dir->current_level + 1) >= args->max_depth) {
        QPTPool_enqueue(ctx, id, rm_dir_up, dir);
        return 0;
    }

    const size_t next_level = dir->current_level + 1;
    for(size_t i = 0; i < args->branching_factor; i++) {
        struct path *subdir = calloc(1, sizeof(struct path));
        if (!subdir) {
            continue;
        }

        dir_name(subdir->name, MAXPATH, dir->name, i);
        subdir->current_level = next_level;
        subdir->rm = calloc(1, sizeof(struct rm_data));
        subdir->rm->parent = dir;
        subdir->rm->subdirs = args->branching_factor;
        subdir->rm->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
        QPTPool_enqueue(ctx, id, rm_dir_down, subdir);
    }

    return 0;
}

int rm_dir(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *dir = (struct path *) data;

    dir->rm = calloc(1, sizeof(struct rm_data));
    dir->rm->parent = NULL;
    dir->rm->subdirs = args->branching_factor;
    dir->rm->mutex = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;

    QPTPool_enqueue(ctx, id, rm_dir_down, dir);

    return 0;
}

// function that actually removes the files
// this allows for parallel removes in a single directory
int rm_file_actual(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *file = (struct path *) data;

    struct start_end rm_time;
    timestamp_start(rm_time);
    const int rc = unlink(file->name);
    timestamp_end(rm_time);

    if (rc < 0) {
        const int err = errno;
        fprintf(stderr, "Failed to rm %s: %d %s\n", file->name, err, strerror(err));
        free(file);
        return 1;
    }

    Stat_add(args->stats[id], RM_FILE, elapsed(&rm_time));

    free(file);
    return 0;
}

// call unlink on the files in the given directory
int rm_file(struct QPTPool *ctx, const size_t id, void *data, void *extra) {
    struct Args *args = (struct Args *) extra;
    struct path *dir = (struct path *) data;

    if (dir->current_level >= args->max_depth) {
        free(dir);
        return 0;
    }

    for(size_t i = 0; i < args->files_per_dir; i++) {
        struct path *file = calloc(1, sizeof(struct path));
        if (!file) {
            continue;
        }

        file_name(file->name, MAXPATH, dir->name, i);
        QPTPool_enqueue(ctx, id, rm_file_actual, file);
    }

    enqueue_subdirs(ctx, id, dir, args, rm_file);

    free(dir);
    return 0;
}
