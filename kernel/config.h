#ifndef MINIRTOS_CONFIG_H
#define MINIRTOS_CONFIG_H

/* 任务优先级 0..7，数字越大优先级越高；0 留给内核 idle。 */
#define MAX_PRIORITY 8
#define IDLE_STACK_WORDS 64

/* Cortex-M 中断优先级数字越小越高，与任务优先级规则相反。 */
#define PORT_SVC_PRIORITY 0U
#define PORT_SYSTICK_PRIORITY 14U
#define PORT_PENDSV_PRIORITY 15U

#if MAX_PRIORITY < 2 || MAX_PRIORITY > 32
#error "MAX_PRIORITY must be between 2 and 32"
#endif

#endif
