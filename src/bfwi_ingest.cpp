#include <atomic>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <libgen.h>
#include <list>
#include <memory>
#include <mutex>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <vector>
#include <unistd.h>

// #ifdef DEBUG
// #undef DEBUG
// #endif

#ifndef PRINT_STAGE
// #define PRINT_STAGE
#endif

#ifdef DEBUG
#include <chrono>
#endif

extern "C" {

#include "bf.h"
#include "utils.h"
#include "dbutils.h"

}

#define MAXLINE MAXPATH+MAXPATH+MAXPATH

static int templatefd = -1;    // this is really a constant that is set at runtime
static off_t templatesize = 0; // this is really a constant that is set at runtime

// The C++ committee forgot this in C++11
template <typename T, typename ...Args>
std::unique_ptr <T> make_unique(Args&& ...args) {
    return std::unique_ptr <T> (new T(std::forward <Args> (args)...));
}

typedef std::unique_ptr<struct work> Row;
Row new_row() {
    return make_unique <struct work> ();
}

void delete_row(Row & row) {
    // no-op until Row is changed back to a raw pointer
}

// basically thread args
struct ThreadWork {
    ThreadWork()
        : id(0),
          queue(),
          mutex(),
          cv()
    {}

    int id;
    std::list <Row> queue;
    std::mutex mutex;
    std::condition_variable cv;
};

typedef std::pair <ThreadWork, std::thread> WorkPair;
typedef std::vector <WorkPair> State;

int duping(0);
int copying(0);
int opening(0);
int settings(0);
int waiting(0);
int prepping(0);
int bt(0);
int parsing(0);
int inserting(0);
int et(0);
int unprep(0);
int closing(0);
std::mutex mutex;

static inline void incr(int & var) {
    #ifdef PRINT_STAGE
    {
        std::lock_guard <std::mutex> lock(mutex);
        var++;
        std::cout << "duping: " << duping << " copying: " << copying << " opening: " << opening << " settings: " << settings << " waiting: " << waiting << " prepping: " << prepping << " begin_transaction: " << bt << " parsing: " << parsing << " inserting: " << inserting << " end_transaction: " << et << " unprep: " << unprep << " closing: " << closing << std::endl;
    }
    #endif
}

static inline void decr(int & var) {
    #ifdef PRINT_STAGE
    {
        std::lock_guard <std::mutex> lock(mutex);
        var--;
        std::cout << "duping: " << duping << " copying: " << copying << " opening: " << opening << " settings: " << settings << " waiting: " << waiting << " prepping: " << prepping << " begin_transaction: " << bt << " parsing: " << parsing << " inserting: " << inserting << " end_transaction: " << et << " unprep: " << unprep << " closing: " << closing << std::endl;
    }
    #endif
}

