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
#include "ch32h417_ipc.h"

#define V5F_FAULT_MAGIC  0xDEADF00Du

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

  uint32_t mtval  = __get_MTVAL();

  IPC_WriteMSG(IPC_MSG2, V5F_FAULT_MAGIC);
  IPC_WriteMSG(IPC_MSG3, mepc);
  IPC_WriteMSG(IPC_MSG1, mtval);
  __asm volatile ("fence iorw, iorw" ::: "memory");
  NVIC_SystemReset();
  while (1)
  {
  }
}


