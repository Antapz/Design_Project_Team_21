#ifndef SPI_BRIDGE_B_H
#define SPI_BRIDGE_B_H

#include "main.h"
#include "spi.h"
#include <stdint.h>
#include <stddef.h>

typedef struct {
  uint8_t  ready;         // 1 = media present & mounted
  uint32_t block_count;   // number of 512B blocks (or actual reported)
  uint16_t block_size;    // usually 512
} media_info_t;

uint8_t spi_b_init(void);
uint8_t spi_b_query(media_info_t *info);                       // 'Q' query
uint8_t spi_b_read(uint32_t lba, uint16_t cnt, uint8_t *buf);  // 'R' read
uint8_t spi_b_write(uint32_t lba, uint16_t cnt, const uint8_t *buf); // 'W' write

#endif
