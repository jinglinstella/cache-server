#include <stdio.h>
#include <unistd.h>
#include <printf.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/mman.h>


#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>



#include "cache-student.h"
#include "shm_channel.h"
#include "simplecache.h"
#include "gfserver.h"
#include "steque.h"

// CACHE_FAILURE
#if !defined(CACHE_FAILURE)
#define CACHE_FAILURE (-1)
#endif

#define MAX_CACHE_REQUEST_LEN 6200
#define MAX_SIMPLE_CACHE_QUEUE_SIZE 822

unsigned long int cache_delay;

static void _sig_handler(int signo){
    if (signo == SIGTERM || signo == SIGINT){
        /*you should do IPC cleanup here*/
        exit(signo);
    }
}

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  simplecached [options]\n"                                                  \
"options:\n"                                                                  \
"  -c [cachedir]       Path to static files (Default: ./)\n"                  \
"  -t [thread_count]   Thread count for work socket_queue (Default is 42, Range is 1-235711)\n"      \
"  -d [delay]          Delay in simplecache_get (Default is 0, Range is 0-2500000 (microseconds)\n "	\
"  -h                  Show this help message\n"

//OPTIONS
static struct option gLongOptions[] = {
        {"cachedir",           required_argument,      NULL,           'c'},
        {"nthreads",           required_argument,      NULL,           't'},
        {"help",               no_argument,            NULL,           'h'},
        {"hidden",			 no_argument,			 NULL,			 'i'}, /* server side */
        {"delay", 			 required_argument,		 NULL, 			 'd'}, // delay.
        {NULL,                 0,                      NULL,             0}
};

void Usage() {
    fprintf(stdout, "%s", USAGE);
}


struct shm_t;

steque_t* socket_queue;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; //to protect the socket_queue that takes in client socket
pthread_cond_t queue_empty = PTHREAD_COND_INITIALIZER; //condition to pop out of the socket_queue


int create_cache_sock(){
    int portno = 8880;

    int cache_sock = socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;

    setsockopt(cache_sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));


    struct sockaddr_in servAddr;
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servAddr.sin_port = htons(portno);

    if(bind(cache_sock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0){
        close(cache_sock);
    }

    return cache_sock;
}

char* cache_socket_recv(int sockfd){
    char buffer[BUFSIZE];

    ssize_t byte_received = 1;

    byte_received = recv(sockfd, buffer, BUFSIZE - 1, 0);

    buffer[byte_received] = '\0';

    char* message = malloc(strlen(buffer) + 1);
    strcpy(message, buffer);
    return message;
}

int fileSize(int fd) {
    struct stat s;
    if (fstat(fd, &s) == -1) {
        return -1;
    }
    return s.st_size;
}

void *worker_process(void* arg){

    while(1){

        pthread_mutex_lock(&mutex);

        while(steque_isempty(socket_queue))
            pthread_cond_wait(&queue_empty, &mutex);

        //pop the client socket out of the socket_queue
        int *proxy_sock = steque_pop(socket_queue);
        pthread_mutex_unlock(&mutex);

        char *cache_ctx, *size, *name_id, *path; //prepare to parse the header
        //size_t segment_size;


        cache_ctx = cache_socket_recv(*proxy_sock);
        printf("msg from webproxy: %s\n", cache_ctx);


        size = strtok(cache_ctx, " ");
        name_id = strtok(NULL, " ");
        path = strtok(NULL, " ");



        size_t segment_size = atoi(size);

        //get file descriptor based on the parsed path(the file path sent by webproxy)
        int fd = simplecache_get(path);

        char invalid[10] = "invalid";
        if(fd == -1){
            printf("path not found: %s\n", path);
            send(*proxy_sock, invalid, strlen(invalid), 0);
            close(*proxy_sock);
            free(proxy_sock);

            free(cache_ctx);

            continue;
        }


        //if file exists, open the shared mm created by the webproxy
        int shm_fd = shm_open(name_id, O_RDWR, 0666);

        //attach to the mm, return the ptr into the virtual address space mapped to the physical address
        void* ptr = mmap(NULL, sizeof(shm_t) + segment_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);

        close(shm_fd);



        struct shm_t* group = ptr;

        //get actual file length
        //size_t request_file_size = lseek(fd, 0, SEEK_END);
        //lseek(fd, 0, SEEK_SET);
        int request_file_size = fileSize(fd);
        char file_size_char[128];

        sprintf(file_size_char, "%u", request_file_size);


        //request file exists, send back the file length
        send(*proxy_sock, file_size_char, strlen(file_size_char), 0);
        printf("simplecache sent requested file len to webproxy as header: %s\n", file_size_char);

        group->file_size = request_file_size;
        //size_t bytes_sent = 0;
        int bytes_sent = 0;
        //int read_size;

        while(bytes_sent < request_file_size)
        {
            pthread_mutex_lock(&group->shm_mutex);
            while(group->proxy_finish_read == 0)  //waiting for read to be done
                pthread_cond_wait(&group->shm_write_con, &group->shm_mutex);

            //read the data into group->buff
            //since group points to virtual address that mapped to shared mm, data is in shared mm

            int read_size = pread(fd, group->buff, group->size - 1, bytes_sent);

            group->read_size = read_size;

            //simplecached finished writing to cache, can't write now, webproxy ready to read
            group->cache_finish_write = 1;
            group->proxy_finish_read = 0;
            pthread_mutex_unlock(&group->shm_mutex);

            //signal webproxy to start reading the data
            pthread_cond_signal(&group->shm_read_con);
            bytes_sent = bytes_sent + read_size;
        }
        printf("simplecache transferred total %d bytes\n", (int) bytes_sent);

        close(*proxy_sock);
        free(proxy_sock);
        free(cache_ctx);

        munmap(ptr, sizeof(shm_t) + segment_size);
    }
}

int main(int argc, char **argv) {
    int nthreads = 11;
    int i;
    char *cachedir = "locals.txt";
    //char *cachedir = "locals-ipcstress.txt";
    char option_char;

    /* disable buffering to stdout */
    setbuf(stdout, NULL);

    while ((option_char = getopt_long(argc, argv, "d:ic:hlt:x", gLongOptions, NULL)) != -1) {
        switch (option_char) {
            default:
                Usage();
                exit(1);
            case 't': // thread-count
                nthreads = atoi(optarg);
                break;
            case 'h': // help
                Usage();
                exit(0);
                break;
            case 'c': //cache directory
                cachedir = optarg;
                break;
            case 'd':
                cache_delay = (unsigned long int) atoi(optarg);
                break;
            case 'i': // server side usage
            case 'o': // do not modify
            case 'a': // experimental
                break;
        }
    }

    if (cache_delay > 2500001) {
        fprintf(stderr, "Cache delay must be less than 2500001 (us)\n");
        exit(__LINE__);
    }

    if ((nthreads>211804) || (nthreads < 1)) {
        fprintf(stderr, "Invalid number of threads must be in between 1-211804\n");
        exit(__LINE__);
    }
    if (SIG_ERR == signal(SIGINT, _sig_handler)){
        fprintf(stderr,"Unable to catch SIGINT...exiting.\n");
        exit(CACHE_FAILURE);
    }
    if (SIG_ERR == signal(SIGTERM, _sig_handler)){
        fprintf(stderr,"Unable to catch SIGTERM...exiting.\n");
        exit(CACHE_FAILURE);
    }
    /*Initialize cache*/

    socket_queue = (steque_t*)malloc(sizeof(*socket_queue));
    steque_init(socket_queue);

    simplecache_init(cachedir);

    // Cache should go here

    pthread_t workers[nthreads];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);


    for(i = 0; i < nthreads; i++)
        pthread_create(&workers[i], &attr, worker_process, NULL);

    int cache_sock = create_cache_sock();

    listen(cache_sock, maxnpending);

    while(1)
    {
        //connect with webproxy socket
        struct sockaddr their_addr;
        socklen_t sin_size = sizeof(their_addr);
        int *sockfd =(int*)malloc(sizeof(int));
        *sockfd = accept(cache_sock, &their_addr, &sin_size);

        pthread_mutex_lock(&mutex);

        //put the socket into socket_queue
        steque_enqueue(socket_queue, sockfd);

        pthread_mutex_unlock(&mutex);

        //signal the worker_process that they can start to pop
        pthread_cond_signal(&queue_empty);

    }

    simplecache_destroy();
    exit(0);



}

