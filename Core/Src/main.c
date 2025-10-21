/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : USB HID + LinkLayer (Hamming+Manchester, ACK/NACK) demo
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_host.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "usbh_core.h"
#include "usbh_hid.h"
#include "link_layer.h"   /* ll_send_with_ack_prof + cycles_to_us_u32 */
#include "swv_trace.h"    /* SWV_Init_Auto() */
/* USER CODE END Includes */

/* Private variables ---------------------------------------------------------*/
UART_HandleTypeDef huart2;
/* USER CODE BEGIN PV */
extern ApplicationTypeDef Appli_state;     /* from usb_host.c */
extern USBH_HandleTypeDef hUsbHostFS;      /* from usb_host.c */

static uint8_t seq_ll = 0;                 /* ARQ sequence */
static uint8_t raw_buf[64];                /* payload buffer */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
void MX_USB_HOST_Process(void);
/* USER CODE BEGIN PFP */
static int hid_get_payload(uint8_t *out, int cap);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* printf -> SWV ITM (Port 0) */
static inline void itm_putc(uint8_t c){
  if (c=='\n') { ITM_SendChar('\r'); ITM_SendChar('\n'); }
  else         { ITM_SendChar(c); }
}
int _write(int file, char *ptr, int len){
  (void)file;
  for(int i=0;i<len;i++){
    uint8_t c=(uint8_t)ptr[i];
    if ((c>=0x20 && c<=0x7E)||c=='\r'||c=='\n'||c=='\t') itm_putc(c);
  }
  return len;
}

/* Return 1B key payload or 4B mouse payload; 0 if no activity */
static int hid_get_payload(uint8_t *out, int cap)
{
  if (!cap) return 0;
  uint8_t type = USBH_HID_GetDeviceType(&hUsbHostFS);
  if (type == HID_KEYBOARD) {
    HID_KEYBD_Info_TypeDef *k = USBH_HID_GetKeybdInfo(&hUsbHostFS);
    if (k) {
      char c = USBH_HID_GetASCIICode(k);
      if (c) { out[0]=(uint8_t)c; return 1; }
    }
  } else if (type == HID_MOUSE) {
    HID_MOUSE_Info_TypeDef *m = USBH_HID_GetMouseInfo(&hUsbHostFS);
    if (m && cap>=4) {
      out[0] = (m->buttons[0]?1:0) | (m->buttons[1]?2:0) | (m->buttons[2]?4:0);
      out[1] = (uint8_t)m->x;
      out[2] = (uint8_t)m->y;
      out[3] = 0; /* wheel if needed */
      return 4;
    }
  }
  return 0;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();

  MX_GPIO_Init();
  MX_USB_HOST_Init();
  // MX_USART2_UART_Init();   /* not needed for SWV */

  /* SWV + timing */
  setvbuf(stdout,NULL,_IONBF,0);
  SWV_Init_Auto(2000000u);
  timing_init();

  printf("\n=== HID Host + LinkLayer (Hamming+Manchester, no CRC) ===\n");
  ll_loopback_set_nack_period(5);  /* optional: force NACK every 5th frame */

  /* Infinite loop */
  while (1)
  {
    MX_USB_HOST_Process();

    if (Appli_state == APPLICATION_READY) {
      int n = hid_get_payload(raw_buf, (int)sizeof raw_buf);
      if (n <= 0) { HAL_Delay(5); continue; }

      size_t   tx_bytes = 0;
      ll_prof_t P;
      timing_reset();

      int ok = ll_send_with_ack_prof(raw_buf, (uint16_t)n, &seq_ll,
                                     ll_loopback_tx, ll_loopback_rx,
                                     50 /*ms*/, 3 /*retries*/,
                                     &tx_bytes, &P);

      /* Convert cycles -> integer microseconds (no float printf needed) */
      uint32_t us_total = cycles_to_us_u32(P.cyc_total,     SystemCoreClock);
      uint32_t us_frame = cycles_to_us_u32(P.cyc_frame,     SystemCoreClock);
      uint32_t us_hamm  = cycles_to_us_u32(P.cyc_hamm_enc,  SystemCoreClock);
      uint32_t us_manc  = cycles_to_us_u32(P.cyc_manc_enc,  SystemCoreClock);
      uint32_t us_tx    = cycles_to_us_u32(P.cyc_tx,        SystemCoreClock);
      uint32_t us_wait  = cycles_to_us_u32(P.cyc_wait_ack,  SystemCoreClock);
      uint32_t us_ackmd = cycles_to_us_u32(P.cyc_ack_mdec,  SystemCoreClock);
      uint32_t us_ackhd = cycles_to_us_u32(P.cyc_ack_hdr,   SystemCoreClock);

      printf("[HID] payload=%dB seq=%u manc_tx=%uB result=%s "
             "total=%luus | frame=%luus ham=%luus manc=%luus tx=%luus wait=%luus ack_dec=%luus ack_hdr=%luus\n",
             n, (unsigned)(seq_ll - (ok?1:0)), (unsigned)tx_bytes,
             ok?"ACK":"FAIL",
             (unsigned long)us_total, (unsigned long)us_frame, (unsigned long)us_hamm,
             (unsigned long)us_manc, (unsigned long)us_tx, (unsigned long)us_wait,
             (unsigned long)us_ackmd, (unsigned long)us_ackhd);
    } else {
      static uint32_t tick = 0;
      uint32_t now = HAL_GetTick();
      if (now - tick > 250) { tick = now; printf("Waiting for HID device...\n"); }
    }
  }
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;   /* 48MHz USB FS */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }

  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|
                                RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }
}

/* Optional UART init; safe to keep even if unused */
static void MX_USART2_UART_Init(void)
{
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK) { Error_Handler(); }
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET);
  GPIO_InitStruct.Pin = GPIO_PIN_0;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1) { }
}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  (void)file; (void)line;
}
#endif
