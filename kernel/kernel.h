#ifndef MINIRTOS_KERNEL_H
#define MINIRTOS_KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"

/* 永久等待不登记超时；有限延时必须 <= INT32_MAX tick。 */
#define SEM_WAIT_FOREVER UINT32_MAX

/* TCB 暴露用于静态分配；应用只分配对象，不直接修改链表字段。 */
typedef enum
{
  TASK_READY,
  TASK_DELAYED,
  TASK_BLOCKED,
} task_state_t;

typedef enum
{
  SEM_OK,
  SEM_TIMEOUT,
} sem_result_t;

typedef struct sem sem_t;

typedef struct tcb{
  uint32_t *sp; /* 栈指针，切换时保存/恢复这个 */
  uint32_t prio; /* 优先级 */
  volatile task_state_t state; /* 状态 */
  volatile uint32_t wake_tick; /* 延时计数 */

  struct tcb *delay_next;

  struct tcb *ready_next;
  struct tcb *ready_prev;
  uint8_t ready_queued; //就绪队列状态标志
  struct tcb *wait_next;
  sem_t *waiting_sem;
  volatile sem_result_t wait_result;
  uint8_t timeout_active; //超时状态标志
} tcb_t;

struct sem
{
  uint32_t count;   /* 只能为 0 或 1 */
  tcb_t *wait_head; /* 等待任务链表 */
};

/* 调度启动前调用：初始化任务并加入就绪队列。栈顶须 8 字节对齐。 */
void task_init(tcb_t *task, uint32_t *stack_top,
               void (*entry)(void *), void *arg, uint32_t prio);

/* 由 main 调用一次，中断必须开启；内部创建 idle 并启动最高优先级任务。 */
__attribute__((noreturn)) void rtos_start(void);
void task_yield(void);

/* 仅普通任务可阻塞，调用时不能持有外层临界区。 */
void task_delay(uint32_t ticks);

/* SysTick 在 HAL_IncTick() 之后调用。 */
void rtos_tick(void);

/* 初始化在启动前完成；take/give 目前仅供任务调用。 */
void sem_init(sem_t *sem, uint32_t initial_count);
void sem_take(sem_t *sem);
sem_result_t sem_take_timeout(sem_t *sem, uint32_t timeout_ticks);
void sem_give(sem_t *sem);
void sem_give_from_isr(sem_t *sem);

#endif
