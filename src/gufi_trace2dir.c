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



#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "QueuePerThreadPool.h"
#include "bf.h"
#include "debug.h"
#include "trace.h"
#include "utils.h"

extern int errno;

/* Data stored during first pass of input file */
struct row {
    size_t first_delim;
    char * line;
    size_t len;
    long offset;
    size_t entries;
};

struct row * row_init(const size_t first_delim, char * line, const size_t len, const long offset) {
    struct row * row = malloc(sizeof(struct row));
    if (row) {
        row->first_delim = first_delim;
        row->line = line; /* takes ownership of line */
        row->len = len;
        row->offset = offset;
        row->entries = 0;
    }
    return row;
}

void row_destroy(struct row * row) {
    if (row) {
        free(row->line);
        free(row);
    }
}

/* process the work under one directory (no recursion) */
int processdir(struct QPTPool * ctx, const size_t id, void * data, void * args) {
    /* skip argument checking */
    /* if (!data) { */
    /*     return 1; */
    /* } */

    /* if (!ctx || (id >= ctx->size)) { */
    /*     free(data); */
    /*     return 1; */
    /* } */

    (void) ctx;

    struct row * w = (struct row *) data;
    FILE * trace = ((FILE **) args)[id];

    struct work dir;
    /* memset(&dir, 0, sizeof(struct work)); */

    /* parse the directory data */
    linetowork(w->line, w->len, in.delim, &dir);

    /* combine the output directory with the current trace directory */
    char topath[MAXPATH];
    if (w->first_delim) {
        SNFORMAT_S(topath, MAXPATH, 2, in.nameto, in.nameto_len, dir.name, w->first_delim);
    }
    else {
        SNFORMAT_S(topath, MAXPATH, 1, in.nameto, in.nameto_len);
    }

    /* create the directory */
    if (dupdir(topath, &dir.statuso)) {
        const int err = errno;
        fprintf(stderr, "Dupdir failure: %s %d %s\n", topath, err, strerror(err));
        row_destroy(w);
        return 1;
    }

    /* move the trace file to the offet */
    fseek(trace, w->offset, SEEK_SET);

    for(size_t i = 0; i < w->entries; i++) {
        char * line = NULL;
        size_t len = 0;
        if (getline(&line, &len, trace) == -1) {
            free(line);
            break;
        }

        struct work row;
        memset(&row, 0, sizeof(struct work));
        linetowork(line, len, in.delim, &row);
        free(line);

        /* /\* don't need this now because this loop uses the count acquired by the scout function *\/ */
        /* /\* stop on directories, since files are listed first *\/ */
        /* if (row.type[0] == 'd') { */
        /*     break; */
        /* } */

        /* overwrite old filename starting from the prefix */
        /* (not starting from the current directory's path because */
        /* each non-directory also contains the full path) */
        SNFORMAT_S(topath + in.nameto_len, MAXPATH, 1, row.name, strlen(row.name));

        /* all links become real files */
        row.statuso.st_mode &= ~S_IFLNK;
        row.statuso.st_mode |= S_IFREG;

        /* use mknod because tablefs doesn't support open/close */
        if (mknod(topath, row.statuso.st_mode, 0) != 0) {
            const int err = errno;
            fprintf(stderr, "Could not mknod '%s' (type: %s): %s (%d)\n", topath, row.type, strerror(err), err);
        }
    }

    row_destroy(w);

    return 0;
}

size_t parsefirst(char * line, const size_t len, const char delim) {
    size_t first_delim = 0;
    while ((first_delim < len) && (line[first_delim] != delim)) {
        first_delim++;
    }

    if (first_delim == len) {
        first_delim = -1;
    }

    return first_delim;
}

