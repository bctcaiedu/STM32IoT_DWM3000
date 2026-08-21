/**
 * @file    dw3000_hw.h
 * @brief   DW3000 HW 플랫폼 계층 (STM32 HAL) — 함수 원형
 */
#ifndef DW3000_HW_H
#define DW3000_HW_H

#include <stdbool.h>
#include <stdint.h>

int  dw3000_hw_init(void);
void dw3000_hw_fini(void);
void dw3000_hw_reset(void);

void dw3000_hw_wakeup(void);
void dw3000_hw_wakeup_pin_low(void);

int  dw3000_hw_init_interrupt(void);
/* EXTI2_IRQHandler(stm32l4xx_it.c) 에서 호출. IRQ 라인이 Low 로 떨어질 때까지
   dwt_isr() 를 반복 호출한다(DW3000 IRQ 는 레벨 방식). */
void dw3000_hw_isr(void);
/* EXTI2 진입 횟수. 0 이면 IRQ 배선/핀맵 문제 (배선 진단용). */
uint32_t dw3000_hw_get_irq_count(void);
void dw3000_hw_interrupt_enable(void);
void dw3000_hw_interrupt_disable(void);
bool dw3000_hw_interrupt_is_enabled(void);

#endif /* DW3000_HW_H */
