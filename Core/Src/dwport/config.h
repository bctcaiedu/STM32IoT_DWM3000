/**
 * @file    config.h
 * @brief   DW3000 플랫폼 보드 설정 (B-L4S5I-IOT01A + DWM3000EVB)
 *          br101/dw3000-decadriver-source 플랫폼 계층용.
 *
 *  우리가 디버깅으로 검증한 핀/SPI 설정.
 */
#ifndef DW3000_CONFIG_H
#define DW3000_CONFIG_H

#include "main.h"   /* STM32 HAL, hspi1 */

/* 사용할 칩: DW3110(DWM3000) */
#define CONFIG_DW3000_CHIP_DW3000   1

/* ---- SPI ----  SPI1, Mode 3 (CubeMX MX_SPI1_Init 에서 설정 완료) */
extern SPI_HandleTypeDef hspi1;
#define DW3000_HSPI                 (&hspi1)

/* CS = PA2 (ARD_D10 / SPI_SSN), GPIO 출력, idle High */
#define CONFIG_DW3000_SPI_CS_PORT   GPIOA
#define CONFIG_DW3000_SPI_CS_PIN    GPIO_PIN_2

/* 고속 SPI 목표 속도(MHz). 배선 정리 후 상향 가능. */
#define CONFIG_DW3000_SPI_MAX_MHZ   15

/* ---- 제어 핀 ---- */
/* RSTn = PA4 (ARD_D7), Open-Drain */
#define CONFIG_DW3000_GPIO_RESET_PORT  GPIOA
#define CONFIG_DW3000_GPIO_RESET_PIN   GPIO_PIN_4

/* IRQ = PA15 (ARD_D9), 입력. 폴링만 쓰면 EXTI 미사용 가능 */
#define CONFIG_DW3000_GPIO_IRQ_PORT    GPIOA
#define CONFIG_DW3000_GPIO_IRQ_PIN     GPIO_PIN_15
#define CONFIG_DW3000_GPIO_IRQ_EXTI    EXTI15_10_IRQn

/* WAKEUP 핀 미사용(-1 의미). 딥슬립 안 쓰면 불필요. */
/* #define CONFIG_DW3000_GPIO_WAKEUP_PORT  GPIOB */
/* #define CONFIG_DW3000_GPIO_WAKEUP_PIN   GPIO_PIN_2 */

#endif /* DW3000_CONFIG_H */
