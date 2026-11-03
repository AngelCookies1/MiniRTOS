# P2 MiniRTOS 第一周代码复盘：PendSV / PSP / 栈初始化

> 平台：STM32F103C8T6（Cortex-M3，72MHz，20KB RAM）
> 工程：`D:\STM32学习\MiniRTOS`（CubeMX 生成 + VS Code + CMake/Ninja + arm-none-eabi-gcc）
> 对应路线：[`P2-MiniRTOS-第一周-学习路径.md`](P2-MiniRTOS-第一周-学习路径.md)、[`P2-MiniRTOS-实现清单.md`](P2-MiniRTOS-实现清单.md)
> 完成日期：2026-11-01（D1–D6 一天跑完，D7 收尾）

本文回答一个问题：**为什么这样几行代码，就能让一个从来没被调用过的函数开始跑，并让两个函数交替执行？**

---

## 目录

1. [全景：只做了两件事](#一全景只做了两件事)
2. [三块栈，各管一段](#二三块栈各管一段)
3. [内存里到底摆了什么](#三内存里到底摆了什么)
4. [启动链路（SVC，只发生一次）](#四启动链路svc只发生一次)
5. [切换链路（PendSV，跑了 5000+ 次）](#五切换链路pendsv跑了-5000-次)
6. [逐条讲汇编](#六逐条讲汇编)
7. [逐个函数](#七逐个函数)
8. [四个"不能这么写"](#八四个不能这么写)
9. [现在的系统能做什么、不能做什么](#九现在的系统能做什么不能做什么)
10. [附录 A：验证记录](#附录-a验证记录)
11. [附录 B：踩坑清单](#附录-b踩坑清单)
12. [附录 C：完整代码索引](#附录-c完整代码索引)

---

## 一、全景：只做了两件事

整个第一周，剥掉所有代码，只有两个动作：

```
① 造一个"假的返回现场"  →  让一个从没被调用过的函数开始执行   （SVC）
② 交换两个"现场"        →  让两个函数交替执行                 （PendSV）
```

它们共用同一个底层机制：**异常返回（exception return）**。

Cortex-M 的硬件在异常返回时会做一件事——从栈里弹出 8 个寄存器并跳转。你做的事就是**把这个"栈"换成自己伪造的**，于是 CPU 以为任务一直在运行。

---

## 二、三块栈，各管一段

| 栈 | 谁在用 | 什么时候用 |
|---|---|---|
| **MSP**（主栈） | `main()`、**所有中断/异常** | 复位后就指着它；进中断时硬件自动切过来 |
| **PSP_A**（`task_a_stack`） | 任务 A | 任务 A 运行时 |
| **PSP_B**（`task_b_stack`） | 任务 B | 任务 B 运行时 |

一条必须记住的规则：

> **异常 handler 永远用 MSP，不管任务用哪个栈。**

所以 PSP 只在"线程模式"下有意义。这也解释了为什么切任务不需要动 MSP——中断那边完全不受影响。

控制这个切换的是 `CONTROL.SPSEL` 位：置 0 用 MSP，置 1 用 PSP。复位后为 0，所以 `main` 跑在 MSP 上。

---

## 三、内存里到底摆了什么

`stack_init` 造出来的东西，长这样（以任务 A 为例）：

```
高地址  &task_a_stack[128]
        ┌─────────────────────┐
 [15]   │ xPSR = 0x01000000   │  ← bit24 的 T 位必须为 1（Thumb 态）
 [14]   │ PC   = task_a_entry │  ← 任务"从哪一行开始执行"
 [13]   │ LR   = 0xFFFFFFFD   │  ← 返回后要回到 PSP 上的线程模式
 [12]   │ R12                 │
 [11]   │ R3                  │   ┐
 [10]   │ R2                  │   │ 硬件帧：异常返回时由 CPU 自动弹出
 [ 9]   │ R1                  │   │ 共 8 个字
 [ 8]   │ R0   = arg          │   ┘ R0 = 第一个参数（AAPCS）
 [ 7]   │ R11                 │
 [ 6]   │ R10                 │
   ⋮    │  ⋮                  │   软件帧：R4-R11 由 PendSV 汇编手工存取
 [ 0]   │ R4                  │
        └─────────────────────┘
低地址  ← task_a.sp 初始值指向这里
```

### 为什么硬件只存 8 个、软件要存 8 个？

ARM 的调用约定（AAPCS）规定：

- `R0-R3, R12, LR` 是**调用者保存（caller-saved）**，函数可以随便改；
- `R4-R11` 是**被调用者保存（callee-saved）**，函数必须保证调用前后不变。

Cortex-M 的异常硬件按这个约定只压前一组——它假设"异常处理程序是个函数"。但**异常返回不是函数调用**，CPU 没有义务帮你保住 R4-R11，编译器也不知道你的代码会在异常里被切成两半。所以 `R4-R11` 必须由 RTOS 自己存。

这就是 `STMDB R0!, {R4-R11}` 存在的全部理由。

> 补充：任务内被切走时，它的 C 局部变量大多放在 R4–R11 里（编译器优先用 callee-saved 寄存器放长生命周期的值）。所以"计数器 A:1→2→3 连续"这条现象，证明的正是软件帧存取正确。

---

## 四、启动链路（SVC，只发生一次）

```
复位
 ↓
main()  ── stack_init() 造好 task_a 的假帧
 ↓
rtos_start()  ── 设 g_current = &task_a，PSP = 0
 ↓
svc 0   ── 陷入，硬件把 main 的现场压到 MSP 上，LR = 0xFFFFFFF9
 ↓
SVC_Handler
 ├─ 取 task_a.sp
 ├─ LDMIA 装上 R4-R11
 ├─ MSR PSP ← task_a.sp + 32   （指向硬件帧起点 R0）
 └─ ORR LR,#0x0D  → 0xFFFFFFFD，BX LR
 ↓
硬件按 0xFFFFFFFD 从 PSP 弹出 8 个字
 ↓
PC = task_a_entry  ← 任务"凭空"开始跑了
```

最后一步的关键：**任务从来没有被 `call` 过，它是被"返回"进去的。**

这就是 `arg` 能是 `0x12345678` 而代码里从没赋值的原因——那个值一直躺在栈里等着被弹出来。

---

## 五、切换链路（PendSV，跑了 5000+ 次）

```
task_a 调用 task_yield()
 ├─ g_prev = &task_a
 ├─ g_current = &task_b
 └─ 写 ICSR.PENDSVSET
 ↓
PendSV 被接受（优先级 15，等所有中断让路）
 硬件先把 task_a 的 R0-R3/R12/LR/PC/xPSR 压到 **PSP_A** 上
 ↓
PendSV_Handler
 ├─ 存：task_a 的 R4-R11 → PSP_A，g_prev->sp = PSP_A
 └─ 取：g_current->sp → PSP_B，LDMIA 装上 R4-R11，MSR PSP
 ↓
BX LR（0xFFFFFFFD）→ 硬件从 PSP_B 弹帧 → 继续跑 task_b
```

**最关键的一句：硬件帧自动落在"被中断的那个栈"上。**

任务 A 运行时 PSP 指向 A 的栈，所以它的 8 个字自然压在 A 上；任务 B 同理。于是 PendSV 唯一要做的就是"换 PSP + 换 R4-R11"——**任务切换的全部计算量就是这 14 条指令**。

---

## 六、逐条讲汇编

### 6.1 SVC_Handler（只跑一次，负责"开机"）

```c
__attribute__((naked)) void SVC_Handler(void)
{
    __asm volatile (
        "MOVW  R0, #:lower16:g_current   \n"
        "MOVT  R0, #:upper16:g_current   \n"
        "LDR   R0, [R0]                  \n"   /* R0 = g_current (tcb_t*) */
        "LDR   R0, [R0]                  \n"   /* R0 = task_a.sp          */
        "LDMIA R0!, {R4-R11}             \n"
        "MSR   PSP, R0                   \n"
        "ORR   LR, LR, #0x0D             \n"
        "BX    LR                        \n"
    );
}
```

| 指令 | 作用 | 为什么 |
|---|---|---|
| `MOVW R0, #:lower16:g_current` | 取地址低 16 位 | 不用 `LDR R0,=sym`，是为了绕开文字池 ±4KB 的范围限制 |
| `MOVT R0, #:upper16:g_current` | 补上高 16 位 | 两条拼出完整 32 位地址 |
| `LDR R0, [R0]` | R0 = `g_current` | 第一次解引用：拿到 `tcb_t*` |
| `LDR R0, [R0]` | R0 = `g_current->sp` | 第二次解引用：**因为 `sp` 是结构体第一个成员**，偏移为 0 |
| `LDMIA R0!, {R4-R11}` | 装上软件帧，R0 前移 32 | `IA` = 先读后加；`!` = 回写 R0 |
| `MSR PSP, R0` | PSP 指向硬件帧起点 | 此时 R0 正好指向 `[8]`（R0 槽） |
| `ORR LR, LR, #0x0D` | `0xFFFFFFF9` → `0xFFFFFFFD` | 把"用 MSP 返回"改成"用 PSP 返回" |
| `BX LR` | 异常返回 | 硬件据此弹帧、跳 PC |

两次 `LDR` 是最容易写错的地方。把 `sp` 放在结构体第一位，就是为了第二次 `LDR` 不用加偏移——**这个约定全世界的 RTOS 都在用**，FreeRTOS 的 `pxTopOfStack` 也在第一个字。

### 6.2 PendSV_Handler（每次切换都跑）

```c
__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile (
        /* 1. 把旧任务的 r4-r11 存进它自己的栈顶 */
        "MOVW  R0, #:lower16:g_prev     \n"
        "MOVT  R0, #:upper16:g_prev     \n"
        "LDR   R0, [R0]                 \n"   /* R0 = g_prev (tcb_t*)  */
        "MRS   R1, PSP                  \n"
        "STMDB R1!, {R4-R11}            \n"
        "STR   R1, [R0]                 \n"   /* g_prev->sp = R1       */

        /* 2. 从新任务的栈顶把 r4-r11 装回来 */
        "MOVW  R2, #:lower16:g_current  \n"
        "MOVT  R2, #:upper16:g_current  \n"
        "LDR   R2, [R2]                 \n"   /* R2 = g_current        */
        "LDR   R1, [R2]                 \n"   /* R1 = next->sp         */
        "LDMIA R1!, {R4-R11}            \n"
        "MSR   PSP, R1                  \n"
        "ORR   LR, LR, #0x0D            \n"
        "BX    LR                       \n"
    );
}
```

| 指令 | 作用 |
|---|---|
| `MRS R1, PSP` | 读出当前任务的栈指针（此时已经压好了硬件帧） |
| `STMDB R1!, {R4-R11}` | 把 R4-R11 压到 PSP 下方，R1 减 32（`DB` = 先减后存） |
| `STR R1, [R0]` | `g_prev->sp = R1` —— **保存旧任务的栈顶** |
| `LDR R1, [R2]` | `g_current->sp` —— 取新任务上次保存的栈顶 |
| `LDMIA R1!, {R4-R11}` | 装上新任务的 R4-R11，R1 前移 32 |
| `MSR PSP, R1` | 换栈 |
| `ORR LR, LR, #0x0D` | 统一成 `0xFFFFFFFD` |
| `BX LR` | 返回，硬件从新栈弹帧 |

这里 `ORR` 其实**是空操作**：任务在 PSP 上运行，PendSV 进来时 `LR` 本来就是 `0xFFFFFFFD`。留着它是为了和 SVC 那条路径写法统一，也防止将来从别处切过来时出错。

### 6.3 两个 EXC_RETURN 值

| 值 | 低 4 位 | 含义 |
|---|---|---|
| `0xFFFFFFF9` | `1001` | 返回**线程模式，用 MSP**（`main` 被 SVC 打断时） |
| `0xFFFFFFFD` | `1101` | 返回**线程模式，用 PSP**（任务被 PendSV 打断时） |
| `0xFFFFFFF1` | `0001` | 返回 **Handler 模式，用 MSP**（异常嵌套时） |

低 4 位的含义：bit0 恒为 1；bit2 = 用哪个栈（1 = PSP）；bit3 = 1 表示回线程模式（0 = 回 Handler 模式）。

`ORR LR, LR, #0x0D` 之所以能把 `0xF9` 变成 `0xFD`：`0x0D = 1101b`，只把 bit2 从 0 置成 1，其余位不变。

---

## 七、逐个函数

### `stack_init(stack_top, entry, arg)` —— 造一具"尸体"

```c
static uint32_t *stack_init(uint32_t *stack_top, void (*entry)(void *), void *arg)
{
    uint32_t *sp = stack_top;

    *(--sp) = 0x01000000u;      /* xPSR  : T 位(bit24)必须为 1 */
    *(--sp) = (uint32_t)entry;  /* PC    : 任务的入口 */
    *(--sp) = 0xFFFFFFFDu;      /* LR    : 返回线程模式用 PSP */
    *(--sp) = 0;                /* R12 */
    *(--sp) = 0;                /* R3  */
    *(--sp) = 0;                /* R2  */
    *(--sp) = 0;                /* R1  */
    *(--sp) = (uint32_t)arg;    /* R0  : AAPCS 第一个参数 */
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
```

| 要点 | 说明 |
|---|---|
| 为什么从 `stack_top` 往下写 | 栈是满递减（full-descending），生长方向朝低地址 |
| 为什么先写 xPSR、最后写 R4 | 因为每次 `--sp` 都往低处走，高地址的先写 |
| `0x01000000` 是什么 | xPSR 的 T 位（bit24），表示"这段代码是 Thumb 指令"。**漏了它，首次取指直接 `INVSTATE` HardFault** |
| `0xFFFFFFFD` 是什么 | 假 LR，让 CPU 认为"我是从 PSP 上的线程模式被打断的" |
| 为什么要 8 字节对齐 | 异常返回要求栈 8 字节对齐；`aligned(8)` + 16 字（64 字节）保证了这个性质 |
| `entry` 为什么是 `void (*)(void *)` | 与 FreeRTOS 的 `pxTaskCode` 一致；`arg` 通过 R0 传进去 |

它不是"初始化任务"，是**伪造一段从未发生过的历史**。任务第一次运行时，会以为自己一直在跑。

### `rtos_start()` —— 按下去启动

```c
void rtos_start(void)
{
    g_current = &task_a;
    __set_PSP(0);
    __DSB();
    __ISB();
    __asm volatile ("svc 0");
}
```

- `__set_PSP(0)`：先把 PSP 清零，避免残留值干扰（SVC 里会立刻覆盖它）；
- `__DSB/__ISB`：保证前面写内存的操作完成、流水线刷新，再去异常；
- `svc 0`：立即数 `0` 不用，因为没有参数要传。

### `task_yield()` —— 让出

```c
void task_yield(void)
{
    g_prev    = g_current;
    g_current = (g_current == &task_a) ? &task_b : &task_a;

    __asm volatile ("dsb" ::: "memory");
    __asm volatile ("isb" ::: "memory");
    (*(volatile uint32_t *)0xE000ED04) = (1u << 28);
}
```

三个设计点：

1. **调度逻辑在 C 里，汇编只搬运**。这是刻意的分工——`PendSV_Handler` 里绝不能 `BL` 到 C 函数，因为 `LR` 此刻是 `0xFFFFFFF9/…D`，一个 `BL` 就把它冲掉了，返回现场直接丢。
2. **两个指针在挂起之前就设好**。PendSV 进来时只认 `g_prev` / `g_current`，不再做任何判断。
3. **写 `1<<28` 只是"挂起"，不是"跳转"**。异常在下一个指令边界才被接受，所以顺序不能反。

### `main()` —— 只活到 `rtos_start()` 为止

```c
task_a.sp = stack_init(&task_a_stack[STACK_WORDS], task_a_entry, (void *)0x12345678u);
task_b.sp = stack_init(&task_b_stack[STACK_WORDS], task_b_entry, (void *)0x87654321u);

NVIC_SetPriority(PendSV_IRQn, 15);
NVIC_SetPriority(SVCall_IRQn, 15);

rtos_start();     /* 不返回 */

while (1) { }     /* 永远到不了 */
```

之后 `main` 的栈（MSP）只服务中断，`while(1)` 是永远到不了的死代码。

### `task_a_entry` / `task_b_entry` —— 任务本体

```c
static void task_a_entry(void *arg)
{
    char b[64];
    int  m;
    uint32_t cnt = 0;

    while (1)
    {
        cnt++;
        m = snprintf(b, sizeof(b), "A:%lu\r\n", (unsigned long)cnt);
        HAL_UART_Transmit(&huart1, (uint8_t*)b, m, HAL_MAX_DELAY);
        task_yield();
    }
}
```

`cnt` 是局部变量，编译器把它放进了 R4–R11 之一。它只存在于任务自己的软件帧里，是"上下文保存是否真的生效"最好的探针。

---

## 八、四个"不能这么写"

| 写法 | 后果 |
|---|---|
| 去掉 `__attribute__((naked))` | 编译器插入 `PUSH {R4-R11,LR}` 序言，你要存的现场已经被它改了 |
| PendSV 里 `BL` 调 C 函数 | `LR` 被覆盖，`BX LR` 变成跳回一个垃圾地址 |
| `LDR R0, =g_current` 不加 `.ltorg` | 文字池可能超过 ±4KB，链接报 `relocation truncated` |
| `sp` 不是 `tcb_t` 第一个成员 | 汇编里 `LDR R0,[R0]` 取到的是 `prio`，PSP 被设成 2 之类，HardFault |

另外两条 C 层面的坑（本周实际踩到）：

- **局部 `static` 遮蔽全局**：`while` 里再写一个 `static uint32_t g_pendsv_cnt;`，链接器会生成 `g_pendsv_cnt` 和 `g_pendsv_cnt.0` 两个符号，printf 读的和 handler 写的不是同一个。`-Wshadow` 能抓。
- **`__attribute__` 必须写进声明里**：单独一行 `__attribute__((aligned(8)));` 会被解析成空声明，报 `empty declaration`。

---

## 九、现在的系统能做什么、不能做什么

| 能做 | 不能做（下一阶段的目标） |
|---|---|
| 双任务交替执行 | 阻塞延时（`HAL_Delay` 还在忙等） |
| 上下文完整保存（验证了 5000+ 次） | 优先级（只有一个优先级） |
| 中断与任务共存（SysTick 正常） | 抢占（SysTick 不会触发切换） |
| 一次切换 = 14 条汇编 | 就绪队列 / 位图 / 任务创建 / 临界区 |

现在这个东西，准确的名字是：**协作式双任务轮转内核，带完整上下文保存**。

它离"RTOS"还差三样东西：**状态机、就绪队列、抢占**。而这三样，全部建立在今天这两条路上——`SVC` 造现场，`PendSV` 换现场。

### 已知的两个简化（不是 bug）

1. `g_current` 在挂起 PendSV **之前**就被改了。若此刻有更高优先级中断读它，会看到"还没切就变了"。现在没有这种读者；阶段 2 上 SysTick 抢占后必须把它放进临界区。
2. `task_yield` 无条件轮转，没有就绪队列、不看优先级、没有 `READY`/`RUNNING` 状态。

### 下一阶段的建议顺序

**阻塞延时 → 就绪位图 → SysTick 抢占 → 临界区**

理由：`HAL_Delay` 忙等是当前最刺眼的功能缺失，A 明明没事干还占着 CPU 空转 500ms。先做 `task_delay(ticks)`，会强迫引入三个新东西——任务状态、延时唤醒、`SysTick_Handler` 里的唤醒检查——**这三样正是阶段 2 的骨架**。做完阻塞，位图调度才有意义（现在只有两个任务，没有"选谁"的问题）。

---

## 附录 A：验证记录

| 阶段 | 实验 | 串口证据 | 结论 |
|---|---|---|---|
| 验证 0 | 手动置 `ICSR.PENDSVSET` | `pendsv_cnt` 从 0 变 1 | 向量表、handler、异常进出都通 |
| D3 | 打印 `stack_init` 造出的 16 字 | `[8]=0x12345678`、`[13]=0xFFFFFFFD`、`[14]=0x08001463`、`[15]=0x01000000` | 栈帧布局正确（`[14]` 末位 1 = Thumb 位） |
| D4 | `naked` handler + scratch 栈回读 | `scratch[56]=0x20000520`、`[59]=0x20004F80`，不再是 `0xAAAAAAAA` | `STMDB R0!,{R4-R11}` 确实执行，落点 `[56..63]` 正确 |
| D5 | SVC 启动第一个任务 | `task A started, arg=0x12345678` | 假帧被硬件弹出恢复，`main` 不再返回 |
| D6 | 双任务 `task_yield` 轮转 | `A:2514 / B:2514 / A:2515 / B:2515 …` 同步递增到 2500+ | 两个任务的上下文都完整保存，切换 5000+ 次无异常 |

**判读技巧**：`A:n` 和 `B:n` 编号必须**同步**递增。若出现 `A:2514 / B:2513` 这类差 1，说明某次 `yield` 被吞了（例如在 PendSV 里又调了 yield）；若编号原地打转，说明栈帧方向或 `sp` 偏移错了，任务每次都被恢复成初始帧。

---

## 附录 B：踩坑清单

按发生顺序：

1. **`stm32f1xx_it.c` 里的空 `SVC_Handler` / `PendSV_Handler` 会抢符号**。CubeMX 生成的弱定义和你的强定义冲突，或者你注释掉它之后忘记 `Generate` 会复活。改完必须确认链接到的是 `main.c` 那份（`nm` 或 `.map` 里看地址）。
2. **`ICSR` 打印成地址而不是值**。`snprintf(..., 0xE000ED04)` 传的是地址字面量，要写成 `*(volatile uint32_t *)0xE000ED04`。
3. **局部 `static` 遮蔽全局**。`.map` 里出现 `x` 和 `x.0` 两个符号就是它。
4. **`__attribute__` 单独一行**。`empty declaration`。
5. **`stack_init` 的参数签名不匹配**。`void (*)(uint32_t)` 和 `void (*)(void)` 是不同类型，统一成 `void (*)(void *)`。
6. **`for (inti i = 0; ...)`** 拼写，以及循环上界误写成 `STACK_WORDS`（128）而不是 16。
7. **`NVIC_SetPriority` 写在挂起之后**。本例碰巧能用，但是错误顺序；正确顺序是"设优先级 → 设 PSP → 挂起"。

---

## 附录 C：完整代码索引

全部在 `D:\STM32学习\MiniRTOS\Core\Src\main.c`：

| 内容 | 位置 |
|---|---|
| `#define STACK_WORDS` / `STACK_CANARY` | `USER CODE BEGIN PV` |
| `g_pendsv_cnt`、`g_current`、`g_prev` | `USER CODE BEGIN PV` |
| `typedef struct { uint32_t *sp; uint32_t prio; uint32_t state; } tcb_t;` | `USER CODE BEGIN PV` |
| `task_a_stack` / `task_b_stack`（带 `aligned(8)`） | `USER CODE BEGIN PV` |
| `tcb_t task_a; tcb_t task_b;` | `USER CODE BEGIN PV` |
| `stack_init()` | `USER CODE BEGIN PV` |
| `SVC_Handler()`（naked 汇编） | `USER CODE BEGIN 0` |
| `PendSV_Handler()`（naked 汇编） | `USER CODE BEGIN 0` |
| `task_a_entry()` / `task_b_entry()` | `USER CODE BEGIN 0` |
| `rtos_start()` / `task_yield()` | `USER CODE BEGIN 0` |
| `main()` 里的初始化与启动 | `USER CODE BEGIN 2` |

代码全部写在 `USER CODE x` 块内，CubeMX 重新 Generate 时不会被覆盖（但 `stm32f1xx_it.c` 里的同名 handler 会被恢复，需要注意）。