/* Read ahead to figure out where files under directories start */
int scout_function(struct QPTPool * ctx, const size_t id, void * data, void * args) {
    struct start_end scouting;
    clock_gettime(CLOCK_MONOTONIC, &scouting.start);

    /* skip argument checking */
    char * filename = (char *) data;
    FILE * trace = fopen(filename, "rb");

    (void) id;
    (void) args;

    /* the trace file must exist */
    if (!trace) {
        fprintf(stderr, "Could not open file %s\n", filename);
        return 1;
    }

    /* keep current directory while finding next directory */
    /* in order to find out whether or not the current */
    /* directory has files in it */
    char * line = NULL;
    size_t len = 0;
    if (getline(&line, &len, trace) == -1) {
        free(line);
        fclose(trace);
        fprintf(stderr, "Could not get the first line of the trace\n");
        return 1;
    }

    /* find a delimiter */
    size_t first_delim = parsefirst(line, len, in.delim[0]);
    if (first_delim == (size_t) -1) {
        free(line);
        fclose(trace);
        fprintf(stderr, "Could not find the specified delimiter\n");
        return 1;
    }

    /* make sure the first line is a directory */
    if (line[first_delim + 1] != 'd') {
        free(line);
        fclose(trace);
        fprintf(stderr, "First line of trace is not a directory\n");
        return 1;
    }

    struct row * work = row_init(first_delim, line, len, ftell(trace));

    size_t target_thread = 0;

    size_t file_count = 0;
    size_t dir_count = 1; /* always start with a directory */
    size_t empty = 0;

    /* don't free line - the pointer is now owned by work */

    /* have getline allocate a new buffer */
    line = NULL;
    len = 0;
    while (getline(&line, &len, trace) != -1) {
        first_delim = parsefirst(line, len, in.delim[0]);

        /* bad line */
        if (first_delim == (size_t) -1) {
            free(line);
            line = NULL;
            len = 0;
            fprintf(stderr, "Scout encountered bad line ending at offset %ld\n", ftell(trace));
            continue;
        }

        /* push directories onto queues */
        if (line[first_delim + 1] == 'd') {
            dir_count++;

            empty += !work->entries;

            /* put the previous work on the queue */
            QPTPool_enqueue(ctx, target_thread, processdir, work);
            target_thread = (target_thread + 1) % ctx->size;

            /* put the current line into a new work item */
            work = row_init(first_delim, line, len, ftell(trace));
        }
        /* ignore non-directories */
        else {
            work->entries++;
            file_count++;

            /* this line is not needed */
            free(line);
        }

        /* have getline allocate a new buffer */
        line = NULL;
        len = 0;
    }

    /* end of file, so getline failed - still have to free line */
    free(line);

    /* insert the last work item */
    QPTPool_enqueue(ctx, target_thread, processdir, work);

    fclose(trace);

    clock_gettime(CLOCK_MONOTONIC, &scouting.end);

    pthread_mutex_lock(&print_mutex);
    fprintf(stdout, "Scout finished in %.2Lf seconds\n", elapsed(&scouting));
    fprintf(stdout, "Files: %zu\n", file_count);
    fprintf(stdout, "Dirs:  %zu (%zu empty)\n", dir_count, empty);
    fprintf(stdout, "Total: %zu\n", file_count + dir_count);
    pthread_mutex_unlock(&print_mutex);

    return 0;
}

void sub_help() {
   printf("input_file        parse this trace file to produce the GUFI index\n");
   printf("output_dir        build GUFI index here\n");
   printf("\n");
}

void close_per_thread_traces(FILE ** traces, const int count) {
    if (traces) {
        for(int i = 0; i < count; i++) {
            fclose(traces[i]);
        }

        free(traces);
    }
}

FILE ** open_per_thread_traces(char * filename, const int count) {
    FILE ** traces = (FILE **) calloc(count, sizeof(FILE *));
    if (traces) {
        for(int i = 0; i < count; i++) {
            if (!(traces[i] = fopen(filename, "rb"))) {
                close_per_thread_traces(traces, i);
                return NULL;
            }
        }
    }
    return traces;
}

int main(int argc, char * argv[]) {
    define_start(main_call);
    epoch = since_epoch(&main_call.start);

    int idx = parse_cmd_line(argc, argv, "hHn:d:", 2, "input_file output_dir", &in);
    if (in.helped)
        sub_help();
    if (idx < 0)
        return -1;
    else {
        /* parse positional args, following the options */
        int retval = 0;
        INSTALL_STR(in.name,   argv[idx++], MAXPATH, "input_file");
        in.name_len = strlen(in.name);

        char nameto[MAXPATH];
        INSTALL_STR(nameto, argv[idx++], MAXPATH, "output_dir");

        /* make sure in.nameto is followed by a path separator */
        in.nameto_len = SNFORMAT_S(in.nameto, MAXPATH, 2, nameto, strlen(nameto), "/", (size_t) 1);

        if (retval)
            return retval;
    }

    /* open trace files for threads to jump around in */
    /* all have to be passed in at once because there's no way to send one to each thread */
    /* the trace files have to be opened outside of the thread in order to not repeatedly open the files */
    FILE ** traces = open_per_thread_traces(in.name, in.maxthreads);
    if (!traces) {
        fprintf(stderr, "Failed to open trace file for each thread\n");
        return -1;
    }

    struct QPTPool * pool = QPTPool_init(in.maxthreads);
    if (!pool) {
        fprintf(stderr, "Failed to initialize thread pool\n");
        close_per_thread_traces(traces, in.maxthreads);
        return -1;
    }

    if (!QPTPool_start(pool, traces)) {
        fprintf(stderr, "Failed to start threads\n");
        close_per_thread_traces(traces, in.maxthreads);
        return -1;
    }

    /* the scout thread pushes more work into the queue instead of processdir */
    QPTPool_enqueue(pool, 0, scout_function, in.name);

    QPTPool_wait(pool);
    QPTPool_destroy(pool);

    /* set top level permissions */
    chmod(in.nameto, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    close_per_thread_traces(traces, in.maxthreads);

    timestamp_end(main_call);
    fprintf(stderr, "main completed %.2Lf seconds\n", elapsed(&main_call));

    return 0;
}
