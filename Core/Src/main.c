/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Smart Soap Mixing Workstation (h in Duty Percentage %)
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    STATE_IDLE = 0,
    STATE_MODE1,
    STATE_MODE2,
    STATE_MODE3,
    STATE_EMERGENCY
} WorkstationState_t;

typedef struct {
    uint8_t  heater_duty;     // 0 ~ 100 (%) (硬體 PWM 加熱百分比)
    uint8_t  soft_pwm_duty;   // 0 ~ 100 (%) (綠燈軟體 PWM 百分比)
    uint16_t blink_period_ms; // 綠燈閃爍週期 (ms)
    uint32_t duration_ms;     // 停留時間 (ms)
} StateConfig_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SOFT_PWM_PERIOD_MS    10    // 軟體 PWM 週期 (10ms)
#define DEBOUNCE_DELAY_MS     150   // 按鈕去彈跳時間 (ms)
#define RX_BUF_SIZE           64    // 串口接收緩衝區長度

#ifndef GREEN_LED_Pin
#define GREEN_LED_Pin GPIO_PIN_4
#define GREEN_LED_GPIO_Port GPIOA
#endif

#ifndef RED_LED_Pin
#define RED_LED_Pin GPIO_PIN_0
#define RED_LED_GPIO_Port GPIOB
#endif

#ifndef USER_BTN_Pin
#define USER_BTN_Pin GPIO_PIN_13
#define USER_BTN_GPIO_Port GPIOC
#endif
/* USER CODE END PD */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef hlpuart1;
TIM_HandleTypeDef htim3;

/* USER CODE BEGIN PV */
volatile WorkstationState_t g_system_state = STATE_IDLE;
uint32_t g_state_start_tick = 0;

uint8_t rx_byte = 0;
char rx_buf[RX_BUF_SIZE];
uint8_t rx_idx = 0;
char cmd_str[RX_BUF_SIZE];
volatile uint8_t g_cmd_flag = 0;

