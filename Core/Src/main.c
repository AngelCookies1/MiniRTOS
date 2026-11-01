/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
#include <stdio.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
volatile uint32_t g_pendsv_cnt = 0;

#define STACK_WORDS 128
#define STACK_CANARY   0xDEADBEEFu

typedef struct {
  uint32_t *sp; /* 栈指针，切换时保存/恢复这个 */
  uint32_t prio; /* 优先级 */
  uint32_t state; /* 状态 */
} tcb_t;

//static uint32_t scratch_stack[64] __attribute__((aligned(8)));
static uint32_t task_a_stack[STACK_WORDS] __attribute__((aligned(8))); /*GCC/ARMCC 编译器指令**，告诉编译器：**把这个变量（或类型）的内存地址按 8 字节边界对齐*/
tcb_t task_a;
tcb_t *g_current;      /* 当前任务的 TCB 指针 */
static uint32_t *stack_init(uint32_t *stack_top, void (*entry)(void *), void *arg)
{
  uint32_t *sp = stack_top;

    *(--sp) = 0x01000000u;      /* xPSR  : 必须带 T 位(bit24)，否则首次取指就 UsageFault */
    *(--sp) = (uint32_t)entry;  /* PC    : 任务的入口 */
    *(--sp) = 0xFFFFFFFDu;      /* LR    : EXC_RETURN，返回线程模式用 PSP */
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
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile (
        "MRS   R0, PSP          \n"   /* 取当前任务的栈指针 */
        "STMDB R0!, {R4-R11}    \n"   /* 压入 r4-r11，R0 随之下移 32 字节 */
        "LDMIA R0!, {R4-R11}    \n"   /* 原样取回，R0 回到原处 */
        "MSR   PSP, R0          \n"
        "BX    LR               \n"   /* LR = 0xFFFFFFF9，硬件按 MSP 弹帧返回 */
    );
}

__attribute__((naked)) void SVC_Handler(void)
{
    __asm volatile (
        "MOVW  R0, #:lower16:g_current   \n"
        "MOVT  R0, #:upper16:g_current   \n"
        "LDR   R0, [R0]                  \n"   /* R0 = g_current (tcb_t*) */
        "LDR   R0, [R0]                  \n"   /* R0 = task_a.sp          */
        "LDMIA R0!, {R4-R11}             \n"   /* 装上软件帧，R0 指向 R0 槽 */
        "MSR   PSP, R0                   \n"
        "ORR   LR, LR, #0x0D             \n"   /* 0xFFFFFFF9 → 0xFFFFFFFD */
        "BX    LR                        \n"
    );
}

static void task_a_entry(void *arg)
{
  char b[64];
  int  m = snprintf(b, sizeof(b), "task A started, arg=0x%08lX\r\n", (unsigned long)arg);
  HAL_UART_Transmit(&huart1, (uint8_t *)b, m, HAL_MAX_DELAY);

  while(1) 
  {
    HAL_Delay(500);
    m = snprintf(b, sizeof(b), "task A alive, tick=%lu\r\n", (unsigned long)HAL_GetTick());
    HAL_UART_Transmit(&huart1, (uint8_t*)b, m, HAL_MAX_DELAY);
  }
}

void rtos_start(void)
{
    g_current = &task_a;
    __set_PSP(0);
    __DSB();
    __ISB();
    __asm volatile ("svc 0");    /* 触发 SVC，之后不会返回到这里 */
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  task_a.sp = stack_init(&task_a_stack[STACK_WORDS], task_a_entry, (void *)0x12345678u);

  NVIC_SetPriority(PendSV_IRQn, 15);
  NVIC_SetPriority(SVCall_IRQn, 15);

  rtos_start();  /* 进入 RTOS，之后不会返回到这里 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA0 PA1 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
