#ifndef POCKETJS_TEST_RTTHREAD_H
#define POCKETJS_TEST_RTTHREAD_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t rt_tick_t;
typedef int rt_bool_t;
typedef int rt_err_t;
typedef size_t rt_size_t;
typedef struct test_sem *rt_sem_t;
#define RT_NULL NULL
#define RT_TRUE 1
#define RT_FALSE 0
#define RT_EOK 0
#define RT_IPC_FLAG_FIFO 0
#define rt_memset memset
void *rt_malloc(rt_size_t size);
void rt_free(void *pointer);
void rt_kprintf(const char *format, ...);
rt_sem_t rt_sem_create(const char *name, unsigned value, unsigned flag);
rt_err_t rt_sem_release(rt_sem_t sem);
rt_err_t rt_sem_trytake(rt_sem_t sem);
rt_err_t rt_sem_delete(rt_sem_t sem);
#endif
