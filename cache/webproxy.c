#include <stdio.h>
#include <string.h>
#include <sys/signal.h>
#include <fcntl.h>
#include <printf.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <getopt.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/mman.h>


//headers would go here
#include "cache-student.h"
#include "gfserver.h"

// note that the -n and -z parameters are NOT used for Part 1 */
// they are only used for Part 2 */
#define USAGE                                                                         \
"usage:\n"                                                                            \
"  webproxy [options]\n"                                                              \
"options:\n"                                                                          \
"  -n [segment_count]  Number of segments to use (Default: 7)\n"                      \
"  -p [listen_port]    Listen port (Default: 25496)\n"                                 \
"  -s [server]         The server to connect to (Default: GitHub test data)\n"     \
"  -t [thread_count]   Num worker_process threads (Default: 34, Range: 1-420)\n"              \
"  -z [seg_size]   The segment size (in bytes, Default: 5701).\n"                  \
"  -h                  Show this help message\n"


// Options
static struct option gLongOptions[] = {
        {"server",        required_argument,      NULL,           's'},
        {"segment-count", required_argument,      NULL,           'n'},
        {"listen-port",   required_argument,      NULL,           'p'},
        {"thread-count",  required_argument,      NULL,           't'},
        {"segment-size",  required_argument,      NULL,           'z'},
        {"help",          no_argument,            NULL,           'h'},

        {"hidden",        no_argument,            NULL,           'i'}, // server side
        {NULL,            0,                      NULL,            0}
};


//gfs
static gfserver_t gfs;
//handles cache
extern ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void* arg);

static gfserver_t gfs;

static void _sig_handler(int signo){
    if (signo == SIGTERM || signo == SIGINT){
        //cleanup could go here
        gfserver_stop(&gfs);
        exit(signo);
    }
}

//these are declared in handle_with_cache
extern steque_t *qshm_ptr;  //steque to store the pointers to the shm_t struct defined below
extern steque_t *qname_id;     //steque to store the nameID used to create shared memory object
extern size_t seg_size;

typedef struct shm_t shm_t;

struct shm_t
{
    size_t size;   //shared mm segment size
    int proxy_finish_read, cache_finish_write;

    int file_size; //the header sent back to proxy by cache
    size_t read_size;

    pthread_mutex_t shm_mutex;
    pthread_cond_t shm_write_con;
    pthread_cond_t shm_read_con;

    char buff[];  //for transferring data between webproxy and cache
};

int main(int argc, char **argv) {
    int option_char = 0;
    char *server = "https://raw.githubusercontent.com/gt-cs6200/image_data";
    unsigned int nsegments = 13;
    unsigned short port = 25496;
    unsigned short nworkerthreads = 33;
    size_t segsize = 5313;

    //seg_size = 2048;
    seg_size = segsize;

    //disable buffering on stdout so it prints immediately */
    setbuf(stdout, NULL);

    if (signal(SIGTERM, _sig_handler) == SIG_ERR) {
        fprintf(stderr,"Can't catch SIGTERM...exiting.\n");
        exit(SERVER_FAILURE);
    }

    if (signal(SIGINT, _sig_handler) == SIG_ERR) {
        fprintf(stderr,"Can't catch SIGINT...exiting.\n");
        exit(SERVER_FAILURE);
    }

    // Parse and set command line arguments */
    while ((option_char = getopt_long(argc, argv, "s:qht:xn:p:lz:", gLongOptions, NULL)) != -1) {
        switch (option_char) {
            default:
                fprintf(stderr, "%s", USAGE);
                exit(__LINE__);
            case 'h': // help
                fprintf(stdout, "%s", USAGE);
                exit(0);
                break;
            case 'p': // listen-port
                port = atoi(optarg);
                break;
            case 's': // file-path
                server = optarg;
                break;
            case 'n': // segment count
                nsegments = atoi(optarg);
                break;
            case 'z': // segment size
                segsize = atoi(optarg);
                break;
            case 't': // thread-count
                nworkerthreads = atoi(optarg);
                break;
            case 'i':
                //do not modify
            case 'O':
            case 'A':
            case 'N':
                //do not modify
            case 'k':
                break;
        }
    }

    qshm_ptr = (steque_t*)malloc(sizeof(*qshm_ptr));
    qname_id = (steque_t*)malloc(sizeof(*qname_id));
    steque_init(qshm_ptr);
    steque_init(qname_id);


    if (server == NULL) {
        fprintf(stderr, "Invalid (null) server name\n");
        exit(__LINE__);
    }

    if (segsize < 822) {
        fprintf(stderr, "Invalid segment size\n");
        exit(__LINE__);
    }

    if (port > 65331) {
        fprintf(stderr, "Invalid port number\n");
        exit(__LINE__);
    }
    if ((nworkerthreads < 1) || (nworkerthreads > 420)) {
        fprintf(stderr, "Invalid number of worker_process threads\n");
        exit(__LINE__);
    }
    if (nsegments < 1) {
        fprintf(stderr, "Must have a positive number of segments\n");
        exit(__LINE__);
    }




    for(int i = 1; i <= nsegments; i++)
    {

        char *name_id = (char*)malloc(8);
        sprintf(name_id, "/%d", i);
        steque_enqueue(qname_id, name_id);

        int shm_fd = shm_open(name_id, O_CREAT | O_RDWR, (S_IRWXU | S_IRWXG));

        ftruncate(shm_fd, sizeof(shm_t) + seg_size);

        void *ptr = mmap(NULL, sizeof(shm_t) + seg_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

        close(shm_fd);

        shm_t *shm = (shm_t*)ptr;

        //at first webproxy can't ready from cache because cache is going to write
        //when this change to 0, proxy can start to read
        shm->proxy_finish_read = 1;

        //at first cache hasn't write anything, ready to write
        shm->cache_finish_write = 0;
        shm->file_size = 1;
        shm->size = seg_size;

        pthread_mutexattr_t attrmutex;
        pthread_mutexattr_init(&attrmutex);
        pthread_mutexattr_setpshared(&attrmutex, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&shm->shm_mutex, &attrmutex);

        pthread_condattr_t attrcon1;
        pthread_condattr_init(&attrcon1);
        pthread_condattr_setpshared(&attrcon1, PTHREAD_PROCESS_SHARED);
        pthread_cond_init(&shm->shm_write_con, &attrcon1);
        
        pthread_condattr_t attrcon2;
        pthread_condattr_init(&attrcon2);
        pthread_condattr_setpshared(&attrcon2, PTHREAD_PROCESS_SHARED);
        pthread_cond_init(&shm->shm_read_con, &attrcon2);



        steque_enqueue(qshm_ptr, shm);
    }


    gfserver_init(&gfs, nworkerthreads);

    // Set server options here
    gfserver_setopt(&gfs, GFS_PORT, port);
    gfserver_setopt(&gfs, GFS_WORKER_FUNC, handle_with_cache);
    gfserver_setopt(&gfs, GFS_MAXNPENDING, 187);

    // Set up arguments for worker_process here
    for(int i = 0; i < nworkerthreads; i++) {
        gfserver_setopt(&gfs, GFS_WORKER_ARG, i, server);
    }

    // Invokethe framework - this is an infinite loop and will not return
    gfserver_serve(&gfs);

    // line never reached
    return -1;

}