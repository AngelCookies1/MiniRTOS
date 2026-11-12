#include "kernel_internal.h"

/* 每个优先级一条双向循环就绪链表，延时链表按截止时间排序。 */
static tcb_t *ready_head[MAX_PRIORITY];
static uint32_t ready_bitmap;
static tcb_t *g_delay_head;

void ready_add(tcb_t *task)
{
  uint32_t p = task->prio;
  if(p >= MAX_PRIORITY || task->ready_queued)
  {
    return;
  }
  tcb_t *head = ready_head[p];
  if(head == NULL)
  {
    task->ready_next = task;
    task->ready_prev = task;
    ready_head[p] = task;
    ready_bitmap |= (1u << p);
  }
  else
  {
    tcb_t *tail = head->ready_prev;

    task->ready_next = head;
    task->ready_prev = tail;

    tail->ready_next = task;
    head->ready_prev = task;
  }
  task->ready_queued = 1;
}

void ready_remove(tcb_t *task)
{
  if(task == NULL || !task->ready_queued)
  {
    return;
  }
  uint32_t p = task->prio;
  if(task->ready_next == task)
  {
    ready_head[p] = NULL;
    ready_bitmap &=~ (1u << p);
  }
  else
  {
    if(ready_head[p] == task)
    {
      ready_head[p] = task->ready_next;
    }

    task->ready_prev->ready_next = task->ready_next;
    task->ready_next->ready_prev = task->ready_prev;
  }

  task->ready_next = NULL;
  task->ready_prev = NULL;
  task->ready_queued = 0;
}

void ready_rotate(uint32_t priority)
{
  tcb_t *head = ready_head[priority];
  if(head != NULL && head->ready_next != head)
  {
    ready_head[priority] = head->ready_next;
  }
}

int ready_highest_priority(void)
{
  for(int p = MAX_PRIORITY - 1; p >= 0; p--)
  {
    if(ready_bitmap & (1u << p))
    {
      return p;
    }
  }
  return -1;
}

void delay_list_insert(tcb_t *task)
{
  tcb_t **link = &g_delay_head;
  task->delay_next = NULL;
  while(*link != NULL && (int32_t)((*link)->wake_tick - task->wake_tick) <= 0)
  {
    link = &(*link)->delay_next;
  }
  task->delay_next = *link;
  *link = task;
}

void delay_list_remove(tcb_t *task)
{
  tcb_t **link = &g_delay_head;
  while (*link != NULL && *link != task)
  {
    link = &(*link)->delay_next;
  }

  if(*link == NULL)
  {
    kernel_panic();
    return;
  }
  *link = task->delay_next;
  task->delay_next = NULL;
}

void delay_list_wake(uint32_t now)
{
  while(g_delay_head != NULL && (int32_t)(now - g_delay_head->wake_tick)>= 0)
  {
    tcb_t *task = g_delay_head;
    if(task->state == TASK_DELAYED)
    {
      g_delay_head = task->delay_next;
      task->delay_next = NULL;
      task->state = TASK_READY;
      ready_add(task);
    }
    else if (task->state == TASK_BLOCKED &&
             task->waiting_sem != NULL &&
             task->timeout_active != 0)
    {
      sem_finish_wait(task, SEM_TIMEOUT);
    }
    else
    {
      kernel_panic();
      return;
    }
  }
}

tcb_t *ready_first(uint32_t priority)
{
  return ready_head[priority];
}
