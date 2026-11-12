#ifndef MINIRTOS_KERNEL_INTERNAL_H
#define MINIRTOS_KERNEL_INTERNAL_H

#include "kernel.h"

/* 仅 kernel 内部包含；应用通过 kernel.h 使用内核。 */
extern tcb_t * volatile g_current;
extern volatile uint32_t g_rtos_start;
extern tcb_t task_idle;

/* 以下队列函数运行时要求调用者已经进入临界区。启动前可直接使用。 */
void ready_add(tcb_t *task);
void ready_remove(tcb_t *task);
void ready_rotate(uint32_t priority);
int ready_highest_priority(void);
tcb_t *ready_first(uint32_t priority);
void delay_list_insert(tcb_t *task);
void delay_list_remove(tcb_t *task);
void delay_list_wake(uint32_t now);
void sem_finish_wait(tcb_t *task, sem_result_t result);

/* Cortex-M3 / STM32 平台接口。 */
uint32_t task_enter_critical(void);
void task_exit_critical(uint32_t primask);
uint32_t *port_stack_init(uint32_t *stack_top,
                          void (*entry)(void *), void *arg);
uint32_t port_get_tick(void);
uint32_t port_get_ipsr(void);
uint32_t port_get_primask(void);
void port_request_switch(void);
void port_idle(void);
__attribute__((noreturn)) void port_start_first_task(void);
__attribute__((noreturn)) void kernel_panic(void);

/* PendSV 汇编的 C 入口，运行时中断已被屏蔽。 */
uint32_t *rtos_switch_context(void);

#endif
