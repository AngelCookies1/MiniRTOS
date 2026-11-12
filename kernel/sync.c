#include "kernel.h"
#include "kernel_internal.h"
#include <stdint.h>

/* 二值信号量：优先级等待队列、直接交付、有限超时及永久等待。 */
/* sem_finish_wait() 统一清理等待队列和超时链表，必须在临界区调用。 */

static void sem_check_context(sem_t *sem)
{
  if(sem == NULL ||
     g_rtos_start == 0 ||
     port_get_ipsr() != 0 ||
     port_get_primask() != 0 ||
     g_current == NULL ||
     g_current == &task_idle)
    {
      kernel_panic();
    }
}

void sem_init(sem_t *sem, uint32_t initial_count)
{
  if(sem == NULL || initial_count > 1 || g_rtos_start != 0)
  {
    kernel_panic();
    return;
  }
  sem->count = initial_count;
  sem->wait_head = NULL;
}

static void sem_wait_insert(sem_t *sem, tcb_t *task)
{
  tcb_t **link = &sem->wait_head;
  while (*link != NULL && (*link)->prio >= task->prio)
  {
    link = &(*link)->wait_next;
  }
  task->wait_next = *link;
  *link = task;
}

static void sem_wait_remove(sem_t *sem, tcb_t *task)
{
  tcb_t **link = &sem->wait_head;

  while (*link != NULL && *link != task)
  {
    link = &(*link)->wait_next;
  }

  if(*link == NULL)
  {
    kernel_panic();
    return;
  }

  *link = task->wait_next;
  task->wait_next = NULL;
}

void sem_finish_wait(tcb_t *task, sem_result_t result)
{
  if(task->state != TASK_BLOCKED || task->waiting_sem == NULL)
  {
    kernel_panic();
    return;
  }
  sem_wait_remove(task->waiting_sem, task);
  if(task->timeout_active != 0)
  {
    delay_list_remove(task);
    task->timeout_active = 0;
  }
  task->waiting_sem = NULL;
  task->wait_result = result;
  task->state = TASK_READY;
  ready_add(task);
}

sem_result_t sem_take_timeout(sem_t *sem, uint32_t timeout_ticks)
{
  sem_check_context(sem);
  if(timeout_ticks != SEM_WAIT_FOREVER && timeout_ticks > (uint32_t)INT32_MAX)
  {
    kernel_panic();
    return SEM_TIMEOUT;
  }
  uint32_t irq_state = task_enter_critical();
  if(sem->count != 0)
  {
    sem->count = 0;
    task_exit_critical(irq_state);
    return SEM_OK;
  }
  if(timeout_ticks == 0)
  {
    task_exit_critical(irq_state);
    return SEM_TIMEOUT;
  }

  tcb_t *current = g_current;
  current->waiting_sem = sem;
  current->wait_result = SEM_TIMEOUT;
  current->timeout_active = 0;

  ready_remove(current);
  current->state = TASK_BLOCKED;
  sem_wait_insert(sem, current);
  if(timeout_ticks != SEM_WAIT_FOREVER)
  {
    current->wake_tick = port_get_tick() + timeout_ticks;
    current->timeout_active = 1;
    delay_list_insert(current);
  }

  task_yield();
  task_exit_critical(irq_state);

  return current->wait_result;
}

void sem_take(sem_t *sem)
{
  (void)sem_take_timeout(sem, SEM_WAIT_FOREVER);
}

void sem_give(sem_t *sem)
{
  sem_check_context(sem);
  uint32_t irq_state = task_enter_critical();
  if(sem->wait_head != NULL)
  {
    tcb_t *task = sem->wait_head;

    sem_finish_wait(task, SEM_OK);

    sem->count = 0;

    task_yield();
  }
  else
  {
    sem->count = 1;
  }
  task_exit_critical(irq_state);
}

void sem_give_from_isr(sem_t *sem)
{
  if(sem == NULL || port_get_ipsr() < 16 || g_rtos_start == 0)
  {
    kernel_panic();
    return;
  }
  uint32_t irq_state = task_enter_critical();
  if(sem->wait_head != NULL)
  {
    tcb_t *task = sem->wait_head;
    sem_finish_wait(task, SEM_OK);
    sem->count = 0;

    if(g_current == NULL || 
       g_current->state != TASK_READY || 
       task->prio >= g_current->prio)
       {
        task_yield();
       }
  }
  else
  {
    sem->count = 1;
  }
  task_exit_critical(irq_state);
}
