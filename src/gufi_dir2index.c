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
#include <unistd.h>

#include "QueuePerThreadPool.h"
#include "SinglyLinkedList.h"
#include "bf.h"
#include "dbutils.h"
#include "debug.h"
#include "template_db.h"
#include "utils.h"

extern int errno;

#if BENCHMARK
#include <time.h>

pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;
size_t total_dirs = 0;
size_t total_files = 0;
#endif

int processdir(struct QPTPool *ctx, const size_t id, void *data, void *args) {
    #if BENCHMARK
    pthread_mutex_lock(&global_mutex);
    total_dirs++;
    pthread_mutex_unlock(&global_mutex);
    #endif

    /* skip argument checking */
    /* if (!data) { */
    /*     return 1; */
    /* } */

    /* if (!ctx || (id >= ctx->size)) { */
    /*     free(data); */
    /*     return 1; */
    /* } */

    struct templates *templates = (struct templates *) args;
    struct work *work = (struct work *) data;

    DIR *dir = opendir(work->name);
    if (!dir) {
        fprintf(stderr, "Could not open directory \"%s\"\n", work->name);
        return 1;
    }

    /* get source directory info */
    struct stat dir_st;
    if (lstat(work->name, &dir_st) < 0)  {
        closedir(dir);
        return 1;
    }

    /* create the directory */
    char topath[MAXPATH];

    /* offset by in.name_len to remove prefix */
    const size_t topath_len = SNFORMAT_S(topath, MAXPATH, 3,
                                         in.nameto, in.nameto_len,
                                         "/", (size_t) 1,
                                         work->name + in.name_len, work->name_len - in.name_len);

    /* don't need recursion because parent is guaranteed to exist */
    if (mkdir(topath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
        const int err = errno;
        if (err != EEXIST) {
            fprintf(stderr, "mkdir %s failure: %d %s\n", topath, err, strerror(err));
            closedir(dir);
            return 1;
        }
    }

    /* create the database name */
    char dbname[MAXPATH];
    SNFORMAT_S(dbname, MAXPATH, 3,
               topath, topath_len,
               "/", (size_t) 1,
               DBNAME, DBNAME_LEN);

    /* copy the template file */
    if (copy_template(&templates->db, dbname, dir_st.st_uid, dir_st.st_gid)) {
        closedir(dir);
        return 1;
    }

    sqlite3 *db = opendb(dbname, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, 1, 0
                         , NULL, NULL
                         #if defined(DEBUG) && defined(PER_THREAD_STATS)
                         , NULL, NULL
                         , NULL, NULL
                         #endif
        );
    if (!db) {
        closedir(dir);
        return 1;
    }

    const size_t next_level = work->level + 1;

    /* prepare to insert into the database */
    struct sum summary;
    zeroit(&summary);

    /* prepared statements within db.db */
    sqlite3_stmt *entries_res = insertdbprep(db, ENTRIES_INSERT);
    sqlite3_stmt *xattrs_res = NULL;
    sqlite3_stmt *xattr_files_res = NULL;

    /* external per-user and per-group dbs */
    struct sll xattr_db_list;
    sll_init(&xattr_db_list);

    if (in.xattrs.enabled) {
        xattrs_res = insertdbprep(db, XATTRS_PWD_INSERT);
        xattr_files_res = insertdbprep(db, XATTR_FILES_PWD_INSERT);
    }

    startdb(db);

    struct dirent *entry = NULL;
    size_t rows = 0;
    while ((entry = readdir(dir))) {
        const size_t len = strlen(entry->d_name);

        /* skip . and .. */
        if (entry->d_name[0] == '.') {
            if ((len == 1) ||
                ((len == 2) && (entry->d_name[1] == '.'))) {
                continue;
            }
        }

        /* get entry path */
        struct work e;
        memset(&e, 0, sizeof(struct work));
        e.name_len = SNFORMAT_S(e.name, MAXPATH, 3,
                                work->name, work->name_len,
                                "/", (size_t) 1,
                                entry->d_name, len);

        /* get the entry's metadata */
        if (lstat(e.name, &e.statuso) < 0) {
            continue;
        }

        /* push subdirectories onto the queue */
        if (S_ISDIR(e.statuso.st_mode)) {
            if (work->level < in.max_level) {
                e.type[0] = 'd';
                e.pinode = work->statuso.st_ino;
                e.level = next_level;

                /* make a copy here so that the data can be pushed into the queue */
                /* this is more efficient than malloc+free for every single entry */
                struct work *copy = (struct work *) calloc(1, sizeof(struct work));
                memcpy(copy, &e, sizeof(struct work));

                QPTPool_enqueue(ctx, id, processdir, copy);
            }
            continue;
        }

        rows++;

        /* non directories */
        if (S_ISLNK(e.statuso.st_mode)) {
            e.type[0] = 'l';
            readlink(e.name, e.linkname, MAXPATH);
        }
        else if (S_ISREG(e.statuso.st_mode)) {
            e.type[0] = 'f';
        }
        else {
            /* other types are not stored */
            continue;
        }

        #if BENCHMARK
        pthread_mutex_lock(&global_mutex);
        total_files++;
        pthread_mutex_unlock(&global_mutex);
        #endif

        xattrs_setup(&e.xattrs);
        if (in.xattrs.enabled) {
            xattrs_get(e.name, &e.xattrs);
            insertdbgo_xattrs(&work->statuso, &e,
                              &xattr_db_list, &templates->xattr,
                              topath, topath_len,
                              xattrs_res, xattr_files_res);
        }

        /* get entry relative path (use extra buffer to prevent memcpy overlap) */
        char relpath[MAXPATH];
        const size_t relpath_len = SNFORMAT_S(relpath, MAXPATH, 1, e.name + in.name_len + 1, e.name_len - in.name_len - 1);

        /* overwrite full path with relative path */
        /* e.name_len = */ SNFORMAT_S(e.name, MAXPATH, 1, relpath, relpath_len);

        /* update summary table */
        sumit(&summary, &e);

        /* add entry + xattr names into bulk insert */
        insertdbgo(&e, db, entries_res);

        xattrs_cleanup(&e.xattrs);
    }

    stopdb(db);

    /* entries and xattrs have been inserted */

    /* pull this directory's xattrs because they were not pulled by the parent */
    xattrs_setup(&work->xattrs);
    if (in.xattrs.enabled) {
        /* write out per-user and per-group xattrs */
        sll_destroy(&xattr_db_list, destroy_xattr_db);

        /* keep track of per-user and per-group xattr dbs */
        insertdbfin(xattr_files_res);

        /* directory xattrs go into the same table as entries xattrs */
        xattrs_get(work->name, &work->xattrs);
        insertdbgo_xattrs_avail(work, xattrs_res);
        insertdbfin(xattrs_res);
    }
    insertdbfin(entries_res);

    /* insert this directory's summary data */
    /* the xattrs go into the xattrs_avail table in db.db */
    insertsumdb(db, work, &summary);
    xattrs_cleanup(&work->xattrs);

    closedb(db);
    db = NULL;

    /* ignore errors */
    chmod(topath, work->statuso.st_mode);
    chown(topath, work->statuso.st_uid, work->statuso.st_gid);

    closedir(dir);

    free(work);

    return 0;
}

struct work *validate_inputs() {
    char expathin[MAXPATH];
    char expathout[MAXPATH];
    char expathtst[MAXPATH];

    SNPRINTF(expathtst,MAXPATH,"%s/%s",in.nameto,in.name);
    realpath(expathtst,expathout);
    //printf("expathtst: %s expathout %s\n",expathtst,expathout);
    realpath(in.name,expathin);
    //printf("in.name: %s expathin %s\n",in.name,expathin);
    if (!strcmp(expathin,expathout)) {
        fprintf(stderr,"You are putting the index dbs in input directory\n");
    }

    struct work *root = (struct work *) calloc(1, sizeof(struct work));
    if (!root) {
        fprintf(stderr, "Could not allocate root struct\n");
        return NULL;
    }

    root->name_len = SNFORMAT_S(root->name, MAXPATH, 1, in.name, in.name_len);

    /* get input path metadata */
    if (lstat(root->name, &root->statuso) < 0) {
        fprintf(stderr, "Could not stat source directory \"%s\"\n", in.name);
        free(root);
        return NULL;
    }

    /* check that the input path is a directory */
    if (S_ISDIR(root->statuso.st_mode)) {
        root->type[0] = 'd';
    }
    else {
        fprintf(stderr, "Source path is not a directory \"%s\"\n", in.name);
        free(root);
        return NULL;
    }

    /* check if the source directory can be accessed */
    if (access(root->name, R_OK | X_OK) != 0) {
        fprintf(stderr, "couldn't access input dir '%s': %s\n",
                root->name, strerror(errno));
        free(root);
        return NULL;
    }

    if (!in.nameto_len) {
        fprintf(stderr, "No output path specified\n");
        free(root);
        return NULL;
    }

    /* check if the destination path already exists (not an error) */
    struct stat dst_st;
    if (lstat(in.nameto, &dst_st) == 0) {
        fprintf(stderr, "\"%s\" Already exists!\n", in.nameto);

        /* if the destination path is not a directory (error) */
        if (!S_ISDIR(dst_st.st_mode)) {
            fprintf(stderr, "Destination path is not a directory \"%s\"\n", in.nameto);
            free(root);
            return NULL;
        }
    }

    /* check the output files, if a prefix was provided */
    if (in.outfile) {
        if (!strlen(in.outfilen)) {
            fprintf(stderr, "No output file name specified\n");
            free(root);
            return NULL;
        }

        /* check if the destination path already exists (not an error) */
        for(int i = 0; i < in.maxthreads; i++) {
            char outname[MAXPATH];
            SNPRINTF(outname, MAXPATH, "%s.%d", in.outfilen, i);

            struct stat dst_st;
            if (lstat(in.outfilen, &dst_st) == 0) {
                fprintf(stderr, "\"%s\" Already exists!\n", in.nameto);

                /* if the destination path is not a directory (error) */
                if (S_ISDIR(dst_st.st_mode)) {
                    fprintf(stderr, "Destination path is a directory \"%s\"\n", in.outfilen);
                    free(root);
                    return NULL;
                }
            }
        }
    }

    /* check the output dbs, if a prefix was provided */
    if (in.outdb) {
        if (!strlen(in.outdbn)) {
            fprintf(stderr, "No output db name specified\n");
            free(root);
            return NULL;
        }

        /* check if the destination path already exists (not an error) */
        for(int i = 0; i < in.maxthreads; i++) {
            char outname[MAXPATH];
            SNPRINTF(outname, MAXPATH, "%s.%d", in.outdbn, i);

            struct stat dst_st;
            if (lstat(in.outdbn, &dst_st) == 0) {
                fprintf(stderr, "\"%s\" Already exists!\n", in.nameto);

                /* if the destination path is not a directory (error) */
                if (S_ISDIR(dst_st.st_mode)) {
                    fprintf(stderr, "Destination path is a directory \"%s\"\n", in.outdbn);
                    free(root);
                    return NULL;
                }
            }
        }
    }

    /* create the source root under the destination directory using */
    /* the source directory's permissions and owners */
    /* this allows for the threads to not have to recursively create directories */
    char dst_path[MAXPATH];
    SNPRINTF(dst_path, MAXPATH, "%s/%s", in.nameto, in.name + in.name_len);
    if (dupdir(dst_path, &root->statuso)) {
        fprintf(stderr, "Could not create %s under %s\n", in.name, in.nameto);
        free(root);
        return NULL;
    }

    return root;
}

void sub_help() {
   printf("input_dir         walk this tree to produce GUFI index\n");
   printf("output_dir        build GUFI index here\n");
   printf("\n");
}

int main(int argc, char *argv[]) {
    int idx = parse_cmd_line(argc, argv, "hHn:xz:", 2, "input_dir output_dir", &in);
    if (in.helped)
        sub_help();
    if (idx < 0)
        return -1;
    else {
        /* parse positional args, following the options */
        int retval = 0;
        INSTALL_STR(in.name,   argv[idx++], MAXPATH, "input_dir");
        INSTALL_STR(in.nameto, argv[idx++], MAXPATH, "output_dir");

        if (retval)
            return retval;

        in.name_len = strlen(in.name);
        in.nameto_len = strlen(in.nameto);

        remove_trailing(in.name, &in.name_len, "/", 1);

        /* root is special case */
        if (in.name_len == 0) {
            in.name[0] = '/';
        }
    }

    /* get first work item by validating inputs */
    struct work *root = validate_inputs();
    if (!root) {
        return -1;
    }

    #if BENCHMARK
    fprintf(stderr, "Creating GUFI Index %s in %s with %d threads\n", in.name, in.nameto, in.maxthreads);
    #endif

    struct templates templates;

    init_template_db(&templates.db);
    if (create_dbdb_template(&templates.db) != 0) {
        fprintf(stderr, "Could not create template file\n");
        return -1;
    }

    init_template_db(&templates.xattr);
    if (create_xattrs_template(&templates.xattr) != 0) {
        fprintf(stderr, "Could not create xattr template file\n");
        close_template_db(&templates.db);
        return -1;
    }

    #if BENCHMARK
    struct start_end benchmark;
    clock_gettime(CLOCK_MONOTONIC, &benchmark.start);
    #endif

    struct QPTPool *pool = QPTPool_init(in.maxthreads
                                        #if defined(DEBUG) && defined(PER_THREAD_STATS)
                                        , NULL
                                        #endif
        );
    if (!pool) {
        fprintf(stderr, "Failed to initialize thread pool\n");
        close_template_db(&templates.xattr);
        close_template_db(&templates.db);
        return -1;
    }

    if (QPTPool_start(pool, &templates) != (size_t) in.maxthreads) {
        fprintf(stderr, "Failed to start threads\n");
        close_template_db(&templates.xattr);
        close_template_db(&templates.db);
        return -1;
    }

    QPTPool_enqueue(pool, 0, processdir, root);
    QPTPool_wait(pool);
    QPTPool_destroy(pool);

    close_template_db(&templates.xattr);
    close_template_db(&templates.db);

    #if BENCHMARK
    clock_gettime(CLOCK_MONOTONIC, &benchmark.end);
    const long double processtime = sec(nsec(&benchmark));

    fprintf(stderr, "Total Dirs:            %zu\n",    total_dirs);
    fprintf(stderr, "Total Files:           %zu\n",    total_files);
    fprintf(stderr, "Time Spent Indexing:   %.2Lfs\n", processtime);
    fprintf(stderr, "Dirs/Sec:              %.2Lf\n",  total_dirs / processtime);
    fprintf(stderr, "Files/Sec:             %.2Lf\n",  total_files / processtime);
    #endif

    return 0;
}
