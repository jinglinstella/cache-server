#include "gfserver.h"
#include "cache-student.h"
#include "shm_channel.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

//#define BUFSIZE (822)

/*
 * Replace with your implementation
*/

pthread_mutex_t qmutex = PTHREAD_MUTEX_INITIALIZER;  //mutex and cd for accessing steque of nameid and shm_t pointer
pthread_cond_t qempty = PTHREAD_COND_INITIALIZER;

steque_t *qname_id;
steque_t *qshm_ptr;
size_t seg_size;


struct shm_t shm_t;

int create_proxy_sock(void){

    struct addrinfo hints, *server_info, *p;;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    getaddrinfo("localhost", "8880", &hints, &server_info);


    int proxy_socket;

    for (p = server_info; p != NULL; p = p->ai_next){

        proxy_socket = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if(proxy_socket < 0)
            continue;
        if(connect(proxy_socket, p->ai_addr, p->ai_addrlen) == -1){
            close(proxy_socket);
            continue;
        }
        break;
    }
    freeaddrinfo(server_info);

    return proxy_socket;
}


ssize_t handle_with_cache(gfcontext_t *ctx, char *path, void* arg)
{
    ssize_t bytes_sent;
    char send_cache[2048];
    char *proxy_recv;
    char *name_id;

    //seg_size declared in the beginning
    sprintf(send_cache, "%zu ", seg_size);

    pthread_mutex_lock(&qmutex);
    while(steque_isempty(qname_id)){
        pthread_cond_wait(&qempty, &qmutex);
    }

    name_id = steque_pop(qname_id);
    struct shm_t *group = steque_pop(qshm_ptr);
    pthread_mutex_unlock(&qmutex);


    //initially header information from webproxy to cache: segmentsize nameID path
    //will send send_cache to cache
    strcat(send_cache, name_id);
    strcat(send_cache, " ");
    strcat(send_cache, path);

    int sockfd = create_proxy_sock();


    ssize_t byte_sent;
    ssize_t total_byte_sent = 0;
    while(total_byte_sent < strlen(send_cache))
    {
        byte_sent = send(sockfd, send_cache + total_byte_sent, strlen(send_cache), 0);

        total_byte_sent = total_byte_sent + byte_sent;
    }


    char buffer[1024];

    //receive header message from cache
    ssize_t header_received = recv(sockfd, buffer, 1024, 0);
    buffer[header_received] = '\0';
    char* message = malloc(strlen(buffer) + 1);
    strcpy(message, buffer);
    proxy_recv = message;


    //webproxy will receive "error" (file not exist, stop) or file length (contiune to transfer)
    if(strcmp(proxy_recv, "invalid") == 0)
    {
        //recovery_shared_objects(name_id, group);
        pthread_mutex_lock(&qmutex);

        steque_enqueue(qshm_ptr, group);

        //put the name_id and shm_t pointer back to the queue for reuse
        steque_enqueue(qname_id, name_id);

        pthread_mutex_unlock(&qmutex);
        pthread_cond_signal(&qempty);

        free(proxy_recv);
        return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
    }

    //ssize_t file_size;
    ssize_t file_size = atoi(proxy_recv);
    printf("file len informed by simplecache: %d\n", (int)file_size);
    gfs_sendheader(ctx, GF_OK, file_size);

    bytes_sent = 0;
    int data_sent;

    //printf("begin recv for %s\n", name_id);
    while(bytes_sent < group->file_size) //keeping transferring until the transfered bytes = file length
    {
        pthread_mutex_lock(&group->shm_mutex);
        while(group->cache_finish_write == 0)  //wait until writing finish
            //when receive shm_read_con signal, go back to check cache_finish_write
            //cache_finish_write should equal 1
            pthread_cond_wait(&group->shm_read_con, &group->shm_mutex);

        //read_size is the amount of data that pread by cache
        data_sent = gfs_send(ctx, group->buff, group->read_size);
        bytes_sent = bytes_sent + data_sent;
        bzero(group->buff, sizeof(group->buff));
        group->proxy_finish_read = 1;  //reading finish, ready to write
        group->cache_finish_write = 0;
        pthread_mutex_unlock(&group->shm_mutex);
        pthread_cond_signal(&group->shm_write_con);
    }

    printf("webproxy done transfer for %s and %s, total bytes sent to gfclient: %d\n", name_id, path, (int)bytes_sent);

    pthread_mutex_lock(&qmutex);

    //put the name_id and shm_t pointer back to the queue for reuse
    steque_enqueue(qshm_ptr, group);

    steque_enqueue(qname_id, name_id);

    pthread_mutex_unlock(&qmutex);
    pthread_cond_signal(&qempty);

    free(proxy_recv);

    return bytes_sent;
}