// 動態配方配置表 (h 改為 0~100%)
StateConfig_t g_state_lut[] = {
    [STATE_IDLE]      = { .heater_duty = 0,   .soft_pwm_duty = 5,   .blink_period_ms = 2000, .duration_ms = 0     },
    [STATE_MODE1]     = { .heater_duty = 30,  .soft_pwm_duty = 30,  .blink_period_ms = 1000, .duration_ms = 5000  },
    [STATE_MODE2]     = { .heater_duty = 60,  .soft_pwm_duty = 60,  .blink_period_ms = 500,  .duration_ms = 10000 },
    [STATE_MODE3]     = { .heater_duty = 100, .soft_pwm_duty = 100, .blink_period_ms = 200,  .duration_ms = 5000  },
    [STATE_EMERGENCY] = { .heater_duty = 0,   .soft_pwm_duty = 0,   .blink_period_ms = 0,    .duration_ms = 0     }
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_LPUART1_UART_Init(void);
static void MX_TIM3_Init(void);

/* USER CODE BEGIN PFP */
void Report_Status(void);
void Change_State(WorkstationState_t new_state);
void Process_FSM_Logic(uint32_t current_tick);
void Update_SoftPWM_And_Blink(uint32_t current_tick);
void Process_Command(char* cmd);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int __io_putchar(int ch) {
    HAL_UART_Transmit(&hlpuart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == LPUART1) {
        if (rx_byte == '\r' || rx_byte == '\n') {
            if (rx_idx > 0) {
                rx_buf[rx_idx] = '\0';
                strcpy(cmd_str, rx_buf);
                rx_idx = 0;
                g_cmd_flag = 1;
            }
        } else {
            if (rx_idx < (RX_BUF_SIZE - 1)) {
                rx_buf[rx_idx++] = rx_byte;
            }
        }
        HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == USER_BTN_Pin) {
        static uint32_t last_exti_tick = 0;
        uint32_t now = HAL_GetTick();

        if ((now - last_exti_tick) > DEBOUNCE_DELAY_MS) {
            if (HAL_GPIO_ReadPin(USER_BTN_GPIO_Port, USER_BTN_Pin) == GPIO_PIN_SET) {
                Change_State(STATE_EMERGENCY);
            }
            last_exti_tick = now;
        }
    }
}
/* USER CODE END 0 */

int main(void)
{
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_LPUART1_UART_Init();
  MX_TIM3_Init();

  /* USER CODE BEGIN 2 */
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
  HAL_UART_Receive_IT(&hlpuart1, &rx_byte, 1);

  printf("\r\n==================================================\r\n");
  printf(" STM32G474RE Soap Station (h & s in Percentage %%) \r\n");
  printf(" Format: m1=t,s%%,h%% | all=t1,s1%%,h1%%,t2,s2%%,h2%%...\r\n");
  printf("==================================================\r\n");

  g_state_start_tick = HAL_GetTick();
  Report_Status();
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  while (1)
  {
      uint32_t current_tick = HAL_GetTick();

      if (g_cmd_flag) {
          g_cmd_flag = 0;
          Process_Command(cmd_str);
      }

      Process_FSM_Logic(current_tick);
      Update_SoftPWM_And_Blink(current_tick);
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

void Report_Status(void) {
    const char *state_names[] = {"IDLE", "MODE1", "MODE2", "MODE3", "EMERGENCY"};
    StateConfig_t config = g_state_lut[g_system_state];

    printf("[UPDATE] State: %-9s | Heater: %3d%% | SoftPWM: %3d%% | Target Time: %lds\r\n",
           state_names[g_system_state],
           config.heater_duty,
           config.soft_pwm_duty,
           config.duration_ms / 1000);
}

void Change_State(WorkstationState_t new_state) {
    if (g_system_state != new_state) {
        g_system_state = new_state;
        g_state_start_tick = HAL_GetTick();
        Report_Status();
    }
}

void Process_FSM_Logic(uint32_t current_tick) {
    StateConfig_t config = g_state_lut[g_system_state];

    // 將 0~100% 換算為 TIM3 CCR 計數值 (100% -> 1000)
    uint16_t ccr_val = (uint16_t)config.heater_duty * 10;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, ccr_val);

    if (g_system_state == STATE_EMERGENCY) {
        HAL_GPIO_WritePin(RED_LED_GPIO_Port, RED_LED_Pin, GPIO_PIN_SET);
        return;
    } else {
        HAL_GPIO_WritePin(RED_LED_GPIO_Port, RED_LED_Pin, GPIO_PIN_RESET);
    }

    if (g_system_state != STATE_IDLE && config.duration_ms > 0) {
        if ((current_tick - g_state_start_tick) >= config.duration_ms) {
            switch (g_system_state) {
                case STATE_MODE1: Change_State(STATE_MODE2); break;
                case STATE_MODE2: Change_State(STATE_MODE3); break;
                case STATE_MODE3:
                    printf("\r\n[PROCESS DONE] Recipe execution completed!\r\n");
                    Change_State(STATE_IDLE);
                    break;
                default: break;
            }
        }
    }
}

void Process_Command(char* cmd) {
    if (g_system_state == STATE_EMERGENCY) {
        if (strcmp(cmd, "R") == 0 || strcmp(cmd, "r") == 0) {
            Change_State(STATE_IDLE);
        } else {
            printf("[WARN] Emergency Active! Command '%s' Ignored. Send 'R' to reset.\r\n", cmd);
        }
        return;
    }

    // 1. 單獨設定單一模式：m1=t,s,h 或 m2=t,s,h 或 m3=t,s,h
    if (strncmp(cmd, "m1=", 3) == 0 || strncmp(cmd, "m2=", 3) == 0 || strncmp(cmd, "m3=", 3) == 0) {
        uint32_t t, s, h;
        int mode_num = cmd[1] - '0';
        WorkstationState_t target_state = (mode_num == 1) ? STATE_MODE1 :
                                          (mode_num == 2) ? STATE_MODE2 : STATE_MODE3;

        char fmt[16];
        snprintf(fmt, sizeof(fmt), "m%d=%%lu,%%lu,%%lu", mode_num);

        if (sscanf(cmd, fmt, &t, &s, &h) == 3) {
            if (s > 100) s = 100;
            if (h > 100) h = 100; // 邊界限制為 100%

            g_state_lut[target_state].duration_ms = t * 1000;
            g_state_lut[target_state].soft_pwm_duty = (uint8_t)s;
            g_state_lut[target_state].heater_duty = (uint8_t)h;
            g_state_lut[target_state].blink_period_ms = (s > 0) ? (30000 / (s + 10)) : 2000;

            printf("\r\n[ACK] MODE%d Successfully Updated: t=%lus, s=%lu%%, h=%lu%%\r\n", mode_num, t, s, h);
        } else {
            printf("\r\n[ERR] Invalid Format! Example: m%d=5,30,30\r\n", mode_num);
        }
        return;
    }

    // 2. 一次設定全部模式：all=t1,s1,h1,t2,s2,h2,t3,s3,h3
    if (strncmp(cmd, "all=", 4) == 0) {
        uint32_t t1, t2, t3, s1, s2, s3, h1, h2, h3;

        int ret = sscanf(cmd, "all=%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu",
                         &t1, &s1, &h1, &t2, &s2, &h2, &t3, &s3, &h3);

        if (ret == 9) {
            if (s1 > 100) s1 = 100; if (s2 > 100) s2 = 100; if (s3 > 100) s3 = 100;
            if (h1 > 100) h1 = 100; if (h2 > 100) h2 = 100; if (h3 > 100) h3 = 100; // 邊界限制為 100%

            g_state_lut[STATE_MODE1].duration_ms = t1 * 1000;
            g_state_lut[STATE_MODE1].soft_pwm_duty = (uint8_t)s1;
            g_state_lut[STATE_MODE1].heater_duty = (uint8_t)h1;
            g_state_lut[STATE_MODE1].blink_period_ms = (s1 > 0) ? (30000 / (s1 + 10)) : 2000;

            g_state_lut[STATE_MODE2].duration_ms = t2 * 1000;
            g_state_lut[STATE_MODE2].soft_pwm_duty = (uint8_t)s2;
            g_state_lut[STATE_MODE2].heater_duty = (uint8_t)h2;
            g_state_lut[STATE_MODE2].blink_period_ms = (s2 > 0) ? (30000 / (s2 + 10)) : 2000;

            g_state_lut[STATE_MODE3].duration_ms = t3 * 1000;
            g_state_lut[STATE_MODE3].soft_pwm_duty = (uint8_t)s3;
            g_state_lut[STATE_MODE3].heater_duty = (uint8_t)h3;
            g_state_lut[STATE_MODE3].blink_period_ms = (s3 > 0) ? (30000 / (s3 + 10)) : 2000;

            printf("\r\n[ACK] ALL Modes Successfully Updated!\r\n");
            printf("  MODE1: %lus, s=%lu%%, h=%lu%%\r\n", t1, s1, h1);
            printf("  MODE2: %lus, s=%lu%%, h=%lu%%\r\n", t2, s2, h2);
            printf("  MODE3: %lus, s=%lu%%, h=%lu%%\r\n", t3, s3, h3);
        } else {
            printf("\r\n[ERR] Invalid Format! Example: all=6,15,20,6,50,60,6,100,100\r\n");
        }
        return;
    }

    // 3. 單字元動作指令
    if (strcmp(cmd, "S") == 0 || strcmp(cmd, "s") == 0) {
        if (g_system_state == STATE_IDLE) {
            Change_State(STATE_MODE1);
        } else {
            printf("[WARN] Process is already running!\r\n");
        }
    }
    else if (strcmp(cmd, "E") == 0 || strcmp(cmd, "e") == 0) {
        Change_State(STATE_EMERGENCY);
    }
    else if (strcmp(cmd, "0") == 0) {
        Change_State(STATE_IDLE);
    }
    else {
        printf("[WARN] Unknown Command: '%s'\r\n", cmd);
    }
}

void Update_SoftPWM_And_Blink(uint32_t current_tick) {
    if (g_system_state == STATE_EMERGENCY) {
        HAL_GPIO_WritePin(GREEN_LED_GPIO_Port, GREEN_LED_Pin, GPIO_PIN_RESET);
        return;
    }

    StateConfig_t config = g_state_lut[g_system_state];

    uint32_t blink_time = current_tick % config.blink_period_ms;
    uint8_t blink_active = (blink_time < (config.blink_period_ms / 2)) ? 1 : 0;

    uint32_t soft_pwm_time = current_tick % SOFT_PWM_PERIOD_MS;
    uint32_t threshold = (SOFT_PWM_PERIOD_MS * config.soft_pwm_duty) / 100;

    if (blink_active && (soft_pwm_time < threshold)) {
        HAL_GPIO_WritePin(GREEN_LED_GPIO_Port, GREEN_LED_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(GREEN_LED_GPIO_Port, GREEN_LED_Pin, GPIO_PIN_RESET);
    }
}

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
    RCC_OscInitStruct.PLL.PLLN = 85;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4);
}

static void MX_LPUART1_UART_Init(void) {
    hlpuart1.Instance = LPUART1;
    hlpuart1.Init.BaudRate = 115200;
    hlpuart1.Init.WordLength = UART_WORDLENGTH_8B;
    hlpuart1.Init.StopBits = UART_STOPBITS_1;
    hlpuart1.Init.Parity = UART_PARITY_NONE;
    hlpuart1.Init.Mode = UART_MODE_TX_RX;
    hlpuart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    hlpuart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    hlpuart1.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    hlpuart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    HAL_UART_Init(&hlpuart1);
}

static void MX_TIM3_Init(void) {
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    htim3.Instance = TIM3;
    htim3.Init.Prescaler = 169;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = 999;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&htim3);
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig);
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1);
    HAL_TIM_MspPostInit(&htim3);
}

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
    HAL_NVIC_SetPriority(LPUART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(LPUART1_IRQn);
}


/* USER CODE END 4 */

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
}
#endif /* USE_FULL_ASSERT */
