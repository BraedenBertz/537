#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include "safequeue.h"

void print_pq(struct priority_queue *pq)
{
    for(int i = 0; i < pq->q; i++) {
        if(pq->numFilled[i] != 0) {
            printf("%d: %s\n", i, pq->levels[i][0].path);
        }
    }
}

void create_queue(struct priority_queue *pq, int q)
{
    printf("creating queue\n");
    pq->q = q;
    pq->levelLocks = (pthread_mutex_t*) malloc(q*sizeof(pthread_mutex_t));
    if (pq->levelLocks == NULL) {
        printf("levellocks");
        exit(-1);
    }
    pq->numFilled = (int *)calloc(q, sizeof(int));
    if (pq->numFilled == NULL)
    {
        printf("numFilled");
        exit(-1);
    }
    pq->levels = (struct http_request **)malloc(MAX_PRIORITY_LEVELS * sizeof(struct http_request *));
    if (pq->levels == NULL)
    {
        printf("levels");
        exit(-1);
    }
    for(int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        pq->levels[i] = (struct http_request *)malloc(q * sizeof(struct http_request));
        if (pq->levels[i] == NULL)
        {
            printf("levels[i]");
            exit(-1);
        }
    }
}

void add_work(struct priority_queue *pq, struct http_request* r, int priority)
{
    printf("adding to queue at level %d, the content: %s\n", priority, r->path);
    pq->levels[priority-1][0] = *r;
    pq->numFilled[priority-1]++;
    printf("added to queue successfully\n");
    //acquire the lock to the priority level, if we can't, then go ahead and do nothing
    //acquiring the lock is like so (if prioity is 1, then get locks[1]);
    //assuming we have the lock, go ahead and see if numfilled is equal to max size
    //if it is, then we cannot add this request
    //if its not, add to the appropriate level

    //release the lock
}

void get_work()
{
    //psuedocode
    //in a for loop, acquire a lock for the highest priority level
    //if we can't, return
    //assuming we have the lock, see if the numfilled > 0, if so, serve this request
    //get it out of the priority queue and decrement numfilled
    //release the lock and return

    //assuming nothing is filled, cond_wait the lock
    //must do a recheck starting at pseudocode again to see if the buffer is filled btw
}

void get_work_nonblocking()
{
    //very similar to get_work, except at the end, if we didn't return, then we just send an error message
}
