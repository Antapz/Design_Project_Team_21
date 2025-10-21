#include "spi_bridge_b.h"
#include <string.h>

static inline uint8_t be_u32(uint32_t v, int i){ return (uint8_t)(v >> (8*(3-i))); }
static inline uint8_t be_u16(uint16_t v, int i){ return (i==0)? (uint8_t)(v>>8) : (uint8_t)v; }

static uint8_t tx(const uint8_t *p, size_t n){
  return (HAL_SPI_Transmit(&hspi1, (uint8_t*)p, (uint16_t)n, HAL_MAX_DELAY) == HAL_OK) ? 0 : 1;
}
static uint8_t rx(uint8_t *p, size_t n){
  return (HAL_SPI_Receive(&hspi1, p, (uint16_t)n, HAL_MAX_DELAY) == HAL_OK) ? 0 : 1;
}

uint8_t spi_b_init(void){
  /* SPI1 is already configured by CubeMX (Master). Nothing else needed. */
  return 0;
}

/* MCU-A protocol:
   'Q' -> MCU-A replies: 'Q','O', ready(1B), block_count(4B BE), block_size(2B BE)
*/
uint8_t spi_b_query(media_info_t *info){
  uint8_t q='Q', resp[1+1+1+4+2]; // id, 'O', ready, blkcnt, blksz
  if (tx(&q,1)) return 1;
  if (rx(resp,sizeof(resp))) return 1;
  if (resp[0] != 'Q' || resp[1] != 'O') return 1;
  info->ready = resp[2];
  info->block_count = ((uint32_t)resp[3]<<24)|((uint32_t)resp[4]<<16)|((uint32_t)resp[5]<<8)|resp[6];
  info->block_size  = ((uint16_t)resp[7]<<8)|resp[8];
  return 0;
}

/* 'R' lba(4B BE) cnt(2B BE) -> MCU-A: 'O',status(0=OK) then data(cnt*block_size) */
uint8_t spi_b_read(uint32_t lba, uint16_t cnt, uint8_t *buf){
  uint8_t hdr[1+4+2] = {'R', be_u32(lba,0),be_u32(lba,1),be_u32(lba,2),be_u32(lba,3), be_u16(cnt,0),be_u16(cnt,1)};
  uint8_t s[2];
  if (tx(hdr,sizeof(hdr))) return 1;
  if (rx(s,2)) return 1;
  if (s[0] != 'O' || s[1] != 0) return 1;
  /* caller does the final rx of data based on known block_size */
  /* We’ll let usbd_storage_if.c do the data rx because it knows cnt & blk sz */
  return 0;
}

/* 'W' lba(4B) cnt(2B) data(...) -> MCU-A: 'O',status */
uint8_t spi_b_write(uint32_t lba, uint16_t cnt, const uint8_t *buf){
  uint8_t hdr[1+4+2] = {'W', be_u32(lba,0),be_u32(lba,1),be_u32(lba,2),be_u32(lba,3), be_u16(cnt,0),be_u16(cnt,1)};
  uint8_t s[2];
  if (tx(hdr,sizeof(hdr))) return 1;
  /* data write happens in caller (immediately after header) */
  if (tx(buf, 0)) { /* no-op, ensures compiler keeps function when link-time opts */ }
  /* Final status will be read by caller */
  return 0;
}
