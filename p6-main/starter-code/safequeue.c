#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include "safequeue.h"
pthread_cond_t workerCondVar;
void print_pq(struct priority_queue *pq)
{
    for(int i = 0; i < pq->q; i++) {
        if(pq->numFilled[i] == 0) {
            continue;
        }
        for(int j = 0; j < pq->numFilled[i]; j++) {
            printf("%d: %s\n", i, pq->levels[i][j]->path);
        }
    }
}

void create_queue(struct priority_queue *pq, int q)
{
    printf("creating queue\n");
    pq->q = q;
    pq->levelLocks = (pthread_mutex_t *)malloc(MAX_PRIORITY_LEVELS * sizeof(pthread_mutex_t));
    if (pq->levelLocks == NULL) {
        printf("levellocks");
        exit(-1);
    }
    pq->numFilled = (int *)calloc(MAX_PRIORITY_LEVELS, sizeof(int));
    if (pq->numFilled == NULL)
    {
        printf("numFilled");
        exit(-1);
    }
    pq->levels = (struct http_request ***)malloc(MAX_PRIORITY_LEVELS * sizeof(struct http_request **));
    if (pq->levels == NULL)
    {
        printf("levels");
        exit(-1);
    }
    for(int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        pq->levels[i] = (struct http_request **)malloc(q * sizeof(struct http_request*));
        if (pq->levels[i] == NULL)
        {
            printf("levels[i]");
            exit(-1);
        }
    }
}

void add_work(struct priority_queue *pq, struct http_request* r, int priority)
{
    // acquire the lock to the priority level, if we can't, then go ahead and do nothing
    // acquiring the lock is like so (if prioity is 1, then get locks[1]);
    if(pq == NULL) return;
    // assuming we have the lock, go ahead and see if numfilled is equal to max size
    if(pq->numFilled[priority-1] == pq->q) {
        // if it is, then we cannot add this request
        return;
    }
    // if its not, add to the appropriate level
    int numberOfAlreadyPresentTasksAtLevel = pq->numFilled[priority - 1];
    //int l = numberOfAlreadyPresentTasksAtLevel;
    // printf("adding to queue at level %d[filled = %d], the content: %s\n", priority-1, l, r->path);
    // printf("pq->levels[%d][%d]\n", priority - 1, numberOfAlreadyPresentTasksAtLevel);
    // printf("%p", pq);
    // printf(", %p", pq->levels);
    // printf(", %p\n", pq->levels[0]);
    pq->levels[priority - 1][numberOfAlreadyPresentTasksAtLevel] = r;
    //printf("pq->numFilled[%d]++;\n", priority - 1);
    pq->numFilled[priority-1]++;
    //printf("added to queue successfully\n");
    // release the lock
    
}

void release_request_from_pq(struct priority_queue* pq, int i) {
    if(pq->numFilled[i] <= 0) return;
    for (int j = 0; j < pq->numFilled[i]; j++)
    {
        pq->levels[i][j] = pq->levels[i][j + 1];
    }
    pq->numFilled[i]--;
    // if numfill = 1, then we have
    // b4: [*][null][null]...
    // after: [null][null][null]...
    // if numfill = q then we have
    // b4: [*][*]...[*][*]
    // after: [*][*]...[*][null]
    pq->levels[i][pq->numFilled[i]] = NULL;
}

struct http_request* get_work(struct priority_queue* pq)
{
    while(1) {
        for (int level = 0; level < MAX_PRIORITY_LEVELS; level++)
        {
            // acquire a lock for the highest priority level
            if(pthread_mutex_lock(&priorityLock[level]) == 0){
                // assuming we have the lock, see if the numfilled > 0, if so, serve this request
                if (pq->numFilled[level] != 0)
                {
                    //printf("A\n");
                    // get it out of the priority queue and decrement numfilled
                    struct http_request* ret = pq->levels[level][0];
                    release_request_from_pq(pq, level); 
                    // release the lock and return
                    pthread_mutex_unlock(&priorityLock[level]);
                    printf("returning from level %d\n", level);
                    return ret;
                }
                // unlock even if the level was clear
                pthread_mutex_unlock(&priorityLock[level]);
            }

            // assuming we have the lock, see if the numfilled > 0, if so, serve this request
           
        }
        pthread_mutex_t lock;
        pthread_mutex_init(&lock, NULL);
        pthread_mutex_lock(&lock);
        printf("couldn't find anything, going to sleep\n");
        pthread_cond_wait(&workerCondVar, &lock);
        pthread_mutex_unlock(&lock);
    }
}

struct http_request* get_work_nonblocking(struct priority_queue *pq)
{
    // very similar to get_work, except at the end, if we didn't return, then we just send an error message
    // psuedocode
    // in a for loop,
    for (int level = 0; level < MAX_PRIORITY_LEVELS; level++)
    {
        // acquire a lock for the highest priority level
        // assuming we have the lock, see if the numfilled > 0, if so, serve this request
        if (pq->numFilled[level] != 0)
        {
            // get it out of the priority queue and decrement numfilled
            struct http_request* ret = pq->levels[level][0];
            release_request_from_pq(pq, level);
            // release the lock and return
            return ret;
        }
    }
    // assuming nothing is filled, cond_wait the lock
    // must do a recheck starting at pseudocode again to see if the buffer is filled btw
    return NULL;
}
