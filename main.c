#include "main.h"
#include "usb_host.h"
#include "usbh_hid.h"
#include <stdio.h>
#include <string.h>
#include "core_cm4.h"

#define SWO_BAUD_HZ  2000000u   /* 2 MHz */
#define USE_SWV_PRINTF 1

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
void MX_USB_HOST_Process(void);
static void SWV_Init_Auto(uint32_t swo_hz);

/* ---------- safe printf retarget to ITM Port 0 ---------- */
static inline void itm_putc(uint8_t c){
  if (c == '\n') { ITM_SendChar('\r'); ITM_SendChar('\n'); }
  else           { ITM_SendChar(c); }
}
int _write(int file, char *ptr, int len)
{
  (void)file;
#if USE_SWV_PRINTF
  for (int i = 0; i < len; i++) {
    uint8_t c = (uint8_t)ptr[i];
    /* printable ASCII or CR/LF/TAB */
    if ((c >= 0x20 && c <= 0x7E) || c == '\r' || c == '\n' || c == '\t') {
      itm_putc(c);
    } else {
      /* show non-printables as [xx] */
      char hex[5];
      snprintf(hex, sizeof(hex), "[%02X]", c);
      for (char *p = hex; *p; ++p) itm_putc((uint8_t)*p);
    }
  }
#endif
  return len;
}

/* --- helper: sign-extend 8-bit deltas --- */
static inline int s8_to_s32(uint8_t v) {
  return (v > 127) ? (int)v - 256 : (int)v;
}

/* USB Host HID event callback */
void USBH_HID_EventCallback(USBH_HandleTypeDef *phost)
{
  uint8_t type = USBH_HID_GetDeviceType(phost);

  if (type == HID_MOUSE) {
    HID_MOUSE_Info_TypeDef *mi = USBH_HID_GetMouseInfo(phost);
    if (mi) {
      /* dx, dy: signed deltas */
      int dx = s8_to_s32((uint8_t)mi->x);
      int dy = s8_to_s32((uint8_t)mi->y);

      /* da: scroll/wheel if available, else 0 */
      int da = 0;
      /* Uncomment the right one depending on your Cube version: */
      /* da = s8_to_s32((uint8_t)mi->z);      // if struct has z */
      /* da = s8_to_s32((uint8_t)mi->wheel);  // if struct has wheel */

      /* dz: button bitmask (L=1, R=2, M=4) */
      int dz = (mi->buttons[0] ? 1 : 0)
             | (mi->buttons[1] ? 2 : 0)
             | (mi->buttons[2] ? 4 : 0);

      /* Final format */
      printf("[ %d %d %d %d ]\n", dx, dy, da, dz);
    }
  }
  else if (type == HID_KEYBOARD) {
    HID_KEYBD_Info_TypeDef *ki = USBH_HID_GetKeybdInfo(phost);
    if (ki) {
      char ascii = USBH_HID_GetASCIICode(ki);
      if (ascii) printf("Key: '%c'\n", ascii);
      else       printf("Key: 0x%02X (non-ASCII)\n", ki->keys[0]);
    }
  }
}

int main(void)
{
  HAL_Init();
  SystemClock_Config();          /* ~96 MHz SYSCLK, USB 48 MHz */

  MX_GPIO_Init();
  setvbuf(stdout, NULL, _IONBF, 0);

  SWV_Init_Auto(SWO_BAUD_HZ);    /* SWO = 2 MHz, ITM Port 0 */

  MX_USB_HOST_Init();

  SystemCoreClockUpdate();
  printf("\n=== USB Host HID demo (mouse+keyboard) ===\n");
  printf("SystemCoreClock = %lu Hz\n", SystemCoreClock);
  printf("SWV: Start trace in SWV ITM Console (Port 0), SWO=%lu Hz\n",
         (unsigned long)SWO_BAUD_HZ);

  while (1) {
    MX_USB_HOST_Process();
  }
}

/* 96 MHz SYSCLK; USB 48 MHz */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM       = 7;
  RCC_OscInitStruct.PLL.PLLN       = 336;
  RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV4;  /* 384/4 = 96 MHz */
  RCC_OscInitStruct.PLL.PLLQ       = 8;              /* 384/8 = 48 MHz for USB */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|
                                     RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK) { Error_Handler(); }
}

static void MX_GPIO_Init(void)
{
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
}

/* ---- SWV init: runtime HCLK, Port 0 only ---- */
static void SWV_Init_Auto(uint32_t swo_hz)
{
  SystemCoreClockUpdate();

  /* Enable trace port (PB3 = SWO with Serial Wire debug) */
  DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN;

  /* Enable trace in core debug */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

  /* Unlock ITM */
  ITM->LAR = 0xC5ACCE55;
  ITM->TCR = 0;
  ITM->TER = 0;

  /* TPIU prescaler */
  TPI->ACPR = (SystemCoreClock / swo_hz) - 1U;

  /* SWO NRZ (UART) mode */
  TPI->SPPR = 2U;

  /* Formatter & flush control */
  TPI->FFCR = 0x00000100U;

  /* Enable ITM, SWO, TraceBusID=1 */
  ITM->TCR = (1U << 0) | (1U << 3) | (1U << 16);

  /* Enable ONLY port 0 */
  ITM->TER = 1U;
}

void Error_Handler(void)
{
  __disable_irq();
  while (1) { }
}
