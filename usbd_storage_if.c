/* USB_DEVICE/App/usbd_storage_if.c — MSC that is "Not Ready" until MCU-A says ready */
#include "usbd_storage_if.h"
#include "spi_bridge_b.h"
#include <string.h>

/* We’ll cache media info after a successful query() */
static volatile uint8_t  g_media_ready = 0;
static volatile uint32_t g_block_cnt   = 0;
static volatile uint16_t g_block_sz    = 512;  /* default; updated by query */

/* Optional: re-query period (ticks) if you want to auto-detect hot-plug */
static uint32_t last_query_ms = 0;
static uint32_t query_period_ms = 500; /* query twice per second */

const int8_t STORAGE_Inquirydata_FS[] = {
  0x00, 0x80, 0x02, 0x02, (STANDARD_INQUIRY_DATA_LEN - 5), 0x00, 0x00, 0x00,
  'B','R','I','D','G','E',' ','B',
  'S','P','I','-','M','S','C',' ',
  'R','E','M','O','T','E',' ',' ',
  '0','.','0','1'
};

static void poll_media_once(void)
{
  uint32_t now = HAL_GetTick();
  if ((now - last_query_ms) < query_period_ms) return;
  last_query_ms = now;

  media_info_t mi;
  if (spi_b_query(&mi) == 0) {
    g_media_ready = mi.ready ? 1 : 0;
    if (g_media_ready) {
      g_block_cnt = mi.block_count;
      g_block_sz  = (mi.block_size ? mi.block_size : 512);
    }
  } else {
    g_media_ready = 0;
  }
}

int8_t STORAGE_Init_FS(uint8_t lun)
{
  (void)lun;
  g_media_ready = 0;
  g_block_cnt = 0;
  g_block_sz = 512;
  last_query_ms = 0;
  return USBD_OK;
}

int8_t STORAGE_GetCapacity_FS(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
  (void)lun;
  poll_media_once();
  if (!g_media_ready || g_block_cnt == 0) {
    /* Report a tiny capacity; host will retry later */
    *block_num = 1;
    *block_size = 512;
    return USBD_OK;
  }
  *block_num  = g_block_cnt;
  *block_size = g_block_sz;
  return USBD_OK;
}

int8_t STORAGE_IsReady_FS(uint8_t lun)
{
  (void)lun;
  poll_media_once();
  return g_media_ready ? USBD_OK : USBD_FAIL;  /* Not ready => no mount */
}

int8_t STORAGE_IsWriteProtected_FS(uint8_t lun)
{
  (void)lun;
  return USBD_OK;
}

int8_t STORAGE_Read_FS (uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
  (void)lun;
  if (!g_media_ready || blk_len == 0) return USBD_FAIL;

  /* Send header & get status */
  if (spi_b_read(blk_addr, blk_len, buf)) return USBD_FAIL;

  /* Now receive data payload from MCU-A (cnt * block_size) */
  size_t n = (size_t)blk_len * g_block_sz;
  if (HAL_SPI_Receive(&hspi1, buf, (uint16_t)n, HAL_MAX_DELAY) != HAL_OK) return USBD_FAIL;

  return USBD_OK;
}

int8_t STORAGE_Write_FS (uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
  (void)lun;
  if (!g_media_ready || blk_len == 0) return USBD_FAIL;

  if (spi_b_write(blk_addr, blk_len, buf)) return USBD_FAIL;

  /* Send the data payload */
  size_t n = (size_t)blk_len * g_block_sz;
  if (HAL_SPI_Transmit(&hspi1, buf, (uint16_t)n, HAL_MAX_DELAY) != HAL_OK) return USBD_FAIL;

  /* Final status from MCU-A */
  uint8_t s[2];
  if (HAL_SPI_Receive(&hspi1, s, 2, HAL_MAX_DELAY) != HAL_OK) return USBD_FAIL;
  if (s[0] != 'O' || s[1] != 0) return USBD_FAIL;

  return USBD_OK;
}

int8_t STORAGE_GetMaxLun_FS(void) { return 0; }

USBD_StorageTypeDef USBD_Storage_Interface_fops_FS =
{
  STORAGE_Init_FS,
  STORAGE_GetCapacity_FS,
  STORAGE_IsReady_FS,
  STORAGE_IsWriteProtected_FS,
  STORAGE_Read_FS,
  STORAGE_Write_FS,
  STORAGE_GetMaxLun_FS,
  (int8_t *)STORAGE_Inquirydata_FS
};
