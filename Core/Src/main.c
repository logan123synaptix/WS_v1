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
#include "i2c.h"
#include "icache.h"
#include "lptim.h"
#include "rtc.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
// #include "app.h"
// #include "sx_board.h"
// #include "test_lte_mqtt.h"
#include "stdio.h"
#include "stdbool.h"
#include "string.h"
#include "logger.h"
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

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ============================================================
 * AT TERMINAL — go lenh AT tay qua UART6 (console/log, huart6), forward
 * sang UART1 (LTE modem, huart1), in nguyen van response tu modem nguoc
 * lai qua UART6. Muc dich: test tay AT+CSQ/AT+CREG?/... ngay sau khi
 * gap timeout hang loat trong chuoi network-attach, de xac dinh module
 * con song/tra loi AT co ban hay da treo han UART — xem trao doi voi
 * nguoi dung ve nghi van "module khong phan hoi UART giua chung dang
 * network attach".
 *
 * Thuan HAL, khong dung sx_board.c/sx_uart abstraction layer (nguoi dung
 * tu comment ISR ben sx_board.c de tranh 2 noi cung tranh IRQ tren cung
 * USART1/USART6). Cung pattern ring-buffer + line-gom da dung thanh cong
 * cho SIM/GPS test truoc day trong handoff cu.
 * ============================================================ */

#define AT_TERM_RING_SIZE   256U

typedef struct {
    volatile uint8_t  buf[AT_TERM_RING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} at_term_ring_t;

static at_term_ring_t s_cmd_ring;   /* UART6 RX (user typing a command)   */
static at_term_ring_t s_resp_ring;  /* UART1 RX (modem response)          */

static uint8_t s_cmd_rx_byte  = 0;
static uint8_t s_resp_rx_byte = 0;

static char     s_cmd_line_buf[128];
static uint16_t s_cmd_line_len = 0;

static char     s_resp_line_buf[256];
static uint16_t s_resp_line_len = 0;

const char *TAG = "MAIN";

void power_sim_on(void){
  log_info(TAG, "Start Power On");
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, 1);
  HAL_Delay(50);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, 0);
  HAL_Delay(7500);
  log_info(TAG, "Power On");
}

static void ring_push(at_term_ring_t *r, uint8_t b)
{
    uint16_t next = (uint16_t)((r->head + 1) % AT_TERM_RING_SIZE);
    if (next == r->tail) return; /* full, drop byte rather than overwrite */
    r->buf[r->head] = b;
    r->head = next;
}

static int ring_pop(at_term_ring_t *r, uint8_t *out)
{
    if (r->tail == r->head) return 0; /* empty */
    *out = r->buf[r->tail];
    r->tail = (uint16_t)((r->tail + 1) % AT_TERM_RING_SIZE);
    return 1;
}

/* Echo the modem's raw response, line by line, straight to the log UART.
 * No parsing — this is a passthrough terminal, not the real AT-command
 * layer (a7677s.c). */
static void resp_line_flush(void)
{
    if (s_resp_line_len == 0) return;
    s_resp_line_buf[s_resp_line_len] = '\0';
    log_info("AT_TERM", "[MODEM RX] %s", s_resp_line_buf);
    s_resp_line_len = 0;
}

/* Sends whatever the user typed (terminated by \r or \n on UART6) out to
 * the modem on UART1, appending \r\n since AT commands need it and the
 * user typing in a terminal usually only sends \r or \n alone depending
 * on their terminal's line-ending setting. */
static void cmd_line_send(void)
{
    if (s_cmd_line_len == 0) return;
    s_cmd_line_buf[s_cmd_line_len] = '\0';

    log_info("AT_TERM", "[TX] %s", s_cmd_line_buf);

    HAL_UART_Transmit(&huart1, (uint8_t *)s_cmd_line_buf, s_cmd_line_len, 100);
    HAL_UART_Transmit(&huart1, (uint8_t *)"\r\n", 2, 100);

    s_cmd_line_len = 0;
}

/* logger_init() requires a real print callback (void(*)(const char*)) —
 * cannot pass NULL. sx_board.c has its own log_print() using the
 * sx_uart_t abstraction layer, but this file runs pure HAL (no
 * sx_board_init() here, per user — avoiding double IRQ ownership on
 * USART1/USART6), so a plain HAL_UART_Transmit-based version is used
 * instead. */
static void at_term_log_print(const char *str)
{
    HAL_UART_Transmit(&huart6, (uint8_t *)str, (uint16_t)strlen(str), 100);
}

static void at_term_init(void)
{
    logger_init(LOGGER_INFO, at_term_log_print);
    log_info("AT_TERM", "=== AT TERMINAL (type AT commands, Enter to send) ===");
}

static void at_term_poll(void)
{
    uint8_t b;

    /* Drain modem RX (UART1) -> gom thanh dong, log ra UART6 */
    while (ring_pop(&s_resp_ring, &b)) {
        if (b == '\n') {
            resp_line_flush();
        } else if (b != '\r') {
            if (s_resp_line_len < sizeof(s_resp_line_buf) - 1) {
                s_resp_line_buf[s_resp_line_len++] = (char)b;
            }
        }
    }

    /* Drain user typing (UART6) -> gom thanh dong, gui sang UART1 */
    while (ring_pop(&s_cmd_ring, &b)) {
        /* Local echo so the user sees what they typed in the terminal. */
        HAL_UART_Transmit(&huart6, &b, 1, 10);
        if (b == '\r' || b == '\n') {
            cmd_line_send();
        } else if (s_cmd_line_len < sizeof(s_cmd_line_buf) - 1) {
            s_cmd_line_buf[s_cmd_line_len++] = (char)b;
        }
    }
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

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_USART3_UART_Init();
  MX_ICACHE_Init();
  MX_LPTIM1_Init();
  MX_RTC_Init();
  MX_TIM1_Init();
  MX_UART4_Init();
  MX_UART5_Init();
  MX_USART6_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_UART_Receive_IT(&huart6, &s_cmd_rx_byte, 1);
  HAL_UART_Receive_IT(&huart1, &s_resp_rx_byte, 1);
  uint32_t last_tick = 0;
  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
  // sx_board_init();
  // app_init();
  // test_lte_mqtt_init();
  power_sim_on();
  at_term_init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();
    uint32_t delta = now - last_tick;
    
    if (delta > 0)  
    {
        last_tick = now;
        // app_process(delta);
        // test_lte_mqtt_poll(delta);
        at_term_poll();
    }
    
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_LSI
                              |RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 16;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_1);
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_CKPER;
  PeriphClkInitStruct.CkperClockSelection = RCC_CLKPSOURCE_HSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* ISR-safe: only pushes to ring buffer + re-arms, no logging/processing
 * here (same discipline as the earlier SIM/GPS ring-buffer test — see
 * Bug 2 in the project handoff: a single-byte flag without a ring buffer
 * drops bytes at 115200 baud, ~87us/byte). */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        ring_push(&s_resp_ring, s_resp_rx_byte);
        HAL_UART_Receive_IT(&huart1, &s_resp_rx_byte, 1);
    } else if (huart->Instance == USART6) {
        ring_push(&s_cmd_ring, s_cmd_rx_byte);
        HAL_UART_Receive_IT(&huart6, &s_cmd_rx_byte, 1);
    }
}

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