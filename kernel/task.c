#include "kernel_internal.h"

/* sp 必须位于首字段，port_asm.s 按偏移 0 保存 PSP。 */
_Static_assert(offsetof(tcb_t, sp) == 0, "TCB.sp must be the first member");

tcb_t * volatile g_current;
volatile uint32_t g_rtos_start;
tcb_t task_idle;
static uint32_t idle_stack[IDLE_STACK_WORDS] __attribute__((aligned(8)));

static tcb_t *pick_next(void);

static void idle_entry(void *arg)
{
    (void)arg;
    while (1)
    {
        port_idle();        /* M3 真的停机，等下一个中断 */
        task_yield();   /* 醒了看看有没有活儿 */
    }
}

void task_init(tcb_t *t, uint32_t *stack_top, 
              void (*entry)(void *), void *arg, uint32_t prio)
{
  if (g_rtos_start != 0U || t == NULL || entry == NULL ||
      stack_top == NULL || prio >= MAX_PRIORITY)
  {
    kernel_panic();
  }

  t->sp = port_stack_init(stack_top, entry, arg);
  t->prio = prio;
  t->state = TASK_READY;
  t->wake_tick = 0;
  t->delay_next = NULL;
  t->ready_prev = NULL;
  t->ready_next = NULL;
  t->ready_queued = 0;
  t->wait_next = NULL;
  t->waiting_sem = NULL;
  t->wait_result = SEM_TIMEOUT;
  t->timeout_active = 0;

  /* 初始化即注册，应用无需接触内核就绪队列。 */
  ready_add(t);
}

void rtos_start(void)
{
  if (g_rtos_start != 0U || port_get_ipsr() != 0U ||
      port_get_primask() != 0U)
  {
    kernel_panic();
  }

  task_init(&task_idle, &idle_stack[IDLE_STACK_WORDS],
            idle_entry, NULL, 0U);

  g_current = NULL;
  g_current = pick_next();
  port_start_first_task();
}

static tcb_t *pick_next(void)
{
  int priority = ready_highest_priority();

  if(priority < 0)
  {
    return &task_idle;
  }

  tcb_t *head = ready_first((uint32_t)priority);

  if(g_current != NULL &&
    g_current->state == TASK_READY &&
    g_current->ready_queued &&
    g_current->prio == (uint32_t)priority &&
    head == g_current)
  {
    ready_rotate(priority);
  }
  return ready_first((uint32_t)priority);
}

void task_yield(void)
{
  if (g_rtos_start != 0U)
  {
    port_request_switch();
  }
}

__attribute__((used, noinline)) uint32_t *rtos_switch_context(void)
{
  g_current = pick_next();
  return g_current->sp;
}

void task_delay(uint32_t ticks)
{
  uint32_t irq_state = task_enter_critical();
  tcb_t *current = g_current;
  if(current != NULL && current != &task_idle)
  {
    ready_remove(current);

    current->wake_tick = port_get_tick() + ticks;
    current->state = TASK_DELAYED;
    delay_list_insert(current);
    task_yield();
  }
  task_exit_critical(irq_state);
}

void rtos_tick(void)
{
  if(g_rtos_start == 0)
  {
    return;
  }
  uint32_t irq_state = task_enter_critical();

  delay_list_wake(port_get_tick());

  task_yield();

  task_exit_critical(irq_state);
}
