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
#include "icache.h"
#include "usart.h"
#include "usb.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bootloader.h"
#include "tusb.h"
#include "stm32h5xx_hal.h"
#include "flash_define.h"
#include "boot_debug.h"
#include "logger.h"
#include "stdio.h"
#include "string.h"
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
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

UART_HandleTypeDef *log_uart = &huart3;

void bsp_log_send(const char *str){
  HAL_UART_Transmit(log_uart, str, strlen(str), 10);
}

__attribute__((optimize("O0"))) static void goto_application(volatile uint32_t address)
{
  BOOT_DEBUG("Gonna Jump to Application 0x%08X",(unsigned int)address);
  volatile void (*app_reset_handler)(void) = (void *)(*((volatile uint32_t *)(address + 4U)));
  // __disable_irq();
  /* Reset the Clock */

  /*Clear UART, ......, peripherals*/

  HAL_ICACHE_Disable();
  HAL_RCC_DeInit();
  HAL_DeInit();
  
  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL = 0;
  
  __set_MSP(*(volatile uint32_t *)address);
  /* Jump to application */
  SCB->VTOR = address;
  // __enable_irq();
  app_reset_handler(); // call the app reset handler
}

#define APP_START_ADDR      PRIMARY_APP_FLASH_START_ADDRESS   // bootloader occupies 0x08000000-0x0800FFFF (64KB)
#define APP_MAX_SIZE        PRIMARY_APP_FLASH_SIZE            // rest of bank 1, adjust to your map
// #define FLASH_SECTOR_SIZE   0x2000UL                       // 8KB sectors on H5
#define QUADWORD_SIZE       16U                               // 128-bit program width
 
static uint32_t _write_addr;                                  // next flash address to write
Bootloader_t *dfu_boot = NULL;

//--------------------------------------------------------------------
// TinyUSB DFU callbacks
//--------------------------------------------------------------------
 
void dfu_app_init(void)
{
  log_info("dfu_app_init");
  _write_addr = APP_START_ADDR;
}
 
// Called before DFU_DNLOAD is acked. Return time (ms) host should wait
// before sending GET_STATUS; use it to stretch out flash erase time.
uint32_t tud_dfu_get_timeout_cb(uint8_t alt, uint8_t state)
{
    (void) alt;
    log_info("tud_dfu_get_timeout_cb\r\n");
    if (state == DFU_DNBUSY) return 50; // erase/program time budget per block
    return 0;
}
 
// Invoked when DFU_DNLOAD request is received.
// Must call tud_dfu_finish_flashing() when done (can be immediate here
// since HAL_FLASH_Program is blocking).
void tud_dfu_download_cb(uint8_t alt, uint16_t block_num, uint8_t const *data, uint16_t length)
{
    (void) alt;
    log_info("tud_dfu_download_cb\r\n");
    if (block_num == 0)
    {
        dfu_app_init();
        HAL_FLASH_Unlock();
        FLASH_EraseInitTypeDef erase = {0};
        uint32_t sector_error = 0;
 
        erase.TypeErase   = FLASH_TYPEERASE_SECTORS;
        erase.Banks        = FLASH_BANK_1;               // adjust if APP_START_ADDR crosses into bank 2
        erase.Sector       = PRIMARY_APP_FLASH_START_ADDRESS / FLASH_SECTOR_SIZE;
        erase.NbSectors    = PRIMARY_APP_FLASH_NUMBER_OF_SECTORS;
 
        HAL_FLASHEx_Erase(&erase, &sector_error);
        HAL_FLASH_Lock();
    }
 
    if (_write_addr + length - APP_START_ADDR > APP_MAX_SIZE)
    {
        tud_dfu_finish_flashing(DFU_STATUS_ERR_ADDRESS);
        return;
    }
    // flash_write(data, length);
    boot_flash_write(&dfu_boot->boot_flash,_write_addr,data,length);
    _write_addr += length;
    tud_dfu_finish_flashing(DFU_STATUS_OK);
}
 
