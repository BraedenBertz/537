#ifndef SAFEQ_H
#define SAFEQ_H
#include <pthread.h>
#include "proxyserver.h"
#define MAX_STORAGE_FOR_REQUESTS 64
#define MAX_PRIORITY_LEVELS 16

struct http_request priority_queue[MAX_PRIORITY_LEVELS][MAX_STORAGE_FOR_REQUESTS];
// Create a new priority queue
extern void create_queue();
// When a new request comes in, you will insert it in priority order.
extern void add_work();
// The worker threads will call a Blocking version of remove, where if there are no elements in the queue, 
//they will block until an item is added. You can implement this using condition variables.
extern struct http_request get_work();
// The listener threads should call a Non-Blocking function to get the highest priority job. If there 
//are no elements on the queue, they will simply return and send an error message to the client.
extern struct http_request get_work_nonblocking();

#endif