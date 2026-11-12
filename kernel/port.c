#include "kernel_internal.h"
#include "main.h"

/* STM32F103 / Cortex-M3 平台相关代码集中在本文件和 port_asm.s。 */

uint32_t *port_stack_init(uint32_t *stack_top, void (*entry)(void *), void *arg)
{
  uint32_t *sp = stack_top;

    *(--sp) = 0x01000000u;      /* xPSR  : 必须带 T 位(bit24)，否则首次取指就 UsageFault */
    *(--sp) = (uint32_t)entry;  /* PC    : 任务的入口 */
    *(--sp) = 0xFFFFFFFDu;      /* 任务 LR：当前不支持任务返回；handler LR 才用于异常返回 */
    *(--sp) = 0;                /* R12 */
    *(--sp) = 0;                /* R3  */
    *(--sp) = 0;                /* R2  */
    *(--sp) = 0;                /* R1  */
    *(--sp) = (uint32_t)arg;              /* R0  : AAPCS 第一个参数 */
    *(--sp) = 0;                /* R11 */
    *(--sp) = 0;                /* R10 */
    *(--sp) = 0;                /* R9  */
    *(--sp) = 0;                /* R8  */
    *(--sp) = 0;                /* R7  */
    *(--sp) = 0;                /* R6  */
    *(--sp) = 0;                /* R5  */
    *(--sp) = 0;                /* R4  */
    return sp;
}

uint32_t task_enter_critical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

void task_exit_critical(uint32_t primask)
{
  __DSB();
  __ISB();
  __set_PRIMASK(primask);
}

uint32_t port_get_tick(void)
{
  return HAL_GetTick();
}

uint32_t port_get_ipsr(void)
{
  return __get_IPSR();
}

uint32_t port_get_primask(void)
{
  return __get_PRIMASK();
}

void port_request_switch(void)
{
  SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
  __DSB();
  __ISB();
}

void port_idle(void)
{
  __WFI();
}

void port_start_first_task(void)
{
  SCB->CCR |= SCB_CCR_STKALIGN_Msk;

  NVIC_SetPriority(SVCall_IRQn, PORT_SVC_PRIORITY);
  NVIC_SetPriority(SysTick_IRQn, PORT_SYSTICK_PRIORITY);
  NVIC_SetPriority(PendSV_IRQn, PORT_PENDSV_PRIORITY);

  __DSB();
  __ISB();
  __asm volatile ("svc 0" ::: "memory");

  kernel_panic();
}

void kernel_panic(void)
{
  /* 保持原工程的错误处理入口，便于在调试器中设置断点。 */
  Error_Handler();
  __disable_irq();
  while (1)
  {
  }
}