// Invoked when download process is complete, host sends DFU_DNLOAD with
// length 0. App now should complete the flashing process and reboot.
void tud_dfu_manifest_cb(uint8_t alt)
{
    (void) alt;
    log_info("tud_dfu_manifest_cb");
    // HAL_FLASH_Unlock();
    // flush_quadword(); // write any trailing partial quad-word
    // HAL_FLASH_Lock();
    dfu_boot->boot_flash.partition.isUpgradeInProgress = false;
    bootloader_save_partition(dfu_boot);
    // Tell TinyUSB we're done manifesting (no separate reboot needed since
    // DFU_ATTR_MANIFESTATION_TOLERANT is set — host can still GET_STATUS).
    tud_dfu_finish_flashing(DFU_STATUS_OK);
 
    // Give the host a moment to read the final GET_STATUS, then jump.
    HAL_Delay(200);
    bootloader_jump_to_application(dfu_boot);
    // NVIC_SystemReset();   // simplest: reset and let bootloader decide to
                           // jump to APP_START_ADDR based on valid vector table
}
 
// Invoked when receiving DFU_UPLOAD request, application should fill
// buffer of length and return the actual number of bytes available.
// Return 0 to stop further uploads (used for firmware read-back/verify).
uint16_t tud_dfu_upload_cb(uint8_t alt, uint16_t block_num, uint8_t *data, uint16_t length)
{
    (void) alt;
    log_info("tud_dfu_upload_cb\r\n");
    uint32_t addr = APP_START_ADDR + (uint32_t) block_num * length;
 
    if (addr >= APP_START_ADDR + APP_MAX_SIZE) return 0;
 
    uint32_t remain = (APP_START_ADDR + APP_MAX_SIZE) - addr;
    uint16_t n = (remain < length) ? (uint16_t) remain : length;
 
    memcpy(data, (void const *) addr, n);
    return n;
}
 
void tud_dfu_abort_cb(uint8_t alt)
{
    (void) alt;
    log_info("tud_dfu_abort_cb\r\n");
    dfu_app_init();
}
 
// Only used if CFG_TUD_DFU_RUNTIME is enabled instead of/alongside DFU mode.
void tud_dfu_detach_cb(void)
{
  log_info("tud_dfu_detach_cb\r\n");
  NVIC_SystemReset();
}

int read_boot_button(){
  return (int) 1;
}

void USB_DRD_FS_IRQHandler(void)
{
    tud_int_handler(0);
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

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

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
  MX_ICACHE_Init();
  MX_USART1_UART_Init();
  MX_USB_PCD_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */

  logger_init(LOGGER_DEBUG, bsp_log_send);

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  // tusb_init(BOARD_TUD_RHPORT);
  dfu_app_init();
  tusb_init();
  Bootloader_t bootloader;
  bootloader_init(&bootloader, goto_application,read_boot_button, &boot_flash_functions);
  dfu_boot = &bootloader;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    tud_task();
    bootloader_process(&bootloader);
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV2;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_PCLK3;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure the programming delay
  */
  __HAL_FLASH_SET_PROGRAM_DELAY(FLASH_PROGRAMMING_DELAY_0);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};
  MPU_Attributes_InitTypeDef MPU_AttributesInit = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region 0 and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x08FFF000;
  MPU_InitStruct.LimitAddress = 0x08FFFFFF;
  MPU_InitStruct.AttributesIndex = MPU_ATTRIBUTES_NUMBER0;
  MPU_InitStruct.AccessPermission = MPU_REGION_ALL_RO;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Attribute 0 and the memory to be protected
  */
  MPU_AttributesInit.Number = MPU_ATTRIBUTES_NUMBER0;
  MPU_AttributesInit.Attributes = INNER_OUTER(MPU_NOT_CACHEABLE);

  HAL_MPU_ConfigMemoryAttributes(&MPU_AttributesInit);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

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
    //  ex: log_info("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