void parsetowork (char * delim, char * line, struct work * pinwork) {
    char *p;
    char *q;

    //printf("in parsetowork delim %s line %s\n",delim,line);
    line[strlen(line)-1]= '\0';
    p=line;    q=strstr(p,delim); memset(q, 0, 1); SNPRINTF(pinwork->name,MAXPATH,"%s",p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); SNPRINTF(pinwork->type,2,"%s",p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->statuso.st_ino=atol(p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->statuso.st_mode=atol(p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->statuso.st_nlink=atol(p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->statuso.st_uid=atol(p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->statuso.st_gid=atol(p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->statuso.st_size=atol(p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->statuso.st_blksize=atol(p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->statuso.st_blocks=atol(p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->statuso.st_atime=atol(p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->statuso.st_mtime=atol(p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->statuso.st_ctime=atol(p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); SNPRINTF(pinwork->linkname,MAXPATH,"%s",p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); SNPRINTF(pinwork->xattr,MAXXATTR,"%s",p);
    p=q+1;     q=strstr(p,delim); memset(q, 0, 1); pinwork->crtime=atol(p);
}

// copy the template file instead of creating a new database and new tables for each work item
// the ownership and permissions are set too
static int copy_template(const int src_fd, const char * dst, off_t size, uid_t uid, gid_t gid) {
    incr(copying);

    // ignore errors here
    const int src_db = dup(src_fd);
    const int dst_db = open(dst, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    off_t offset = 0;
    const ssize_t sf = sendfile(dst_db, src_db, &offset, size);
    fchown(dst_db, uid, gid);
    // fchmod(dst_db, mode | S_IRUSR | S_IWUSR);
    close(src_db);
    close(dst_db);

    if (sf == -1) {
        fprintf(stderr, "Could not copy template file to %s\n", dst);
        return -1;
    }

    decr(copying);
    return 0;
}

// Read ahead to figure out where files under directories start
void scout_function(std::atomic_bool & scouting, const char * filename, State & consumers) {
    #ifdef DEBUG
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    #endif

    // figure out whether or not this function is running
    std::ifstream file(filename, std::ios::binary);
    if (!(scouting = (bool) file)) {
        std::cerr << "Could not open file " << filename << std::endl;
        return;
    }

    int tid = 0;
    char line[MAXLINE];
    char delim[2] = {'\x1e', '\x00'};
    std::size_t file_count = 0;
    std::size_t dir_count = 1; // always start with a directory

    Row work = new_row();
    memset(work.get(), 0, sizeof(struct work));
    if (!file.getline(line, MAXLINE)) {
        delete_row(work);
        return;
    }

    parsetowork(delim, line, work.get());

    // set up some stuff using the first entry of the trace
    {
        char buf[MAXPATH];

        // create the prefix of all of the paths
        struct work prefix;
        memset(&prefix, 0, sizeof(prefix));
        SNPRINTF(buf, MAXPATH, "%s", work->name);
        SNPRINTF(prefix.name, MAXPATH, "%s", dirname(buf));
        prefix.statuso.st_mode = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
        dupdir(&prefix);

        // add empty databases to each prefix directory
        char *curr = prefix.name + strlen(prefix.name);
        while (curr != prefix.name) {
            SNPRINTF(buf, MAXPATH, "%s/%s/" DBNAME, in.nameto, prefix.name);
            copy_template(templatefd, buf, templatesize, 0, 0);

            // find the next slash
            while ((curr != prefix.name) && (*curr != '/')) {
                curr--;
            }

            // remove consecutive slashes
            while ((curr != prefix.name) && (*curr == '/')) {
                *curr = '\0';
                curr--;
            }
        }

        // copy the template to the top level as well
        SNPRINTF(buf, MAXPATH, "%s/" DBNAME, in.nameto);
        copy_template(templatefd, buf, templatesize, 0, 0);
    }

    // if the first line is not a directory, create work for root
    // and moved the parsed data under it
    if (work->type[0] == 'f') {
        file_count++;

        Row root = new_row();
        memset(root.get(), 0, sizeof(struct work));

        SNPRINTF(root->name, MAXPATH, "/");
        root->offset = 0;
        root->has_entries = 1;
        root->statuso.st_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
        work = std::move(root);
    }

    while (true) {
        // get starting offset
        std::ostream::streampos starting_offset = file.tellg();

        if (!file.getline(line, MAXLINE)) {
            break;
        }

        Row next = new_row();
        memset(next.get(), 0, sizeof(struct work));

        // parse
        parsetowork(delim, line, next.get());

        // push directories onto queues
        if (next->type[0] == 'd') {
            dir_count++;

            work->has_entries = (work->offset != starting_offset);
            next->offset = file.tellg();

            // put the previous work on the queue
            {
                std::lock_guard <std::mutex> lock(consumers[tid].first.mutex);
                consumers[tid].first.queue.emplace_back(std::move(work));
                consumers[tid].first.cv.notify_all();
            }

            // round robin
            tid++;
            tid %= in.maxthreads;

            work = std::move(next);
        }
        else {
            file_count++;
        }
    }

    // insert the last work item
    {
        work->has_entries = (work->offset != file.tellg());
        std::lock_guard <std::mutex> lock(consumers[tid].first.mutex);
        consumers[tid].first.queue.emplace_back(std::move(work));
        consumers[tid].first.cv.notify_all();
    }

    scouting = false;

    // release all condition variables
    for(int i = 0; i < in.maxthreads; i++) {
        consumers[i].first.cv.notify_all();
    }

    std::lock_guard <std::mutex> lock(mutex);

    #ifdef DEBUG
    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    std::cerr << "Scout finished in " << std::chrono::duration_cast <std::chrono::nanoseconds> (end - start).count() / 1e9 << " seconds"  << std::endl;
    #endif

    std::cerr << "Files: " << file_count << std::endl
              << "Dirs:  " << dir_count << std::endl
              << "Total: " << file_count + dir_count << std::endl;

    return;
}

static sqlite3 * opendb(const char *name)
{
    char dbn[MAXPATH];
    snprintf(dbn, MAXSQL, "%s", name);

    incr(opening);
    sqlite3 * db = NULL;
    if (sqlite3_open_v2(dbn, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_URI, NULL) != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s %s rc %d\n", dbn, sqlite3_errmsg(db), sqlite3_errcode(db));
    }
    decr(opening);

    incr(settings);
    // try to turn sychronization off
    if (sqlite3_exec(db, "PRAGMA synchronous = OFF", NULL, NULL, NULL) != SQLITE_OK) {
        std::cerr << "Could not turn off synchronization" << std::endl;
    }

    // try to turn journaling off
    if (sqlite3_exec(db, "PRAGMA journal_mode = OFF", NULL, NULL, NULL) != SQLITE_OK) {
        std::cerr << "Could not turn off journaling" << std::endl;
    }

    // try to store temp_store in memory
    if (sqlite3_exec(db, "PRAGMA temp_store = 2", NULL, NULL, NULL) != SQLITE_OK) {
        std::cerr << "Could not set temporary storage to in-memory" << std::endl;
    }

    // try to increase the page size
    if (sqlite3_exec(db, "PRAGMA page_size = 16777216", NULL, NULL, NULL) != SQLITE_OK) {
        std::cerr << "Could not set page size" << std::endl;
    }

    // try to increase the cache size
    if (sqlite3_exec(db, "PRAGMA cache_size = 16777216", NULL, NULL, NULL) != SQLITE_OK) {
        std::cerr << "Could not set cache size" << std::endl;
    }

    // try to get an exclusive lock
    if (sqlite3_exec(db, "PRAGMA locking_mode = EXCLUSIVE", NULL, NULL, NULL) != SQLITE_OK) {
        std::cerr << "Could not set locking mode" << std::endl;
    }
    decr(settings);

    return db;
}

// process the work under one directory (no recursion)
// also deletes w
static bool processdir(Row & w, std::ifstream & trace) {
    // might want to skip this check
    if (!w) {
        return false;
    }

    // create the directory
    incr(duping);
    dupdir(w.get());
    decr(duping);

    // create the database name
    char dbname[MAXPATH];
    SNPRINTF(dbname, MAXPATH, "%s/%s/" DBNAME, in.nameto, w->name);

    // copy the template file
    if (copy_template(templatefd, dbname, templatesize, w->statuso.st_uid, w->statuso.st_gid)) {
        delete_row(w);
        return false;
    }

    // don't bother opening the database file if there is nothing to insert
    if (!w->has_entries) {
        delete_row(w);
        return true;
    }

    // process the work
    sqlite3 * db = opendb(dbname);
    if (db) {
        // struct sum summary;
        // zeroit(&summary);

        incr(prepping);
        sqlite3_stmt * res = insertdbprep(db, NULL);
        decr(prepping);

        incr(bt);
        startdb(db);
        decr(bt);

        // move the trace file to the offet
        trace.seekg(w->offset);

        std::size_t rows = 0;
        while (true) {
            char line[MAXLINE];
            if (!trace.getline(line, MAXLINE)) {
                break;
            }

            incr(parsing);
            struct work row;
            parsetowork(in.delim, line, &row);
            decr(parsing);

            // stop on directories, since files are listed first
            if (row.type[0] == 'd') {
                break;
            }

            // // update summary table
            // sumit(&summary,w->head);

            // add row to bulk insert
            incr(inserting);
            insertdbgo(&row,db,res);
            decr(inserting);

            rows++;
            if (rows > 100000) {
                stopdb(db);
                startdb(db);
                rows=0;
            }
        }

        incr(et);
        stopdb(db);
        decr(et);

        incr(unprep);
        insertdbfin(db, res);
        decr(unprep);

        // insertsumdb(db, w, &summary);

        incr(closing);
        closedb(db); // don't set to nullptr
        decr(closing);
    }

    delete_row(w);

    return db;
}

// consumer of work
static std::size_t worker_function(std::atomic_bool & scouting, ThreadWork & tw) {
    std::ifstream trace(in.name, std::ios::binary);
    if (!trace) {
        std::cerr << "Could not open " << in.name << ". Thread not running." << std::endl;
        // possibly move queued work into anther thread's queue?
        return 0;
    }

    std::size_t processed = 0;
    while (scouting || tw.queue.size()) {
        std::list <Row> dirs;
        {
            // #ifdef DEBUG
            // std::chrono::high_resolution_clock::time_point wait_start = std::chrono::high_resolution_clock::now();
            // #endif

            incr(waiting);

            std::unique_lock <std::mutex> lock(tw.mutex);

            // wait for work
            while (scouting && !tw.queue.size()) {
                tw.cv.wait(lock);
            }

            decr(waiting);

            // #ifdef DEBUG
            // std::chrono::high_resolution_clock::time_point wait_end = std::chrono::high_resolution_clock::now();
            // std::cerr << "Thread " << tw.id << " waited for " << std::chrono::duration_cast <std::chrono::nanoseconds> (wait_end - wait_start).count() / 1e9 << " seconds. Queued: " << tw.queue.size() << std::endl;
            // #endif

            if (!scouting && !tw.queue.size()) {
                break;
            }

            // // take single work item
            // dirs.push_back(tw.queue.front());
            // tw.queue.pop_front();

            // take all work
            dirs = std::move(tw.queue);
            tw.queue.clear();
        }

        // #ifdef DEBUG
        // std::chrono::high_resolution_clock::time_point process_start = std::chrono::high_resolution_clock::now();
        // #endif

        // process all work
        for(Row & dir : dirs) {
            processed += processdir(dir, trace);
        }

        // #ifdef DEBUG
        // std::chrono::high_resolution_clock::time_point process_end = std::chrono::high_resolution_clock::now();
        // std::cerr << "Thread " << tw.id << " processed for " << std::chrono::duration_cast <std::chrono::nanoseconds> (process_end - process_start).count() / 1e9 << " seconds." << std::endl;
        // #endif
    }

    return processed;
}

static bool processinit(std::thread & scout, std::atomic_bool & scouting, const char * filename, State & state) {
    scouting = true;

    for(int i = 0; i < in.maxthreads; i++) {
        state[i].first.id = i;
        state[i].second = std::thread(worker_function, std::ref(scouting), std::ref(state[i].first));
    }

    scout = std::thread(scout_function, std::ref(scouting), filename, std::ref(state));

    return true;
}

static void processfin(std::thread & scout, State & state, const bool spawned_threads) {
    scout.join();

    if (spawned_threads) {
        for(WorkPair & wp : state) {
            wp.second.join();
        }
    }
}

// create the initial database file to copy from
static off_t create_template(int & fd) {
    static const char name[] = "tmp.db";

    sqlite3 * db = nullptr;
    if (sqlite3_open_v2(name, &db, SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE | SQLITE_OPEN_URI, NULL) != SQLITE_OK) {
        fprintf(stderr, "Cannot create template database: %s %s rc %d\n", name, sqlite3_errmsg(db), sqlite3_errcode(db));
        return -1;
    }

    create_tables(name, 4, db);
    closedb(db);

    if ((fd = open(name, O_RDONLY)) == -1) {
        fprintf(stderr, "Could not open template file\n");
        return -1;
    }

    // no need for the file to remain on the filesystem
    remove(name);

    return lseek(fd, 0, SEEK_END);
}

// This app allows users to do any of the following: (a) just walk the
// input tree, (b) like a, but also creating corresponding GUFI-tree
// directories, (c) like b, but also creating an index.
int validate_inputs() {
   char expathin[MAXPATH];
   char expathout[MAXPATH];
   char expathtst[MAXPATH];

   if (in.buildindex && !in.nameto[0]) {
      fprintf(stderr, "Building an index '-b' requires a destination dir '-t'.\n");
      return -1;
   }
   if (in.nameto[0] && ! in.buildindex) {
      fprintf(stderr, "Destination dir '-t' found.  Assuming implicit '-b'.\n");
      in.buildindex = 1;        // you're welcome
   }

   SNPRINTF(expathtst,MAXPATH,"%s/%s",in.nameto,in.name);
   realpath(expathtst,expathout);
   //printf("expathtst: %s expathout %s\n",expathtst,expathout);
   realpath(in.name,expathin);
   //printf("in.name: %s expathin %s\n",in.name,expathin);
   if (!strcmp(expathin,expathout)) {
     fprintf(stderr,"You are putting the index dbs in input directory\n");
     in.buildinindir = 1;
   }

   // not errors, but you might want to know ...
   if (! in.nameto[0])
      fprintf(stderr, "WARNING: No GUFI-tree specified (-t).  No GUFI-tree will be built.\n");
   //   else if (! in.buildindex)
   //      fprintf(stderr, "WARNING: Index-building not requested (-b).  No GUFI index will be built.\n");

   return 0;
}

void sub_help() {
   printf("input_dir         walk this tree to produce GUFI-tree\n");
   // printf("GUFI_dir          build GUFI index here (if -b)\n");
   printf("\n");
}

int main(int argc, char * argv[]) {
    #ifdef DEBUG
    std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();
    #endif

    int idx = parse_cmd_line(argc, argv, "hHpn:d:xPbo:t:Du", 1, "input_dir", &in);
    if (in.helped)
        sub_help();
    if (idx < 0)
        return -1;
    else {
        // parse positional args, following the options
        int retval = 0;
        INSTALL_STR(in.name,   argv[idx++], MAXPATH, "input_dir");
        // INSTALL_STR(in.nameto, argv[idx++], MAXPATH, "to_dir");

        if (retval)
            return retval;
    }
    if (validate_inputs())
        return -1;

    State state(in.maxthreads);
    std::atomic_bool scouting;
    std::thread scout;

    if ((templatesize = create_template(templatefd)) == (off_t) -1) {
        std::cerr << "Could not create template file" << std::endl;
        return -1;
    }

    const bool spawned_threads = processinit(scout, scouting, in.name, state);
    processfin(scout, state, spawned_threads);

    close(templatefd);

    // set top level permissions
    chmod(in.nameto, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);

    #ifdef DEBUG
    std::chrono::high_resolution_clock::time_point end = std::chrono::high_resolution_clock::now();
    std::cerr << "main finished in " << std::chrono::duration_cast <std::chrono::nanoseconds> (end - start).count() / 1e9 << " seconds"  << std::endl;
    #endif

    return 0;
}
