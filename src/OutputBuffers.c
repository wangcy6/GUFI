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



#include "OutputBuffers.h"

#include <stdlib.h>
#include <string.h>

struct OutputBuffer * OutputBuffer_init(struct OutputBuffer * obuf, const size_t capacity) {
    if (obuf) {
        if (!(obuf->buf = malloc(capacity))) {
            return NULL;
        }
        obuf->capacity = capacity;
        obuf->filled = 0;
        obuf->count = 0;
    }

    return obuf;
}

size_t OutputBuffer_write(struct OutputBuffer * obuf, const void * buf, const size_t size, const int increment_count) {
    if ((obuf->capacity - obuf->filled) < size) {
        return 0;
    }

    memcpy(obuf->buf + obuf->filled, buf, size);
    obuf->filled += size;
    obuf->count += !!increment_count;

    return size;
}

size_t OutputBuffer_flush(struct OutputBuffer * obuf, FILE * out) {
    /* /\* skip argument checking *\/ */
    /* if (!output_buffer || !out) { */
    /*     return 0; */
    /* } */

    const size_t rc = fwrite(obuf->buf, sizeof(char), obuf->filled, out);
    obuf->filled = 0;

    return rc;
}

void OutputBuffer_destroy(struct OutputBuffer * obuf) {
    if (obuf) {
        free(obuf->buf);
    }
}

struct OutputBuffers * OutputBuffers_init(struct OutputBuffers * obufs, const size_t count, const size_t capacity, pthread_mutex_t *global_mutex) {
    if (!obufs) {
        return NULL;
    }

    obufs->mutex = global_mutex;
    obufs->count = 0;
    if (!(obufs->buffers = malloc(count * sizeof(struct OutputBuffer)))) {
        return NULL;
    }

    for(size_t i = 0; i < count; i++) {
        if (!OutputBuffer_init(&obufs->buffers[i], capacity)) {
            OutputBuffers_destroy(obufs);
            return NULL;
        }
        obufs->count++;
    }

    return obufs;
}

size_t OutputBuffers_println(struct OutputBuffers * obufs, const int id, const void * line, const size_t size, FILE * out) {
    /* if (!obufs) { */
    /*     return 0; */
    /* } */

    /* if (!line) { */
    /*     return 0; */
    /* } */

    struct OutputBuffer * obuf = &obufs->buffers[id];

    /* if (!obuf) { */
    /*     return 0; */
    /* } */

    const size_t final_size = size + 1;
    size_t octets = 0;

    /* if a line cannot fit the buffer for whatever reason, flush the existing bufffer */
    if ((obuf->capacity - obuf->filled) < final_size) {
        if (obufs->mutex) {
            pthread_mutex_lock(obufs->mutex);
        }
        octets += OutputBuffer_flush(obuf, stdout);
        if (obufs->mutex) {
            pthread_mutex_unlock(obufs->mutex);
        }
    }

    /* if the line is larger than the entire buffer, flush this row */
    if (obuf->capacity < final_size) {
        /* the existing buffer will have been flushed a few lines ago, maintaining output order */
        if (obufs->mutex) {
            pthread_mutex_lock(obufs->mutex);
        }

        octets += fwrite(line, sizeof(char), size, stdout);
        octets += fwrite("\n", sizeof(char), 1, stdout);

        obufs->buffers[id].count++;
        if (obufs->mutex) {
            pthread_mutex_unlock(obufs->mutex);
        }
    }
    /* otherwise, the line can fit into the buffer, so buffer it */
    /* if the old data + this line cannot fit the buffer, works since old data has been flushed */
    /* if the old data + this line fit the buffer, old data was not flushed, but no issue */
    else {
        char * buf = ((char *) obuf->buf) + obuf->filled;

        memcpy(buf, line, size);
        buf += size;

        *buf = '\n';

        octets += final_size;
        obuf->filled += final_size;
        obuf->count++;
    }

    return octets;
}

size_t OutputBuffers_flush_to_single(struct OutputBuffers * obufs, FILE * out) {
    /* skip argument checking */

    if (obufs->mutex) {
        pthread_mutex_lock(obufs->mutex);
    }

    size_t octets = 0;
    for(size_t i = 0; i < obufs->count; i++) {
        octets += OutputBuffer_flush(&obufs->buffers[i], out);
    }

    if (obufs->mutex) {
        pthread_mutex_unlock(obufs->mutex);
    }

    return octets;
}

size_t OutputBuffers_flush_to_multiple(struct OutputBuffers * obufs, FILE ** out) {
    /* skip argument checking */

    if (obufs->mutex) {
        pthread_mutex_lock(obufs->mutex);
    }

    size_t octets = 0;
    for(size_t i = 0; i < obufs->count; i++) {
        octets += OutputBuffer_flush(&obufs->buffers[i], out[i]);
    }

    if (obufs->mutex) {
        pthread_mutex_unlock(obufs->mutex);
    }

    return octets;
}

void OutputBuffers_destroy(struct OutputBuffers * obufs) {
    if (obufs) {
        if (obufs->buffers) {
            for(size_t i = 0; i < obufs->count; i++) {
                OutputBuffer_destroy(&obufs->buffers[i]);
            }

            free(obufs->buffers);
            obufs->buffers = NULL;
        }

        obufs->count = 0;
    }
}
