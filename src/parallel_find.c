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

#include "OutputBuffers.h"
#include "QueuePerThreadPool.h"
#include "bf.h"
#include "debug.h"
#include "utils.h"

int processdir(struct QPTPool * ctx, const size_t id, void * data, void * args) {
    char * path = (char *) data;
    OutputBuffers_println((struct OutputBuffers *) args, id, path, strlen(path), stdout);

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
            OutputBuffers_println((struct OutputBuffers *) args, id, entry_path, entry_path_len, stdout);
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

int main(int argc, char *argv[]) {
    struct start_end main_timer;
    timestamp_start(main_timer);

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
    pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
    struct OutputBuffers outbufs;
    if (!OutputBuffers_init(&outbufs, in.maxthreads, in.output_buffer_size, &print_mutex)) {
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
    const size_t completed = QPTPool_threads_completed(pool);
    QPTPool_destroy(pool);

    /* clear out buffered data */
    OutputBuffers_flush_to_single(&outbufs, stdout);

    /* clean up globals */
    OutputBuffers_destroy(&outbufs);

    timestamp_end(main_timer);
    fprintf(stderr, "main + %zu threads (%d simultaneous threads) finished in %.2Lfs\n", completed, in.maxthreads, elapsed(&main_timer));

    return 0;
}
