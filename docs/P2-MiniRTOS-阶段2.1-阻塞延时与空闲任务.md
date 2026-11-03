# P2 MiniRTOS 阶段 2.1 复盘：阻塞延时与空闲任务

> 平台：STM32F103C8T6（Cortex-M3，72MHz，20KB RAM）
> 工程：`D:\STM32学习\MiniRTOS`
> 前置：第一周（[`P2-MiniRTOS-第一周-代码复盘.md`](P2-MiniRTOS-第一周-代码复盘.md)）已完成 SVC 启动 + PendSV 双任务切换
> 完成日期：2026-11-03

---

## 目录

1. [这一阶段做了什么](#一这一阶段做了什么)
2. [核心概念：调度不变量](#二核心概念调度不变量)
3. [四个坑](#三四个坑)
4. [代码清单](#四代码清单)
5. [验证记录](#五验证记录)
6. [已知遗留](#六已知遗留)
7. [下一步](#七下一步)

---

## 一、这一阶段做了什么

从"两个任务轮流跑"变成"任务能睡、能被叫醒、没人跑时有地方待着"。

| 能力 | 之前 | 现在 |
|---|---|---|
| 任务状态 | 无 | `TASK_READY` / `TASK_DELAYED` |
| 切换决策 | 写死 `A ↔ B` 轮转 | `pick_next()` 扫就绪队列 |
| 阻塞延时 | `HAL_Delay()` 忙等 | `task_delay()` 让出 CPU |
| 唤醒 | 无 | `SysTick` 里检查 `wake_tick` |
| 无任务可跑 | 未处理（隐患） | 空闲任务 `__WFI()` |

一句话：**内核第一次有了"状态"，而不只是"两个栈"。**

---

## 二、核心概念：调度不变量

整个阶段最值钱的一条不是代码，是这个约束：

> **`pick_next()` 的返回值，必须是一个 `TASK_READY` 的任务。**

看起来像废话，但它决定了系统会不会在某个角落静默崩掉。

看反例——最初的兜底写法：

```c
return g_current;   /* 没有就绪任务，继续运行当前任务 */
```

它把「**没人可跑**」伪装成「**就绪的是我自己**」。于是 `task_yield()` 会做一次"存进自己栈再取回来"的空切换，让一个**状态是 `TASK_DELAYED` 的任务继续执行**。

后果不是崩溃，是**语义被静默破坏**：`task_delay()` 不再延迟，任务在空转，另一个任务永远等不到调度。你看到的现象是"串口卡在某个数字不动"，而代码一行没报错。

**好的内核不是靠 `if` 检查正确性，是靠结构让错误不可能发生。**

空闲任务的真正作用不是"占位"，而是让 `pick_next()` 的返回值**类型上永远非空、状态上永远 READY**。加上它之后，那个兜底分支根本不需要存在。

---

## 三、四个坑

### 坑 1：无符号比较恒为真

```c
if (HAL_GetTick() - task_a.wake_tick >= 0)   /* ❌ 永远成立 */
```

`HAL_GetTick()` 返回 `uint32_t`，差值也是无符号。**无符号数永远 ≥ 0**，所以这个条件退化成"只要状态是 DELAYED 就立刻唤醒"——`task_delay(500)` 实际只延迟了 1 个 tick。

**怎么发现的**：不是靠读代码，是靠算数据。

```
B: 19860    A: 8412    →  比值 2.36
```

如果 A 真的延迟 500ms，A 每秒 2 行、B 每秒 1200 行，比值应该是 600 倍以上。2.36 说明延迟只有约 1ms。**用比例反推时长，比盯着代码看快得多。**

**正确写法**：

```c
if ((int32_t)(HAL_GetTick() - task_a.wake_tick) >= 0)   /* ✅ */
```

为什么先做减法再转类型：`HAL_GetTick()` 约 49.7 天回绕一次。先按无符号减法算模 2³² 的差值（回绕时依然正确），再解释成 `int32_t`——负数表示没到，正数表示过期。直接转成有符号再比大小，回绕那天就崩。

**编译器为什么不拦**：`-Wall` 不管这个，`-Wextra` 的 `-Wtype-limits` 才会报 `comparison of unsigned expression >= 0 is always true`。

### 坑 2：强制类型转换的括号位置

第一次修复时写成了：

```c
if ((int32_t)(HAL_GetTick() - task_a.wake_tick >= 0))   /* ❌ 还是错的 */
```

括号套在整个比较外面——先算出比较结果（0 或 1），再把这个 0/1 转成 `int32_t`。里面的比较仍然是无符号比较，仍然恒真。

| 写法 | 转的是什么 | 结果 |
|---|---|---|
| `(int32_t)(A - B >= 0)` | 比较结果 0/1 | 恒真 ❌ |
| `(int32_t)(A - B) >= 0` | 差值本身 | 正确 ✅ |

**这类错误的可怕之处：语法完全合法，编译器沉默，运行结果和没改一样。** 只能靠自己盯括号。

### 坑 3：`pick_next` 的兜底分支

见上一节。症状是"任务空转 + 另一个任务永久失联"，根因是调度不变量被破坏。

**教训**：调度器、状态机这类代码里的"兜底返回当前值"，几乎总是 bug 温床。真正该做的是**让"无任务可跑"成为一个合法的、有明确承载者的状态**。

### 坑 4：缺少空闲任务

加 idle 之前，两个任务都 `task_delay` 时系统进入非法状态。加 idle 之后：

```c
static void idle_entry(void *arg)
{
    (void)arg;
    while (1)
    {
        __WFI();        /* 真的停机，等下一个中断 */
        task_yield();
    }
}
```

`__WFI()` 让 CPU 在无事可做时真正停机省电，这是真实 RTOS 里 idle 的日常用法。

**注意 `task_yield()` 不能省**：如果 idle 只 `WFI` 不 `yield`，被唤醒的任务永远等不到切换——idle 会一直自己循环下去。

---

## 四、代码清单

全部在 `D:\STM32学习\MiniRTOS`：

### `Core/Src/main.c` — 任务状态与延时

```c
typedef enum
{
  TASK_READY,
  TASK_DELAYED,
} task_state_t;

typedef struct {
  uint32_t *sp;
  uint32_t prio;
  volatile task_state_t state;
  volatile uint32_t wake_tick;
} tcb_t;
```

```c
void task_delay(uint32_t ticks)
{
  g_current->wake_tick = HAL_GetTick() + ticks;
  g_current->state = TASK_DELAYED;
  task_yield();          /* 让出，不是忙等 */
}
```

### `Core/Src/main.c` — 选任务

```c
static tcb_t *pick_next(void)
{
  tcb_t *tasks[2] = {&task_a, &task_b};
  int cur = (g_current == &task_a) ? 0 : 1;

  for (int i = 1; i <= 2; i++)
  {
    tcb_t *t = tasks[(cur + i) % 2];
    if (t->state == TASK_READY) return t;
  }

  return &task_idle;    /* 兜底：一定有活儿干 */
}
```

两个细节：

- **从 `cur + 1` 开始扫**，保证"轮到下一个"而不是"永远优先 A"。任务多了以后这是防饿死的原点。
- **idle 不参与轮转**，只在用户任务全不可跑时返回。这其实就是"优先级"的雏形。

### `Core/Src/main.c` — 唤醒

```c
void rtos_tick(void)
{
  if (task_a.state == TASK_DELAYED && (int32_t)(HAL_GetTick() - task_a.wake_tick) >= 0)
    task_a.state = TASK_READY;

  if (task_b.state == TASK_DELAYED && (int32_t)(HAL_GetTick() - task_b.wake_tick) >= 0)
    task_b.state = TASK_READY;
}
```

### `Core/Src/stm32f1xx_it.c` — 挂载点

```c
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */
  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */
  extern void rtos_tick(void);
  rtos_tick();
  /* USER CODE END SysTick_IRQn 1 */
}
```

**为什么写在这里**：CubeMX 重新 Generate 不会覆盖 `USER CODE` 块内的内容。这比接管整个 `SysTick_Handler` 安全——那要和 `it.c` 的强定义打架，而且重新生成会复活。

**为什么放在 `IRQn 1` 而不是 `IRQn 0`**：`HAL_IncTick()` 必须先跑完，`HAL_GetTick()` 才是新值。

> 注：F1 的 HAL 里**没有** `HAL_SYSTICK_Callback()` 这个弱函数（那是 F4/H7 模板的东西），所以"实现弱回调"这条路在 F103 上走不通。

---

## 五、验证记录

### 单任务延时（A 睡 500，B 全速）

```
A:1  →  B:1 … B:793  →  A:2  →  B:794 …
```

793 行 B 对应 500ms。每行 `B:790\r\n` 是 7 字节 = 70 bit，115200 波特率下理论极限 1645 行/秒，实测 **1586 行/秒 = 带宽的 96%**。

> 判读：A 能停 500ms 不算本事；**A 停的这 500ms 里 CPU 完全归 B 用**，才是"阻塞延时"跑通的证据。

### 双任务延时（A 睡 500，B 睡 1000）

```
B:10  A:20 A:21  B:11  A:22 A:23  B:12  A:24 A:25
B:13  A:26 A:27  B:14  A:28 A:29  B:15  A:30 A:31  B:16
```

**每 1 行 B，正好 2 行 A** —— 1000 ÷ 500 = 2，符合预期。

时间校验：A:20→A:31 是 11 个间隔 = 5.5s；B:10→B:16 是 6 个间隔 = 6s。同一窗口内自洽。

> **判读技巧**：两个任务的**输出比例**应该等于它们周期的反比。比例不对，先查延时值；比例对但总数不对，查 tick 频率。

### 故障记录：B 卡在 793（悬案）

修复兜底之前，出现过 B 停在 `B:793`、A 之后连续空转（`A:2` 到 `A:10` 之间没有一行 B，说明 A 也没在睡）。

加 idle task 后现象消失。**确切机制未查到底**，符合"兜底分支破坏不变量"导致的连锁故障特征。

若将来在高负载下复现，排查方向：**调度器记录的 `g_current` 与 CPU 实际运行的任务不一致**。这是同类问题里最阴险的一类——`PendSV` 里 `STR R1, [R0]` 存的是"我以为的旧任务"，假设错了就会把 A 的寄存器存进 B 的栈，把 B 的现场覆盖成垃圾。B 之后被恢复时"活着但什么都不干"，正是当时的现象。

---

## 六、已知遗留

| 项 | 说明 | 影响 |
|---|---|---|
| idle 每 tick 空切换 | `pick_next` 返回 idle 时 `g_prev == g_current`，照样存/取一次 | 1ms 一次，无害 |
| 无抢占 | 高优先级任务醒来后要等当前任务 `yield` 才能运行 | 实时性差 |
| `prio` 字段未使用 | `task_init` 里赋了值，但 `pick_next` 不看 | 无优先级调度 |
| `g_current` 在挂起前修改 | `task_yield` 先改指针再置 PendSV，非原子 | 多优先级后成隐患 |
| 任务数写死在 `pick_next` | `tcb_t *tasks[2]` | 加任务要改代码 |

### 空切换的优化（可做可不做）

```c
void task_yield(void)
{
  tcb_t *next = pick_next();

  if (next == g_current) return;   /* 没人可换，省掉一次空切换 */

  g_prev    = g_current;
  g_current = next;
  ...
}
```

**注意判断必须放在 `pick_next()` 之后。** 加 idle 之前不能这么写——那时 `pick_next` 可能返回 `TASK_DELAYED` 的任务，跳过切换会让它带着错误状态继续跑。现在前提不存在了，所以安全。

---

## 七、下一步

**下一步：优先级 + 抢占。**

但在写代码之前，先做一个实验，让"为什么需要抢占"变得具体：

1. 把 `task_a_entry` 改成做一段纯计算（比如空转 200ms 的循环），不再调 `task_delay`
2. 把 B 的周期改成 100ms

没有抢占时你会看到：**B 本该 100ms 一次，实际被 A 的计算拖成 200ms 一次**。

看到这个"迟到"，优先级和抢占才有具体的意义——否则只是照着清单加功能。

之后的技术顺序：

```
优先级 + 抢占  →  就绪位图  →  临界区  →  同步原语
```

位图放在抢占之后，是因为"选优先级最高的就绪任务"在任务数少时用循环就够，位图的价值要等任务多起来、并且切换发生在中断里（每次切换都扫一遍数组代价就显出来了）才体现。

---

## 附：一句话总结

这一阶段真正学到的不是"怎么写 task_delay"，而是：

> **调度器必须守住一个不变量，然后用结构保证它，而不是用 if 检查它。**
