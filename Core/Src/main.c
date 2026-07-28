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

static const char *TAG = "MAIN";

/* --- Ring buffer nhan cho SIM (UART1) va GPS (UART2) ---
 * LY DO CAN RING BUFFER (khong dung 1 bien + 1 co nhu ban truoc): o
 * baudrate 115200, moi byte chi mat ~87us. ISR HAL_UART_RxCpltCallback()
 * tu re-arm HAL_UART_Receive_IT() ngay sau khi nhan 1 byte de san sang
 * nhan byte tiep theo - neu module gui nhieu byte lien tiep khong nghi
 * (vd "OK\r\n"), va main loop (uart_test_poll(), goi tu while(1)) chua
 * kip doc/log byte cu TRUOC KHI ISR ghi de bang byte moi, thi byte cu se
 * MAT VINH VIEN. Day chinh la nguyen nhan quan sat duoc: oscilloscope
 * thay du xung (phan cung nhan dung), nhung code chi log lai duoc 1 phan
 * (vd "A" va "\n" trong "AT\r\n", mat "T" va "\r" o giua) vi buffer 1-byte
 * bi ghi de lien tuc truoc khi kip doc.
 *
 * Ring buffer giai quyet dung van de nay: ISR chi lam 1 viec toi thieu
 * (ghi byte vao buffer, tang chi so ghi) roi tra ve NGAY, khong cho main
 * loop kip lam gi ca - main loop se doc dan tu buffer o toc do cua rieng
 * no, khong bao gio bi ISR ghi de len du liieu chua doc. */
#define RING_BUF_SIZE 256   /* phai la luy thua cua 2 de phep '&' lam mod nhanh, khong dung '%' */

#define CMD_AT_TEST       "AT\r\n"   /* AT: lay IMEI, xem Documents/a76xx_at_cmd.md muc 2.2.19 */
#define CMD_AT_TEST_LABEL "AT"       /* ban khong co \r\n, chi dung de log cho gon, khong bi xuong dong giua chung */

typedef struct {
    volatile uint8_t buf[RING_BUF_SIZE];
    volatile uint16_t head; /* ISR ghi vao day */
    volatile uint16_t tail; /* main loop doc tu day */
} ring_buf_t;

static ring_buf_t sim_ring = {0};
static ring_buf_t gps_ring = {0};

static uint8_t sim_isr_byte; /* byte tam ISR nhan vao truoc khi day vao ring buffer */
static uint8_t gps_isr_byte;

static inline void ring_push(ring_buf_t *r, uint8_t byte)
{
    uint16_t next_head = (uint16_t)((r->head + 1) & (RING_BUF_SIZE - 1));
    if (next_head == r->tail) {
        /* Buffer day - byte nay bi mat (overflow). Khong nen xay ra voi
         * RING_BUF_SIZE=256 tru khi main loop bi block qua lau o dau do
         * khac - neu gap truong hop nay, tang RING_BUF_SIZE hoac tim cho
         * nao dang block main loop. */
        return;
    }
    r->buf[r->head] = byte;
    r->head = next_head;
}

/* Tra ve true neu doc duoc 1 byte (ghi vao *out), false neu buffer rong. */
static inline bool ring_pop(ring_buf_t *r, uint8_t *out)
{
    if (r->tail == r->head) {
        return false; /* rong */
    }
    *out = r->buf[r->tail];
    r->tail = (uint16_t)((r->tail + 1) & (RING_BUF_SIZE - 1));
    return true;
}

/* Gui "AT\r\n" xuong SIM lap lai moi AT_SPAM_INTERVAL_MS, vo thoi han
 * (khong dung lai du co nhan duoc phan hoi hay khong). */
#define AT_SPAM_INTERVAL_MS   2000
static uint32_t at_spam_last_tick = 0;

/* Buffer gom byte SIM RX thanh tung DONG hoan chinh de log 1 lan, thay vi
 * log tung byte hex rieng le (kho doc IMEI/phan hoi dai bang mat). Gom
 * cho toi khi gap '\n' thi in ca dong ra 1 lan, roi reset buffer. */
#define SIM_LINE_BUF_SIZE 128
static char     sim_line_buf[SIM_LINE_BUF_SIZE];
static uint16_t sim_line_len = 0;

void power_on_sim(void);
void send_byte_sim(const char *cmd);
void send_byte_gps(const char *cmd);
void uart_test_init(void);
void uart_test_poll(void);
static void log_print(const char *s);
static void sim_line_flush(void);

/* Ham "print" ma logger_init() se goi moi khi co 1 dong log da format
 * xong (xem logger.h's p_log_func) - o day chi don gian ghi thang ra
 * UART6, blocking, dung cho muc dich test cong cu nay. */
