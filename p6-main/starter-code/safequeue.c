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
    for(int i = 0; i < MAX_PRIORITY_LEVELS; i++) {
        pthread_mutex_init(&pq->levelLocks[i], NULL);
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

int add_work(struct priority_queue *pq, struct http_request* r, int priority)
{
    if (pq == NULL)
        exit(-1);
    int fill = 0;
    for (int level = 0; level < MAX_PRIORITY_LEVELS; level++)
    {
        // assuming we have the lock, see if the numfilled > 0, if so, serve this request
        fill += pq->numFilled[level];
    }
    if(fill >= pq->q) return -1;
    printf("spinning in add_work: %d, %p \n", priority - 1, &priorityLock);
    pthread_mutex_lock(&priorityLock[priority-1]);
    int numberOfAlreadyPresentTasksAtLevel = pq->numFilled[priority - 1];
    pq->levels[priority - 1][numberOfAlreadyPresentTasksAtLevel] = r;
    pq->numFilled[priority-1]++;
    pthread_mutex_unlock(&priorityLock[priority - 1]);
    printf("unlocking in add_work\n");
    return 0;
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
            pthread_mutex_lock(&priorityLock[level]);
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
    
    for (int level = 0; level < MAX_PRIORITY_LEVELS; level++)
    {
        // acquire a lock for the highest priority level
        pthread_mutex_lock(&priorityLock[level]);
        // assuming we have the lock, see if the numfilled > 0, if so, serve this request
        if (pq->numFilled[level] != 0)
        {
            //  get it out of the priority queue and decrement numfilled
            struct http_request *ret = pq->levels[level][0];
            release_request_from_pq(pq, level);
            // release the lock and return
            pthread_mutex_unlock(&priorityLock[level]);
            printf("returning from level %d\n", level);
            return ret;
        }
        // unlock even if the level was clear
        pthread_mutex_unlock(&priorityLock[level]);
    }
    return NULL;
}
