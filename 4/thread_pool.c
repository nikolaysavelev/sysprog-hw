#include "thread_pool.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <math.h>

struct thread_task {
	thread_task_f function;
	void *arg;
	void *result;
	pthread_mutex_t mutex;
	pthread_cond_t completed;
	_Atomic int status;
};

struct thread_pool {
	pthread_t *threads;
	_Atomic int max_threads_count;
	_Atomic int threads_count;

	struct thread_task **tasks;
	_Atomic int tasks_count;
	_Atomic int tasks_in_progress_count;

	_Atomic bool is_deleted;

	pthread_mutex_t mutex;
	pthread_cond_t available;
};

int
thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
	/* IMPLEMENT THIS FUNCTION */
	if (max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS || pool == NULL) {
		return TPOOL_ERR_INVALID_ARGUMENT;
	}

	*pool = (struct thread_pool*)malloc(sizeof(struct thread_pool));
	if (*pool == NULL) {
		return TPOOL_ERR_NOT_IMPLEMENTED;
	}

	atomic_init(&(*pool)->max_threads_count, max_thread_count);

	(*pool)->threads = (pthread_t*)malloc(sizeof(pthread_t) * max_thread_count);
	if ((*pool)->threads == NULL) {
		free(*pool);
		return TPOOL_ERR_NOT_IMPLEMENTED;
	}

	(*pool)->tasks = (struct thread_task**)malloc(sizeof(struct thread_task) * TPOOL_MAX_TASKS);
	if ((*pool)->tasks == NULL) {
		free(*pool);
		return TPOOL_ERR_NOT_IMPLEMENTED;
	}

	atomic_init(&(*pool)->threads_count, 0);
	atomic_init(&(*pool)->tasks_count, 0);
	atomic_init(&(*pool)->tasks_in_progress_count, 0);
	atomic_init(&(*pool)->is_deleted, false);

	pthread_mutex_init(&(*pool)->mutex, NULL);
	pthread_cond_init(&(*pool)->available, NULL);

	return 0;
}

int
thread_pool_thread_count(const struct thread_pool *pool)
{
	/* IMPLEMENT THIS FUNCTION */
	return pool->threads_count;
}

int
thread_pool_delete(struct thread_pool *pool)
{
	/* IMPLEMENT THIS FUNCTION */
	if (!pool) {
		return TPOOL_ERR_INVALID_ARGUMENT;
	}

	if (pool->tasks_in_progress_count > 0 || pool->tasks_count > 0) {
		return TPOOL_ERR_HAS_TASKS;
	}

	atomic_store(&pool->is_deleted, true);
	pthread_cond_broadcast(&pool->available);

	for (int i = 0; i < pool->threads_count; ++i) {
		pthread_join(pool->threads[i], NULL);
	}

	pthread_cond_destroy(&pool->available);
	pthread_mutex_destroy(&pool->mutex);

	free(pool->tasks);
	free(pool->threads);
	free(pool);

	return 0;
}

void 
*pool_worker(void *tpool) {
	/* 
		MY FUNCTION. comments by author:
		This function represents the main task execution loop in each thread of the pool.
		Implemented on counters and mutex lock/unlock.
	*/
    struct thread_pool *pool = (struct thread_pool*) tpool;

    while (true) {
        pthread_mutex_lock(&pool->mutex);

        while (pool->tasks_count == 0 && !pool->is_deleted) {
            pthread_cond_wait(&pool->available, &pool->mutex);
        }

        if (pool->is_deleted) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        struct thread_task *task = NULL;
        if (pool->tasks_count > 0) {
            pool->tasks_count--;
            task = pool->tasks[pool->tasks_count];
            pool->tasks_in_progress_count++;
        }

        pthread_mutex_unlock(&pool->mutex);

        if (task != NULL) {
            pthread_mutex_lock(&task->mutex);
            if (task->status != TASK_DETACHED) {
                task->status = TASK_RUNNING;
            }
            pthread_mutex_unlock(&task->mutex);

            void *result = task->function(task->arg);

            pthread_mutex_lock(&task->mutex);
            task->result = result;
            pool->tasks_in_progress_count--;

            if (task->status == TASK_DETACHED) {
                task->status = TASK_JOINED;
                pthread_mutex_unlock(&task->mutex);
                thread_task_delete(task);
                continue;
            }

            task->status = TASK_FINISHED;
            pthread_mutex_unlock(&task->mutex);
            pthread_cond_signal(&task->completed);
        }
    }

    return NULL;
}

