/*
 * swv_trace.c
 */
#include "stm32f4xx.h"  /* DBGMCU/TPI/ITM/CoreDebug defs for F4 */
#include "swv_trace.h"

void SWV_Init_Auto(uint32_t swo_hz)
{
    SystemCoreClockUpdate();

    /* Enable SWO trace pins + core trace */
    DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    /* Unlock + disable before configure */
    ITM->LAR = 0xC5ACCE55;
    ITM->TCR = 0;
    ITM->TER = 0;

    /* TPIU prescaler for desired SWO speed */
    TPI->ACPR = (SystemCoreClock / swo_hz) - 1U;
    TPI->SPPR = 2U;           /* NRZ / async SWO */
    TPI->FFCR = 0x00000100U;  /* continuous formatting */

    /* Enable ITM: ITMENA, SWOENA, TraceBusID=1; enable stimulus port 0 */
    ITM->TCR = (1U<<0) | (1U<<3) | (1U<<16);
    ITM->TER = 1U;
}
