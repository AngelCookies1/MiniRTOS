# MiniRTOS 内核文件导航

## 文件职责

| 文件 | 内容 |
| --- | --- |
| `kernel.h` | 应用接口、任务/信号量类型，main.c 只需包含它 |
| `config.h` | 优先级数量、idle 栈大小、异常优先级 |
| `kernel_internal.h` | 内核模块间的声明，应用不要包含 |
| `task.c` | 任务初始化与注册、idle、选任务、阻塞延时、tick、切换决策 |
| `list.c` | 就绪循环链表、就绪位图、按截止时间排序的延时链表 |
| `sync.c` | 二值信号量、等待队列、超时及成功后的统一清理 |
| `port.c` | 初始栈帧、临界区、HAL 时间适配、异常优先级、SVC 启动 |
| `port_asm.s` | SVC 首次恢复、PendSV 保存/恢复现场 |
| `config.c`、`heap.c` | 预留空文件，当前没有实现，因此未加入构建 |

`Core/Src/main.c` 保留时钟、GPIO、UART 初始化及 A/B/C 示例任务。
`Core/Src/stm32f1xx_it.c` 中的 SysTick 先执行 `HAL_IncTick()`，再执行 `rtos_tick()`。

## 启动顺序

1. 硬件初始化。
2. `task_init()` 初始化并注册每个用户任务（内部调用 `ready_add()`）。
3. `sem_init()` 初始化信号量。
4. `rtos_start()` 创建并注册 idle，选择最高优先级任务，调用平台启动函数。
5. SVC 恢复第一个任务的 PSP，并将 `g_rtos_start` 置 1。

因此 main 不再直接调用 `ready_add()`，也不再设置内核异常优先级。
用户任务和栈使用静态分配；不要在运行时再次初始化已注册任务或信号量。

## 运行链路

```text
SysTick -> HAL_IncTick -> rtos_tick
                         -> delay_list_wake
                         -> task_yield -> 挂起 PendSV

PendSV -> 保存当前任务现场与 SP
       -> rtos_switch_context -> pick_next -> 更新 g_current
       -> 恢复新任务现场 -> 异常返回

sem_take_timeout -> 移出就绪队列 -> 进入信号量等待队列
                                  -> 有限等待时加入延时链表
sem_give / 超时 -> sem_finish_wait -> 同时清理两条等待链表 -> 加入就绪队列
```

## 约束和当前范围

- 任务优先级数字越大越高，0 留给 idle；异常优先级数字越小越高。
- TCB 的 `sp` 必须是第一个成员，task.c 有编译期检查。
- 运行期间所有队列操作都要求临界区保护，PendSV 也保护整个调度过程。
- 阻塞接口只能在普通任务、中断开启且没有外层临界区时调用。
- 信号量目前不支持中断释放；有限等待最大为 `INT32_MAX` tick。
- `SEM_WAIT_FOREVER` 表示永久等待，不登记超时。
- 同一次等待只能结束一次：成功与超时都经 `sem_finish_wait()` 清理。
- 仍使用 HAL 的 1 ms tick。任务返回、动态内存、栈溢出检测尚未实现。
- 延时排序使用带符号差值，要求时间跨度小于半个 32 位计数周期，长时间屏蔽 tick 不受支持。
- CubeMX 若重新生成 SVC/PendSV 的 C 函数，需避免与 port_asm.s 的强定义重复。

## 当前示例

- A：约 200 ms 的计算循环，更新 `test_sink`。
- B：信号量等待 300 tick；成功翻转 PC13，更新 `sem_ok_count`，超时更新 `sem_timeout_count`。
- C：每次延时 200 tick，翻转 PA0 并释放信号量。

调度、链表和信号量算法沿用拆分前的实现；这次整理不增加新内核功能。
