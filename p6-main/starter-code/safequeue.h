#ifndef SAFEQ_H
#define SAFEQ_H

#define MAX_STORAGE_FOR_REQUESTS 64
#define MAX_PRIORITY_LEVELS 16

void* priority_queue[MAX_PRIORITY_LEVELS][MAX_STORAGE_FOR_REQUESTS];
// Create a new priority queue
extern void create_queue();
// When a new request comes in, you will insert it in priority order.
extern void add_work();
// The worker threads will call a Blocking version of remove, where if there are no elements in the queue, 
//they will block until an item is added. You can implement this using condition variables.
extern void get_work();
// The listener threads should call a Non-Blocking function to get the highest priority job. If there 
//are no elements on the queue, they will simply return and send an error message to the client.
extern void get_work_nonblocking();

#endif