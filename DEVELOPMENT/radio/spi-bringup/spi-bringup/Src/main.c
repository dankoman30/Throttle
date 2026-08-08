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
#include <string.h>
#include "main.h"
#include "spi.h"
#include "gpio.h"
#include "nrf24.h"
#include "throttle_protocol.h"
#include "crc8.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* Two-board TX/RX smoke test (DEVELOPMENT/radio/README.md bring-up step 3).
 * Set to 1, build, and flash the first ("TX") board - it sends a real,
 * protocol-valid throttle_packet_t (sync/seq/CRC8 all correct, flags=0)
 * every ~300ms, with the throttle field slowly triangle-sweeping between
 * IDLE_THROTTLE_VALUE and 200 so a receiving board's servo visibly moves -
 * an easy confirmation that packets are being received and VALIDATED
 * (sync/CRC8/sequence all passing), not just that SPI/radio transactions
 * are happening. Set to 0, rebuild, and flash the second ("RX") board -
 * it listens and stores each received counter in rx_last_counter/
 * rx_packet_count (this path still expects the OLD raw-counter-in-every-
 * byte format from before this file sent real packets; only meaningful
 * against another spi-bringup board in TX role, not receiver-prod).
 * Only flash/debug one board at a time; let the other free-run on USB or
 * battery power without an attached debug session. */
#define BRINGUP_ROLE_TX 1

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

  /* nRF24L01+ driver smoke test: nrf24_init() configures EN_AA, SETUP_RETR,
   * RF_CH, RF_SETUP, RX_ADDR_P0/TX_ADDR, and RX_PW_P0, then raises PWR_UP.
   * Confirmed on hardware, 2026-08-06 (see DEVELOPMENT/radio/README.md):
   *   - CONFIG:   0x0A (EN_CRC=1, PWR_UP=1, PRIM_RX=0) - our own write.
   *   - RF_SETUP: 0x24 (250kbps, PA_HIGH/-6dBm) - our own write.
   *   - RX_ADDR_P0 (5 bytes): {0xE7,0xE7,0xE7,0xE7,0xE7} - confirms
   *     multi-byte register writes/reads work too, not just single-byte. */
  nrf24_init();

  uint8_t config_val    = nrf24_read_reg(NRF24_REG_CONFIG);
  uint8_t rf_setup_val   = nrf24_read_reg(NRF24_REG_RF_SETUP);
  uint8_t rx_addr_p0[5]  = { 0 };
  nrf24_read_reg_n(NRF24_REG_RX_ADDR_P0, rx_addr_p0, 5);
  /* Never previously read back - checking these because MAX_RT (max
   * retransmits) is showing up on the TX board despite EN_AA/SETUP_RETR
   * both being written 0x00, which should make MAX_RT impossible. Expect
   * both 0x00 here; if not, the writes aren't landing as intended. */
  uint8_t en_aa_val      = nrf24_read_reg(NRF24_REG_EN_AA);
  uint8_t setup_retr_val = nrf24_read_reg(NRF24_REG_SETUP_RETR);

#if BRINGUP_ROLE_TX
  uint8_t tx_seq = 0;
  uint8_t tx_throttle = IDLE_THROTTLE_VALUE;
  int8_t  tx_sweep_dir = 1;
  /* nrf24_init() already leaves the chip in TX mode (PRIM_RX=0, CE low). */
#else
  uint32_t rx_packet_count = 0;
  uint8_t rx_last_counter = 0;
  nrf24_enter_rx_mode();
#endif

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if BRINGUP_ROLE_TX
    throttle_packet_t pkt;
    pkt.sync = PACKET_SYNC_BYTE;
    pkt.seq = tx_seq++;
    pkt.throttle = tx_throttle;
    pkt.flags = 0;
    pkt.crc8 = crc8_compute((const uint8_t *)&pkt, PACKET_CRC_LEN);

    uint8_t raw[PACKET_SIZE];
    memcpy(raw, &pkt, PACKET_SIZE);
    nrf24_send_payload(raw, (uint8_t)PACKET_SIZE);
    uint8_t tx_fifo_status = nrf24_read_reg(NRF24_REG_FIFO_STATUS);
    (void)tx_fifo_status; /* breakpoint here if debugging; TX_EMPTY (0x10) should be set after each send */

    /* Slow triangle sweep, step 4 every ~300ms (~15s per direction) - the
     * receiving board's servo moving is a visual confirmation that packets
     * are being received AND validated, not just that radio traffic is
     * happening at all. */
    if (tx_sweep_dir > 0) {
      if (tx_throttle >= 200) { tx_sweep_dir = -1; } else { tx_throttle += 4; }
    } else {
      if (tx_throttle <= IDLE_THROTTLE_VALUE) { tx_sweep_dir = 1; } else { tx_throttle -= 4; }
    }

    HAL_Delay(300);
#else
    if (nrf24_rx_available()) {
      uint8_t payload[NRF24_PAYLOAD_SIZE];
      nrf24_read_payload(payload, NRF24_PAYLOAD_SIZE);
      rx_last_counter = payload[0];
      rx_packet_count++; /* breakpoint here; expect rx_last_counter to climb by 1 each stop */
    }
#endif
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
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 16;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
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