int 
thread_pool_push_task(struct thread_pool *pool, struct thread_task *task) 
{
	/* IMPLEMENT THIS FUNCTION */
    if (pool == NULL || task == NULL) {
        return TPOOL_ERR_INVALID_ARGUMENT;
    }

    if (pool->tasks_count >= TPOOL_MAX_TASKS) {
        return TPOOL_ERR_TOO_MANY_TASKS;
    }

    pthread_mutex_lock(&pool->mutex);

    pool->tasks[pool->tasks_count++] = task;
    atomic_store_explicit(&task->status, TASK_WAITING, memory_order_relaxed);

    if (pool->max_threads_count > pool->threads_count && pool->tasks_in_progress_count == pool->threads_count) {
        if (pthread_create(&(pool->threads[pool->threads_count++]), NULL, pool_worker, (void*) pool) != 0) {
            pthread_mutex_unlock(&pool->mutex);
        }
    }

    pthread_mutex_unlock(&pool->mutex);
    pthread_cond_signal(&pool->available);

    return 0;
}

int
thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	/* IMPLEMENT THIS FUNCTION */
	*task = malloc(sizeof(struct thread_task));
	(*task)->function = function;
	(*task)->status = TASK_CREATED;
	(*task)->arg = arg;

	pthread_mutex_init(&(*task)->mutex, NULL);
	pthread_cond_init(&(*task)->completed, NULL);

	return 0;
}

bool
thread_task_is_finished(const struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	return task->status == TASK_FINISHED;
}

bool
thread_task_is_running(const struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	return task->status == TASK_RUNNING;
}

int
thread_task_join(struct thread_task *task, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
	if (task->status == TASK_CREATED) {
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}

	pthread_mutex_lock(&task->mutex);

	while (!thread_task_is_finished(task)) {
		pthread_cond_wait(&task->completed, &task->mutex);
	}

	pthread_mutex_unlock(&task->mutex);

	*result = task->result;
	atomic_store(&task->status, TASK_JOINED);

	return 0;
}

#ifdef NEED_TIMED_JOIN

int
thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
    if (task->status == TASK_CREATED) {
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }

    pthread_mutex_lock(&task->mutex);

    if (timeout < 0.000000001) {
        if (task->status != TASK_FINISHED) {
            pthread_mutex_unlock(&task->mutex);
            return TPOOL_ERR_TIMEOUT;
        }
    } else {
        struct timespec abs_timeout;
        clock_gettime(CLOCK_REALTIME, &abs_timeout);
        abs_timeout.tv_sec += (time_t)timeout;
        abs_timeout.tv_nsec += (long)((timeout - (double)((long)timeout)) * 1e9);

        if (abs_timeout.tv_nsec >= 1000000000) {
            abs_timeout.tv_sec++;
            abs_timeout.tv_nsec -= 1000000000;
        }

        int condition = 0;
        while (task->status != TASK_FINISHED && condition != ETIMEDOUT) {
            condition = pthread_cond_timedwait(&task->completed, &task->mutex, &abs_timeout);
        }

        if (condition == ETIMEDOUT) {
            pthread_mutex_unlock(&task->mutex);
            return TPOOL_ERR_TIMEOUT;
        }
    }

    *result = task->result;
	atomic_store(&task->status, TASK_JOINED);
    pthread_mutex_unlock(&task->mutex);

    return 0;
}

#endif

int
thread_task_delete(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	if (task->status != TASK_CREATED && task->status != TASK_JOINED) {
        return TPOOL_ERR_TASK_IN_POOL;
    }

    pthread_cond_destroy(&task->completed);
    pthread_mutex_destroy(&task->mutex);

    free(task);
    task = NULL;

	return 0;
}

#ifdef NEED_DETACH

int
thread_task_detach(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	if (task->status == TASK_CREATED) {
        return TPOOL_ERR_TASK_NOT_PUSHED;
    }

    pthread_mutex_lock(&task->mutex);

    if (task->status == TASK_FINISHED) {
        task->status = TASK_JOINED;
        pthread_mutex_unlock(&task->mutex);
        thread_task_delete(task);

        return 0;
    }

    task->status = TASK_DETACHED;
    pthread_mutex_unlock(&task->mutex);

    return 0;
}

#endif