static void log_print(const char *s)
{
  HAL_UART_Transmit(&huart6, (uint8_t *)s, (uint16_t)strlen(s), 1000);
}

void power_on_sim(void)
{
  /* Pwrkey: PD12, active-LOW theo datasheet (Documents/a7677s.md, muc
   * 3.2.1 "Customer can power on the module by pulling down the PWRKEY
   * pin", pin duoc pull-up noi bo len VBAT san). Ton (do rong xung LOW)
   * toi thieu 50ms theo Table 14. */
  log_info(TAG, "POWER ON");
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET); /* keo LOW */
  HAL_Delay(50);
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_RESET);   /* tha ve HIGH */
  // HAL_Delay(8050);
  log_info(TAG, "POWER ON OK");
}

void send_byte_sim(const char *cmd)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)cmd, (uint16_t)strlen(cmd), 100);
}

void send_byte_gps(const char *cmd)
{
  HAL_UART_Transmit(&huart2, (uint8_t *)cmd, (uint16_t)strlen(cmd), 100);
}

/* In ra sim_line_buf hien tai (neu co gi de in) roi reset buffer, dung
 * chung cho ca truong hop gap '\n' lan buffer day (tranh tran). */
static void sim_line_flush(void)
{
  if (sim_line_len > 0) {
    sim_line_buf[sim_line_len] = '\0';
    log_info(TAG, "[SIM RX] %s", sim_line_buf);
    sim_line_len = 0;
  }
}

/* Goi 1 lan trong main(), SAU khi MX_USARTx_UART_Init() da chay het. */
void uart_test_init(void)
{
  logger_init(LOGGER_INFO, log_print);
  log_info(TAG, "=== UART TEST -- SIM(UART1) + GPS(UART2), spam AT moi %dms ===", AT_SPAM_INTERVAL_MS);

  at_spam_last_tick = HAL_GetTick() - AT_SPAM_INTERVAL_MS; /* gui ngay lan dau */
}

/* Goi lien tuc trong vong while(1) chinh, KHONG block. */
void uart_test_poll(void)
{
  uint8_t byte;

  /* Rut can toan bo byte SIM dang cho trong ring buffer moi lan poll,
   * gom thanh tung dong (ket thuc boi '\n') roi in ca dong 1 lan - de doc
   * hon nhieu so voi log tung byte hex rieng le, dac biet voi phan hoi
   * dai nhu IMEI. */
  while (ring_pop(&sim_ring, &byte)) {
    if (byte == '\n') {
      sim_line_flush();
    } else if (sim_line_len < SIM_LINE_BUF_SIZE - 1) {
      sim_line_buf[sim_line_len++] = (char)byte;
    } else {
      sim_line_flush(); /* dong qua dai, flush truoc khi mat du lieu */
    }
  }

  while (ring_pop(&gps_ring, &byte)) {
    log_info(TAG, "[GPS RX] 0x%02X ('%c')", byte,
             (byte >= 0x20 && byte < 0x7F) ? byte : '.');
  }

  /* Spam CMD_AT_TEST (AT+CGSN, lay IMEI) xuong SIM moi
   * AT_SPAM_INTERVAL_MS, vo thoi han. */
  if (HAL_GetTick() - at_spam_last_tick >= AT_SPAM_INTERVAL_MS) {
    at_spam_last_tick = HAL_GetTick();
    log_info(TAG, "[SIM TX] %s", CMD_AT_TEST_LABEL);
    send_byte_sim(CMD_AT_TEST);
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
  HAL_UART_Receive_IT(&huart1, &sim_isr_byte, 1);
  HAL_UART_Receive_IT(&huart2, &gps_isr_byte, 1);
  uint32_t last_tick = 0;
  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
  // sx_board_init();
  // app_init();
  uart_test_init();     /* THEM MOI - se tu dong spam "AT" moi 2s trong vong lap */
  power_on_sim();      /* THEM MOI - bat nguon SIM truoc khi test AT command */
  
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    uart_test_poll();   /* THEM MOI */
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
  /* Chi lam viec toi thieu trong ISR: day byte vao ring buffer roi
   * re-arm ngay lap tuc de san sang nhan byte tiep theo cang som cang
   * tot - khong lam gi ton thoi gian hon trong ISR (vd khong goi
   * log_info() o day, vi log_info() dung HAL_UART_Transmit blocking,
   * goi trong ISR se rat cham va co the lam mat byte UART khac dang
   * toi cung luc). */
  if (huart->Instance == USART1) {
    ring_push(&sim_ring, sim_isr_byte);
    HAL_UART_Receive_IT(&huart1, &sim_isr_byte, 1);
  } else if (huart->Instance == USART2) {
    ring_push(&gps_ring, gps_isr_byte);
    HAL_UART_Receive_IT(&huart2, &gps_isr_byte, 1);
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