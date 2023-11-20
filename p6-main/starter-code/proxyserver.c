#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "proxyserver.h"
#include "safequeue.h"

struct priority_queue pq;
pthread_mutex_t priorityLock[MAX_PRIORITY_LEVELS];


/*
 * Constants
 */
#define RESPONSE_BUFSIZE 10000

/*
 * Global configuration variables.
 * Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int num_listener;
int *listener_ports;
int num_workers;
char *fileserver_ipaddr;
int fileserver_port;
int max_queue_size;

void send_error_response(int client_fd, status_code_t err_code, char *err_msg) {
    http_start_response(client_fd, err_code);
    http_send_header(client_fd, "Content-Type", "text/html");
    http_end_headers(client_fd);
    char *buf = malloc(strlen(err_msg) + 2);
    sprintf(buf, "%s\n", err_msg);
    http_send_string(client_fd, buf);
    return;
}

/*
 * forward the client request to the fileserver and
 * forward the fileserver response to the client
 */
void serve_request(int client_fd) {
    // create a fileserver socket
    int fileserver_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (fileserver_fd == -1) {
        fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
        exit(errno);
    }

    // create the full fileserver address
    struct sockaddr_in fileserver_address;
    fileserver_address.sin_addr.s_addr = inet_addr(fileserver_ipaddr);
    fileserver_address.sin_family = AF_INET;
    fileserver_address.sin_port = htons(fileserver_port);

    // connect to the fileserver
    int connection_status = connect(fileserver_fd, (struct sockaddr *)&fileserver_address,
                                    sizeof(fileserver_address));
    if (connection_status < 0) {
        // failed to connect to the fileserver
        printf("Failed to connect to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");
        return;
    }

    // successfully connected to the file server
    char *buffer = (char *)malloc(RESPONSE_BUFSIZE * sizeof(char));
    //printf("B\n");
    // forward the client request to the fileserver
    int bytes_read = recv(client_fd, buffer, RESPONSE_BUFSIZE, 0);
    int ret = http_send_data(fileserver_fd, buffer, bytes_read);
    //printf("B2\n");
    if (ret < 0) {
        printf("Failed to send request to the file server\n");
        send_error_response(client_fd, BAD_GATEWAY, "Bad Gateway");

    } else {
        // forward the fileserver response to the client
        while (1) {
            int bytes_read = recv(fileserver_fd, buffer, RESPONSE_BUFSIZE - 1, 0);
            if (bytes_read <= 0) // fileserver_fd has been closed, break
                break;
            ret = http_send_data(client_fd, buffer, bytes_read);
            if (ret < 0) { // write failed, client_fd has been closed
                break;
            }
        }
    }
    // close the connection to the fileserver
    shutdown(fileserver_fd, SHUT_WR);
    close(fileserver_fd);

    // Free resources and exit
    free(buffer);
}

void*
workerThreadEntrance(void* fd) {
    while(1) {
        struct http_request* topPriority = get_work(&pq);
        if (topPriority->delay > 0) {
            sleep(topPriority->delay);
        }
        serve_request(topPriority->fd);
        shutdown(topPriority->fd, SHUT_WR);
        close(topPriority->fd);
    }
}

void 
createAndReturnQUEUE_EMPTY(int fd) {
    http_start_response(fd, QUEUE_EMPTY);
    http_end_headers(fd);
    http_send_string(fd, "Nothing to dequeue\n");
}

void
createAndReturnQUEUE_FULL(int fd) {
    http_start_response(fd, QUEUE_FULL);
    http_end_headers(fd);
    http_send_string(fd, "Nothing to dequeue\n");
}

void
createAndReturn200(struct http_request* r, int fd)
{
    http_start_response(fd, 200);
    http_end_headers(fd);
    http_send_string(fd, r->path);
    http_send_string(fd, "\n");
}

void *
thread_entrance(void *a)
{
    int* server_fd = (int*) a;
    // make the threads
    struct sockaddr_in client_address;
    size_t client_address_length = sizeof(client_address);
    int client_fd;
    while (1)
    {
        printf("ready to accept the next request\n");
        client_fd = accept(*server_fd,
                           (struct sockaddr *)&client_address,
                           (socklen_t *)&client_address_length);
        if (client_fd < 0)
        {
            perror("Error accepting socket");
            return NULL;
        }

        printf("Accepted connection from %s on port %d\n",
               inet_ntoa(client_address.sin_addr),
               client_address.sin_port);
        
        struct http_request* client_request;
        client_request = parse_client_request(client_fd);
        printf("\n\tParsed HTTP request:\n");
        printf("\tPath: '%s'\n", client_request->path);
        printf("\tPriority: '%d'\n", client_request->priority);
        printf("\tDelay: '%d'\n\n", client_request->delay);
        if(strcmp(client_request->path, GETJOBCMD) == 0) {
            struct http_request* topRequest = get_work_nonblocking(&pq);
            if(topRequest == NULL) {
                createAndReturnQUEUE_EMPTY(client_fd);
            } else {
                createAndReturn200(topRequest, client_fd);
            }
            shutdown(client_fd, SHUT_WR);
            close(client_fd);
            shutdown(topRequest->fd, SHUT_WR);
            close(topRequest->fd);
        } else {
            client_request->fd = client_fd;
            if(client_request->priority < 0) return NULL;
            int ret = add_work(&pq, client_request, client_request->priority);
            if(ret == -1){
                createAndReturnQUEUE_FULL(client_fd);
                shutdown(client_fd, SHUT_WR);
                close(client_fd);
            }
            // wakeup some worker threads, if they are sleeping
            else
                pthread_cond_broadcast(&workerCondVar);
        }
    }
    return NULL;
}

