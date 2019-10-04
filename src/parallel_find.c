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



#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "QueuePerThreadPool.h"
#include "bf.h"
#include "OutputBuffers.h"
#include "utils.h"

void buffered_print(struct OutputBuffers * outbufs, const size_t id, const char * str, const size_t len) {
    const size_t capacity = outbufs->buffers[id].capacity;

    /* if the str can fit within an empty buffer, try to add the new str to the buffer */
    if (len < capacity) {
        /* if there's not enough space in the buffer to fit the new str, flush it first */
        if ((outbufs->buffers[id].filled + len) >= capacity) {
            OutputBuffer_flush(&outbufs->mutex, &outbufs->buffers[id], stdout);
        }

        char * buf = outbufs->buffers[id].buf;
        size_t filled = outbufs->buffers[id].filled;

        memcpy(&buf[filled], str, len);
        filled += len;

        buf[filled] = '\n';
        filled++;

        outbufs->buffers[id].filled = filled;
    }
    else {
        /* if the str does not fit the buffer, output immediately instead of buffering */
        pthread_mutex_lock(&outbufs->mutex);
        fwrite(str, sizeof(char), len, stdout);
        fwrite("\n", sizeof(char), 1, stdout);
        pthread_mutex_unlock(&outbufs->mutex);
    }
}

int processdir(struct QPTPool * ctx, const size_t id, void * data, void * args) {
    char * path = (char *) data;
    buffered_print((struct OutputBuffers *) args, id, path, strlen(path));

    DIR * dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "Could not open directory %s\n", path);
        free(path);
        return 1;
    }

    /* push subdirectories into the queue */
    struct dirent * entry = NULL;
    while ((entry = readdir(dir))) {
        const size_t len = strlen(entry->d_name);
        if (((len == 1) && (strncmp(entry->d_name, ".",  1) == 0)) ||
            ((len == 2) && (strncmp(entry->d_name, "..", 2) == 0))) {
            continue;
        }

        char entry_path[MAXPATH];
        const size_t entry_path_len = SNFORMAT_S(entry_path, MAXPATH, 3, path, strlen(path), "/", (size_t) 1, entry->d_name, len);

        if ((entry->d_type == DT_DIR)) {
            /* make a clone here so that the data can be pushed into the queue */
            /* this is more efficient than malloc+free for every single entry */
            char * clone = malloc(entry_path_len + 1);
            memcpy(clone, &entry_path, entry_path_len);
            clone[entry_path_len] = '\0';

            // this pushes the dir onto queue - pushdir does locking around queue update
            QPTPool_enqueue(ctx, id, processdir, clone);
        }
        else {
            buffered_print((struct OutputBuffers *) args, id, entry_path, entry_path_len);
        }
    }

    closedir(dir);
    free(path);

    return 0;
}

void sub_help() {
   printf("path         path to walk\n");
   printf("\n");
}

int main(int argc, char *argv[])
{
    /* process input args - all programs share the common 'struct input', */
    /* but allow different fields to be filled at the command-line. */
    /* Callers provide the options-string for get_opt(), which will */
    /* control which options are parsed for each program. */
    int idx = parse_cmd_line(argc, argv, "hHn:B:", 0, "[path ...]", &in);
    if (in.helped)
        sub_help();
    if (idx < 0)
        return -1;

    /* initialize globals */
    struct OutputBuffers outbufs;
    if (!OutputBuffers_init(&outbufs, in.maxthreads, in.output_buffer_size)) {
        return -1;
    }

    struct QPTPool * pool = QPTPool_init(in.maxthreads);
    if (!pool) {
        fprintf(stderr, "Failed to initialize thread pool\n");
        return -1;
    }

    if (QPTPool_start(pool, &outbufs) != (size_t) in.maxthreads) {
        fprintf(stderr, "Failed to start all threads\n");
        return -1;
    }

    if (idx < argc) {
        /* enqueue all input paths */
        for(int i = idx; i < argc; i++) {
            struct stat st;
            if (lstat(argv[i], &st) != 0) {
                fprintf(stderr, "Error: Could not stat \"%s\"\n", argv[i]);
                continue;
            }

            if (!S_ISDIR(st.st_mode) ) {
                fprintf(stdout, "%s\n", argv[i]);
            }
            else {
                char * path = malloc(MAXPATH);

                /* copy argv[i] into the work item */
                SNFORMAT_S(path, MAXPATH, 1, argv[i], strlen(argv[i]));

                /* push the path onto the queue */
                QPTPool_enqueue(pool, i % in.maxthreads, processdir, path);
            }
        }
    }
    else {
        /* default to current directory */
        char * path = malloc(2 * sizeof(char));
        path[0] = '.';
        path[1] = '\0';
        QPTPool_enqueue(pool, 0, processdir, path);
    }

    QPTPool_wait(pool);
    QPTPool_destroy(pool);

    /* clear out buffered data */
    OutputBuffers_flush_single(&outbufs, in.maxthreads, stdout);

    /* clean up globals */
    OutputBuffers_destroy(&outbufs, in.maxthreads);

    return 0;
}
