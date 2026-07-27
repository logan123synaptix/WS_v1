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
#include "stdio.h"
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

static const char *TAG = "MAIN";

/* --- SIM (UART1) --- */
static volatile uint8_t uart_byte_sim;
static volatile uint8_t sim_rx_flag = 0;

/* --- GPS (UART2) --- */
static volatile uint8_t uart_byte_gps;
static volatile uint8_t gps_rx_flag = 0;

/* --- Terminal input, UART6/log, dung de go lenh AT forward sang SIM --- */
static volatile uint8_t uart_byte_log;
static volatile uint8_t log_rx_flag = 0;
static char     log_line_buf[128];
static uint16_t log_line_len = 0;

void power_on_sim(void);
void send_byte_sim(const char *cmd);
void send_byte_gps(const char *cmd);
void uart_test_init(void);
void uart_test_poll(void);
static void log_print(const char *s);

/* Ham "print" ma logger_init() se goi moi khi co 1 dong log da format
 * xong (xem logger.h's p_log_func) - o day chi don gian ghi thang ra
 * UART6, blocking, dung cho muc dich test cong cu nay. Day CUNG LA UART
 * dung lam CLI transport (uart_byte_log/log_rx_flag ben tren) - log
 * output va terminal input dung chung 1 day, giong thiet ke that cua
 * sx_board.c (khong phai bug, la co y). */
static void log_print(const char *s)
{
  HAL_UART_Transmit(&huart6, (uint8_t *)s, (uint16_t)strlen(s), 1000);
}

void power_on_sim(void)
{
  /* Pwrkey: PD12. Giu nguyen dung timing ban goc da viet (50ms/50ms/7s) -
   * CHUA xac nhan lai voi datasheet A7677S xem dung sequence PWRKEY hay
   * chua (bao nhieu ms low/high, active-high hay active-low) - ghi chu
   * lai vi day la timing tu viet, chua doi chieu voi Documents/a7677s.md */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);
  HAL_Delay(50);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_RESET);
  HAL_Delay(50);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);
  HAL_Delay(7000);
}

void send_byte_sim(const char *cmd)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)cmd, (uint16_t)strlen(cmd), 100);
}

void send_byte_gps(const char *cmd)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)cmd, (uint16_t)strlen(cmd), 100);
}

/* Goi 1 lan trong main(), SAU khi MX_USARTx_UART_Init() da chay het. */
void uart_test_init(void)
{
  logger_init(LOGGER_INFO, log_print);
  log_info(TAG, "=== UART TEST -- SIM(UART1) + GPS(UART2), CLI via UART6 ===");
}

/* Goi lien tuc trong vong while(1) chinh, KHONG block. */
void uart_test_poll(void)
{
  /* SIM -> log raw byte nhan duoc. Dung %02X (hex) thay vi %c/%s vi day
   * la 1 BYTE THO tu UART, co the la ky tu khong in duoc (vd 0x00) hoac
   * khong ket thuc bang '\0' - dua thang vao printf-style %s se khong an
   * toan. */
  if (sim_rx_flag) {
    sim_rx_flag = 0;
    log_info(TAG, "[SIM RX] 0x%02X ('%c')", uart_byte_sim,
             (uart_byte_sim >= 0x20 && uart_byte_sim < 0x7F) ? uart_byte_sim : '.');
  }

  /* GPS -> log raw byte nhan duoc (NMEA sentence tho, in tung byte) */
  if (gps_rx_flag) {
    gps_rx_flag = 0;
    log_info(TAG, "[GPS RX] 0x%02X ('%c')", uart_byte_gps,
             (uart_byte_gps >= 0x20 && uart_byte_gps < 0x7F) ? uart_byte_gps : '.');
  }

  /* Terminal (UART6) -> gom thanh 1 dong, forward sang SIM khi gap Enter */
  if (log_rx_flag) {
    log_rx_flag = 0;
    uint8_t c = uart_byte_log;

    /* Echo lai ky tu vua go, van dung HAL_UART_Transmit truc tiep (khong
     * qua log_info) vi day la echo ky tu don, khong phai 1 dong log co
     * cau truc - dung log_info o day se tu them newline/format khong
     * dung y do "go ky tu nao hien ra ky tu do". */
    HAL_UART_Transmit(&huart6, &c, 1, 100);

    if (c == '\r' || c == '\n') {
      if (log_line_len > 0) {
        log_line_buf[log_line_len] = '\0';
        send_byte_sim(log_line_buf);
        send_byte_sim("\r\n");

        log_info(TAG, "[-> SIM] %s", log_line_buf);

        log_line_len = 0;
      }
    } else if (log_line_len < sizeof(log_line_buf) - 1) {
      log_line_buf[log_line_len++] = (char)c;
    } else {
      log_line_len = 0; /* dong qua dai, reset thay vi tran buffer */
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
  HAL_UART_Receive_IT(&huart1, (uint8_t *)&uart_byte_sim, 1);
  HAL_UART_Receive_IT(&huart2, (uint8_t *)&uart_byte_gps, 1);
  HAL_UART_Receive_IT(&huart6, (uint8_t *)&uart_byte_log, 1);
  uint32_t last_tick = 0;
  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
  // sx_board_init();
  // app_init();
  uart_test_init();
  power_on_sim();      /* THEM MOI - bat nguon SIM truoc khi test AT command,
                           neu quen buoc nay SIM se khong tra loi gi ca du
                           UART da noi dung */     
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uart_test_poll();   
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // uint32_t now = HAL_GetTick();
    // uint32_t delta = now - last_tick;
    
    // if (delta > 0)  
    // {
    //     last_tick = now;
    //     app_process(delta);
    // }
    
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
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1) {
    sim_rx_flag = 1;
    HAL_UART_Receive_IT(&huart1, (uint8_t *)&uart_byte_sim, 1);
  } else if (huart->Instance == USART2) {
    gps_rx_flag = 1;
    HAL_UART_Receive_IT(&huart2, (uint8_t *)&uart_byte_gps, 1);
  } else if (huart->Instance == USART6) {
    log_rx_flag = 1;
    HAL_UART_Receive_IT(&huart6, (uint8_t *)&uart_byte_log, 1);
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