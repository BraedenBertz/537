#ifndef http_request_h
#define http_request_h
struct http_request
{
    char *method;
    char *path;
    char *delay;
};
#endif

#ifndef pq_h
#define pq_h
#define MAX_STORAGE_FOR_REQUESTS 64
#define MAX_PRIORITY_LEVELS 16
struct priority_queue
{
    pthread_mutex_t* levelLocks;
    int* numFilled;
    struct http_request** levels;
};
#endif
#ifndef SAFEQ_H
#define SAFEQ_H
// Create a new priority queue
extern void create_queue(struct priority_queue*, int);
// When a new request comes in, you will insert it in priority order.
extern void add_work();
// The worker threads will call a Blocking version of remove, where if there are no elements in the queue, 
//they will block until an item is added. You can implement this using condition variables.
extern void get_work();
// The listener threads should call a Non-Blocking function to get the highest priority job. If there 
//are no elements on the queue, they will simply return and send an error message to the client.
extern void get_work_nonblocking();

#endif