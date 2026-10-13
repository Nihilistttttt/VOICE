/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32h417_it.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "ch32h417_it.h"
#include "debug.h"
#include "Serial.h"

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  uint32_t mepc   = __get_MEPC();
  uint32_t mcause = __get_MCAUSE();
  uint32_t mtval  = __get_MTVAL();

  Serial_Init(SERIAL_PORT_DEBUG);
  Serial_Printf(SERIAL_PORT_DEBUG, "\r\n[V3F HardFault] MEPC=0x%08lX MCAUSE=0x%08lX MTVAL=0x%08lX\r\n",
                (unsigned long)mepc, (unsigned long)mcause, (unsigned long)mtval);

  Delay_Ms(100);
  NVIC_SystemReset();
  while (1)
  {
  }
}


