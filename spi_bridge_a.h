#ifndef SPI_BRIDGE_A_H
#define SPI_BRIDGE_A_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call once after init */
void SPIA_Init(void);

/* Call often in the while(1) loop to service incoming SPI commands */
void SPIA_Service(void);

#ifdef __cplusplus
}
#endif

#endif /* SPI_BRIDGE_A_H */
