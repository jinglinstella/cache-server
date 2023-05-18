// In case you want to implement the shared memory IPC as a library
//You may use this file
//

#ifndef SHM_CHANNEL_H
#define SHM_CHANNEL_H

#define maxnpending 10
#define BUFSIZE 4096



struct shm_t
{
    size_t size;
    int proxy_finish_read, cache_finish_write;

    int file_size;
    size_t read_size;

    pthread_mutex_t shm_mutex;
    pthread_cond_t shm_read_con;
    pthread_cond_t shm_write_con;
    char buff[2048];
} shm_t;



#endif

