#ifndef http_request_h
#define http_request_h
struct http_request
{
    char *method;
    char *path;
    int delay;
    int priority;
    pthread_cond_t listenerCondVar;
    pthread_mutex_t signalingLock;
    int fd;
};
#endif

#ifndef pq_h
#define pq_h
#define MAX_STORAGE_FOR_REQUESTS 64
#define MAX_PRIORITY_LEVELS 16
struct priority_queue
{
    pthread_mutex_t *levelLocks;
    int *numFilled;
    struct http_request ***levels;
    int q;
};
#endif

#ifndef PROXYSERVER_H
#define PROXYSERVER_H



typedef enum scode {
    OK = 200,           // ok
    BAD_REQUEST = 400,  // bad request
    BAD_GATEWAY = 502,  // bad gateway
    SERVER_ERROR = 500, // internal server error
    QUEUE_FULL = 599,   // priority queue is full
    QUEUE_EMPTY = 598   // priority queue is empty
} status_code_t;

#define GETJOBCMD "/GetJob"
extern pthread_cond_t workerCondVar;
extern struct priority_queue pq;

/*
 * Functions for sending an HTTP response.
 */
void http_start_response(int fd, int status_code);
void http_send_header(int fd, char *key, char *value);
void http_end_headers(int fd);
void http_send_string(int fd, char *data);
int http_send_data(int fd, char *data, size_t size);
char *http_get_response_message(int status_code);

void http_start_response(int fd, int status_code) {
    dprintf(fd, "HTTP/1.0 %d %s\r\n", status_code,
            http_get_response_message(status_code));
}

void http_send_header(int fd, char *key, char *value) {
    dprintf(fd, "%s: %s\r\n", key, value);
}

void http_end_headers(int fd) {
    dprintf(fd, "\r\n");
}

void http_send_string(int fd, char *data) {
    http_send_data(fd, data, strlen(data));
}

int http_send_data(int fd, char *data, size_t size) {
    ssize_t bytes_sent;
    while (size > 0) {
        bytes_sent = write(fd, data, size);
        if (bytes_sent < 0)
            return -1; // Indicates a failure
        size -= bytes_sent;
        data += bytes_sent;
    }
    return 0; // Indicate success
}

void http_fatal_error(char *message) {
    fprintf(stderr, "%s\n", message);
    exit(ENOBUFS);
}

#define LIBHTTP_REQUEST_MAX_SIZE 8192

struct http_request *http_request_parse(int fd) {
    struct http_request *request = malloc(sizeof(struct http_request));
    if (!request) http_fatal_error("Malloc failed");

    char *read_buffer = malloc(LIBHTTP_REQUEST_MAX_SIZE + 1);
    if (!read_buffer) http_fatal_error("Malloc failed");
    int bytes_read = recv(fd, read_buffer, LIBHTTP_REQUEST_MAX_SIZE, MSG_PEEK);
    read_buffer[bytes_read] = '\0'; /* Always null-terminate. */

    char *read_start, *read_end;
    size_t read_size;

    do {
        /* Read in the HTTP method: "[A-Z]*" */
        read_start = read_end = read_buffer;
        while (*read_end >= 'A' && *read_end <= 'Z') {
            printf("%c", *read_end);
            read_end++;
        }
        read_size = read_end - read_start;
        if (read_size == 0) break;
        request->method = malloc(read_size + 1);
        memcpy(request->method, read_start, read_size);
        request->method[read_size] = '\0';
        printf("parsed method %s\n", request->method);

        /* Read in a space character. */
        read_start = read_end;
        if (*read_end != ' ') break;
        read_end++;

        /* Read in the path: "[^ \n]*" */
        read_start = read_end;
        while (*read_end != '\0' && *read_end != ' ' && *read_end != '\n')
            read_end++;
        read_size = read_end - read_start;
        if (read_size == 0) break;
        request->path = malloc(read_size + 1);
        memcpy(request->path, read_start, read_size);
        request->path[read_size] = '\0';
        printf("parsed path %s\n", request->path);

        /* Read in HTTP version and rest of request line: ".*" */
        read_start = read_end;
        while (*read_end != '\0' && *read_end != '\n')
            read_end++;
        if (*read_end != '\n') break;
        read_end++;

        free(read_buffer);
        return request;
    } while (0);

    /* An error occurred. */
    free(request);
    free(read_buffer);
    return NULL;
}

struct http_request* parse_client_request(int fd)
{
    struct http_request *return_value = malloc(sizeof(struct http_request));
    char *read_buffer = malloc(LIBHTTP_REQUEST_MAX_SIZE + 1);
    if (!read_buffer)
        http_fatal_error("Malloc failed");

    int bytes_read = recv(fd, read_buffer, LIBHTTP_REQUEST_MAX_SIZE, MSG_PEEK);
    read_buffer[bytes_read] = '\0'; /* Always null-terminate. */

    int delay = -1;
    int priority = -1;
    char *path = NULL;

    int is_first = 1;
    size_t size;

    char *token = strtok(read_buffer, "\r\n");
    while (token != NULL)
    {
        size = strlen(token);
        if (is_first)
        {
            is_first = 0;
            // get path
            char *s1 = strstr(token, " ");
            char *s2 = strstr(s1 + 1, " ");
            size = s2 - s1 - 1;
            path = strndup(s1 + 1, size);

            if (strcmp(GETJOBCMD, path) == 0)
            {
                break;
            }
            else
            {
                // get priority
                s1 = strstr(path, "/");
                s2 = strstr(s1 + 1, "/");
                size = s2 - s1 - 1;
                char *p = strndup(s1 + 1, size);
                priority = atoi(p);
            }
        }
        else
        {
            char *value = strstr(token, ":");
            if (value)
            {
                size = value - token - 1; // -1 for space
                if (strncmp("Delay", token, size) == 0)
                {
                    delay = atoi(value + 2); // skip `: `
                }
            }
        }
        token = strtok(NULL, "\r\n");
    }
    return_value->path = path;
    return_value->delay = delay;
    return_value->priority = priority;
    free(read_buffer);
    return return_value;
}

char *http_get_response_message(int status_code) {
    switch (status_code) {
    case 100:
        return "Continue";
    case 200:
        return "OK";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    default:
        return "Internal Server Error";
    }
}
#endif
