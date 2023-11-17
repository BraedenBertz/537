#ifndef http_request_h
#define http_request_h
struct http_request
{
    char *method;
    char *path;
    char *delay;
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
    pthread_mutex_t* levelLocks;
    int* numFilled;
    struct http_request*** levels;
    int q;
};

#endif
#ifndef SAFEQ_H
#define SAFEQ_H

extern pthread_mutex_t priorityLock[MAX_PRIORITY_LEVELS];


extern void print_pq(struct priority_queue* pq);

// Create a new priority queue
extern void create_queue(struct priority_queue*, int);
// When a new request comes in, you will insert it in priority order.
extern void add_work(struct priority_queue *pq, struct http_request *r, int priority);

// The worker threads will call a Blocking version of remove, where if there are no elements in the queue,
// they will block until an item is added. You can implement this using condition variables.
extern struct http_request *get_work(struct priority_queue *pq);
// The listener threads should call a Non-Blocking function to get the highest priority job. If there 
//are no elements on the queue, they will simply return and send an error message to the client.
extern struct http_request *get_work_nonblocking(struct priority_queue *pq);

#endif