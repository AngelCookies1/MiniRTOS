/* Cortex-M3 / GNU assembler。异常处理使用 MSP，任务现场使用 PSP。 */
.syntax unified
.cpu cortex-m3
.thumb

.extern g_current
.extern g_rtos_start
.extern rtos_switch_context

.section .text.PendSV_Handler, "ax", %progbits
.global PendSV_Handler
.type PendSV_Handler, %function
.thumb_func
PendSV_Handler:
    /* 保存中断状态和 EXC_RETURN；MSP 仍保持 8 字节对齐。 */
    MRS   R2, PRIMASK
    CPSID I
    PUSH  {R2, LR}
    /* 保存旧任务 R4-R11，并将栈顶写入当前 TCB 首字段。 */
    MRS   R0, PSP
    STMDB R0!, {R4-R11}
    MOVW  R1, #:lower16:g_current
    MOVT  R1, #:upper16:g_current
    LDR   R1, [R1]
    STR   R0, [R1]
    /* C 调度函数返回下一任务 SP。LR 已保存在 MSP。 */
    BL    rtos_switch_context
    LDMIA R0!, {R4-R11}
    MSR   PSP, R0
    ISB
    /* 新 PSP 已装好，再恢复中断状态并异常返回。 */
    POP   {R2, LR}
    MSR   PRIMASK, R2
    BX    LR
.size PendSV_Handler, .-PendSV_Handler

.section .text.SVC_Handler, "ax", %progbits
.global SVC_Handler
.type SVC_Handler, %function
.thumb_func
SVC_Handler:
    CPSID I
    MOVW  R0, #:lower16:g_current
    MOVT  R0, #:upper16:g_current
    LDR   R0, [R0]
    LDR   R0, [R0]
    LDMIA R0!, {R4-R11}
    MSR   PSP, R0
    /* 首次任务 PSP 就绪，允许后续 SysTick 请求调度。 */
    MOVW  R0, #:lower16:g_rtos_start
    MOVT  R0, #:upper16:g_rtos_start
    MOVS  R1, #1
    STR   R1, [R0]
    ORR   LR, LR, #0x0D
    DSB
    ISB
    CPSIE I
    BX    LR
.size SVC_Handler, .-SVC_Handler
