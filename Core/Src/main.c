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
#include "iwdg.h"
#include "lptim.h"
#include "rtc.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app.h"
#define TEST        0
#include "sx_board.h"
#include "sx_ex_storage.h"
#if TEST
#include "test_lte_mqtt.h"
#include "test_sht3x.h"
#include "test_rtc.h"
#include "test_imu.h"
#include "test_gps.h"
#include "test_exflash.h"
#include "test_ads1115.h"
#include "test_shell.h"
#include "test_sleep.h"
#include "test_sps30.h"
#include "test_ze12a.h"
#include "test_mux_select.h"
#endif

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
static void ensure_iwdg_frozen_in_stop_option_byte(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Ensures the FLASH_OPTR.IWDG_STOP option byte is set to "freeze" before
 * IWDG is ever started (MX_IWDG_Init(), called later in main()) --
 * without this, IWDG (LSI-clocked, ~30s timeout, see iwdg.c) keeps
 * counting straight through STOP mode, and since app.c's sleep cycle
 * parks the MCU in STOP for sleep_ms (e.g. 1800s), the device would
 * reset itself roughly every 30s while "asleep" -- entirely defeating
 * the point of a long sleep cycle. This is a one-time FLASH OPTION BYTE
 * (not a runtime register) -- it persists across power cycles and normal
 * reflashing, so on any board that already has it set correctly this
 * function reads the current value, sees it already matches, and returns
 * immediately without touching flash again. Only the very first boot on
 * a fresh/unprogrammed board (or one explicitly reset via mass erase)
 * actually performs the option byte write, and HAL_FLASH_OB_Launch()
 * below deliberately resets the MCU immediately to reload the new value
 * -- so on that first boot only, the board will appear to reset once
 * extra right after this call; every boot after that, this function is a
 * no-op. See "STM32L4 IWDG freeze in STOP mode" (ST community) for the
 * exact unlock/read-modify-write/launch sequence this mirrors, and the
 * common pitfall of forgetting HAL_FLASH_Unlock() before
 * HAL_FLASH_OB_Unlock() (the two locks are independent -- both are
 * required). Deliberately called before SystemClock_Config() in main()
 * (see the call site) so that if this does reset the MCU, it does so as
 * early and cheaply as possible rather than after clock/peripheral init
 * has already run. */
static void ensure_iwdg_frozen_in_stop_option_byte(void)
{
    FLASH_OBProgramInitTypeDef ob_current = {0};
    HAL_FLASHEx_OBGetConfig(&ob_current);

    /* ob_current.USERConfig is a bitmask covering ALL user option bytes
     * (IWDG_SW, WWDG_SW, nRST_STOP, nRST_STANDBY, IWDG_STOP,
     * IWDG_STANDBY, ...), not just the one this function cares about --
     * see FLASH_OBProgramInitTypeDef's doc-comment in
     * stm32h5xx_hal_flash_ex.h. Checking OB_USER_IWDG_STOP's bit alone
     * (not comparing the whole USERConfig word) is what lets this
     * function safely leave every other option byte (nRST_STOP, IWDG_SW,
     * etc) exactly as some other part of the provisioning process set
     * them, rather than silently reverting them to whatever this
     * function's own OBInit struct would otherwise imply. */
    /* BUG FIX (2026-08-10): this check used to mask ob_current.USERConfig
     * with OB_USER_IWDG_STOP (0x100, bit 8) instead of
     * FLASH_OPTSR_IWDG_STOP (bit 20). OB_USER_IWDG_STOP is the HAL's
     * *field-selector* namespace used only for OBProgram()'s USERType
     * argument -- it does NOT match the bit position ob_current.USERConfig
     * actually uses, because HAL_FLASHEx_OBGetConfig() fills USERConfig
     * straight from the raw FLASH->OPTSR_CUR register (see
     * FLASH_OB_GetUser() in stm32h5xx_hal_flash_ex.c), which uses real
     * hardware bit positions (FLASH_OPTSR_IWDG_STOP = bit 20). Masking
     * with the wrong bit (8) read an unrelated, essentially-always-zero
     * bit, so this check almost always read as "already FREEZE" and
     * returned early -- meaning the option byte write below never
     * actually ran, IWDG_STOP stayed at its power-on-default ACTIVE, and
     * IWDG kept counting straight through STOP mode, resetting the board
     * ~30s into every sleep regardless of sleep_ms. Confirmed on real
     * hardware: board reset immediately after ">>> Entering STOP mode
     * NOW" instead of sleeping for the configured 1800s. */
    if ((ob_current.USERConfig & FLASH_OPTSR_IWDG_STOP) == OB_IWDG_STOP_FREEZE) {
        /* Already frozen-in-STOP -- normal case on every boot after the
         * very first one on a given board. Nothing to do. */
        return;
    }

    if (HAL_FLASH_Unlock() != HAL_OK) {
        /* Cannot safely proceed with an option byte write without this
         * unlock -- HAL_FLASHEx_OBProgram() below would fail anyway.
         * Falling through to boot normally rather than looping/hanging
         * here: IWDG will behave as if this feature doesn't exist (keeps
         * counting through STOP, ~30s resets during sleep) until this
         * function can succeed on some later boot -- safer than blocking
         * boot entirely over a flash-unlock failure. */
        return;
    }
    if (HAL_FLASH_OB_Unlock() != HAL_OK) {
        HAL_FLASH_Lock();
        return;
    }

    FLASH_OBProgramInitTypeDef ob_write = {0};
    ob_write.OptionType = OPTIONBYTE_USER;
    /* USERType restricts the write to ONLY the IWDG_STOP field -- see
     * the doc-comment above ob_current's check for why this matters
     * (every other user option byte field is left completely untouched
     * by this call, regardless of what value we put in USERConfig for
     * bits outside this field). */
    ob_write.USERType   = OB_USER_IWDG_STOP;
    ob_write.USERConfig = OB_IWDG_STOP_FREEZE;

    HAL_StatusTypeDef program_status = HAL_FLASHEx_OBProgram(&ob_write);

    HAL_FLASH_OB_Lock();
    HAL_FLASH_Lock();

    if (program_status != HAL_OK) {
        /* Same reasoning as the unlock-failure branches above: leave
         * IWDG_STOP as whatever it currently is and continue booting
         * rather than blocking here. */
        return;
    }

    /* HAL_FLASH_OB_Launch() reloads the option bytes from flash into the
     * live FLASH_OPTR register -- this is the step that actually makes
     * the new IWDG_STOP value take effect, and it does so by resetting
     * the MCU immediately (documented HAL behaviour, confirmed by the ST
     * community thread this function's doc-comment references). This
     * call does not return under normal operation. */
    HAL_FLASH_OB_Launch();
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
  ensure_iwdg_frozen_in_stop_option_byte();
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
  MX_IWDG_Init();
  /* USER CODE BEGIN 2 */
  uint32_t last_tick = 0, last_ticks = 0;
  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
  sx_board_init();
  // sx_storage_factory_reset();
  #if TEST
  // test_ze12a_init();
  // test_gps_init();
  #else
  app_init();
  #endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint32_t now = HAL_GetTick();
    uint32_t noww = HAL_GetTick();
    uint32_t delta = now - last_tick;
    uint32_t theta = noww - last_ticks;

    if (delta > 0)  
    {
        last_tick = now;
        
        #if TEST
        // test_gps_poll(delta);
        #else
        app_process(delta);
        #endif
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