int server_fd;
/*
 * opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *server_fd) {

    pthread_t* listener_threads[num_listener];

    for (int i = 0; i < num_listener; i++)
    {
        // create a socket to listen
        *server_fd = socket(PF_INET, SOCK_STREAM, 0);
        if (*server_fd == -1)
        {
            perror("Failed to create a new socket");
            exit(errno);
        }

        // manipulate options for the socket
        int socket_option = 1;
        if (setsockopt(*server_fd, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                       sizeof(socket_option)) == -1)
        {
            perror("Failed to set socket options");
            exit(errno);
        }

        int proxy_port = listener_ports[i];
        // create the full address of this proxyserver
        struct sockaddr_in proxy_address;
        memset(&proxy_address, 0, sizeof(proxy_address));
        proxy_address.sin_family = AF_INET;
        proxy_address.sin_addr.s_addr = INADDR_ANY;
        proxy_address.sin_port = htons(proxy_port); // listening port

        // bind the socket to the address and port number specified in
        if (bind(*server_fd, (struct sockaddr *)&proxy_address,
                    sizeof(proxy_address)) == -1)
        {
            perror("Failed to bind on socket");
            exit(errno);
        }

        // starts waiting for the client to request a connection
        if (listen(*server_fd, 1024) == -1)
        {
            perror("Failed to listen on socket");
            exit(errno);
        }

        printf("Listening on port %d[%d]...\n", proxy_port, i);
        //create a thread for each listener
        pthread_t* p = (pthread_t*) malloc(sizeof(pthread_t));
        int *server_fd_copy = (int *)malloc(sizeof(int));
        *server_fd_copy = *server_fd;
        if (pthread_create(p, NULL, thread_entrance, (void *)(server_fd_copy)) != 0)
        {
            exit(-1);
        }
        listener_threads[i] = p;
    }

    void * null;
    pthread_join(*listener_threads[0], &null);
    shutdown(*server_fd, SHUT_RDWR);
    close(*server_fd);
}


/*
 * Default settings for in the global configuration variables
 */
void default_settings() {
    num_listener = 1;
    listener_ports = (int *)malloc(num_listener * sizeof(int));
    listener_ports[0] = 8000;

    num_workers = 1;

    fileserver_ipaddr = "127.0.0.1";
    fileserver_port = 3333;

    max_queue_size = 100;
}

void print_settings() {
    printf("\t---- Setting ----\n");
    printf("\t%d listeners [", num_listener);
    for (int i = 0; i < num_listener; i++)
        printf(" %d", listener_ports[i]);
    printf(" ]\n");
    printf("\t%d workers\n", num_workers);
    printf("\tfileserver ipaddr %s port %d\n", fileserver_ipaddr, fileserver_port);
    printf("\tmax queue size  %d\n", max_queue_size);
    printf("\t  ----\t----\t\n");
}

void signal_callback_handler(int signum) {
    printf("Caught signal %d: %s\n", signum, strsignal(signum));
    for (int i = 0; i < num_listener; i++) {
        if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
    }
    free(listener_ports);
    exit(0);
}

char *USAGE =
    "Usage: ./proxyserver [-l 1 8000] [-n 1] [-i 127.0.0.1 -p 3333] [-q 100]\n";

void exit_with_usage() {
    fprintf(stderr, "%s", USAGE);
    exit(EXIT_SUCCESS);
}

void create_worker_threads(int nw) {
    pthread_t *p; 
    for(int i = 0; i < nw; i++) {
        p = (pthread_t *)malloc(sizeof(pthread_t));
        if (pthread_create(p, NULL, workerThreadEntrance, (void *)(NULL)) != 0)
        {
            exit(-1);
        }
    }
}

int
main(int argc, char **argv)
{
    signal(SIGINT, signal_callback_handler);

    // initialize priotityLock, each lock must be initialized before use.
    for(int i = 0; i < MAX_PRIORITY_LEVELS; i++){
        pthread_mutex_init(&priorityLock[i], NULL);
    }

    /* Default settings */
    default_settings();

    int i;
    for (i = 1; i < argc; i++)
    {
        if (strcmp("-l", argv[i]) == 0)
        {
            num_listener = atoi(argv[++i]);
            free(listener_ports);
            listener_ports = (int *)malloc(num_listener * sizeof(int));
            for (int j = 0; j < num_listener; j++)
            {
                listener_ports[j] = atoi(argv[++i]);
            }

        }
        else if (strcmp("-w", argv[i]) == 0)
        {
            num_workers = atoi(argv[++i]);
        }
        else if (strcmp("-q", argv[i]) == 0)
        {
            max_queue_size = atoi(argv[++i]);
        }
        else if (strcmp("-i", argv[i]) == 0)
        {
            fileserver_ipaddr = argv[++i];
        }
        else if (strcmp("-p", argv[i]) == 0)
        {
            fileserver_port = atoi(argv[++i]);
        }
        else
        {
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            exit_with_usage();
        }
    }
    print_settings();
    create_queue(&pq, max_queue_size);
    create_worker_threads(num_workers);
    serve_forever(&server_fd);

    return EXIT_SUCCESS;
}
