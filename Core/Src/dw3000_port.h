/**
 * @file    dw3000_port.h
 * @brief   STM32 HAL 포팅 레이어 (B-L4S5I-IOT01A + DWM3000EVB)
 *
 * 이 파일은 보드 의존적인 부분만 모아둡니다.
 * 핀/SPI 핸들은 CubeMX 설정에 맞게 아래 매크로를 수정하세요.
 */
#ifndef DW3000_PORT_H
#define DW3000_PORT_H

#include "main.h"   /* STM32 HAL + CubeMX 가 생성한 핀 라벨 */
#include <stdint.h>

/* ------------------------------------------------------------------ *
 * 1) SPI 핸들                                                         *
 *    CubeMX 에서 SPI1 을 활성화했다면 hspi1 을 사용합니다.            *
 * ------------------------------------------------------------------ */
extern SPI_HandleTypeDef hspi1;
#define DW3000_SPI            (&hspi1)

/* ------------------------------------------------------------------ *
 * 2) 제어 핀 (CubeMX 에서 GPIO_Output / GPIO_Input 으로 설정 후       *
 *    User Label 을 아래 이름으로 지정하면 그대로 동작합니다)         *
 *                                                                    *
 *    DW_CS_*   : SPI Chip Select  (Output, 기본 High)                *
 *    DW_RST_*  : DW3000 RSTn      (Output, Open-Drain 권장)          *
 *    DW_IRQ_*  : DW3000 IRQ       (Input, 폴링이면 미사용 가능)      *
 *    DW_WAKE_* : WAKEUP           (Output, 미사용 시 생략 가능)      *
 * ------------------------------------------------------------------ */
/* B-L4S5I-IOT01A 아두이노 헤더에 DWM3000EVB 실드를 꽂았을 때의 매핑 */
#ifndef DW_CS_GPIO_Port
#define DW_CS_GPIO_Port      GPIOA            /* ARD_D10 [SPI_SSN] */
#define DW_CS_Pin            GPIO_PIN_2
#endif

#ifndef DW_RST_GPIO_Port
#define DW_RST_GPIO_Port     GPIOA            /* ARD_D7 */
#define DW_RST_Pin           GPIO_PIN_4
#endif

#ifndef DW_IRQ_GPIO_Port
#define DW_IRQ_GPIO_Port     GPIOA            /* ARD_D9 */
#define DW_IRQ_Pin           GPIO_PIN_15
#endif

/* ------------------------------------------------------------------ *
 * 3) 포팅 함수 (dw3000_port.c 에 구현)                                *
 * ------------------------------------------------------------------ */
void     dw_port_cs_low(void);          /* CS = 0 (선택)   */
void     dw_port_cs_high(void);         /* CS = 1 (해제)   */
void     dw_port_hard_reset(void);      /* RSTn 펄스로 HW 리셋 */
void     dw_port_delay_ms(uint32_t ms); /* ms 지연         */
uint8_t  dw_port_irq_level(void);       /* IRQ 핀 레벨 읽기 */

/* SPI 송수신 (헤더 + 데이터). read 시 dataLen 만큼 수신, write 시 송신 */
int      dw_port_spi_read(const uint8_t *header, uint16_t headerLen,
                          uint8_t *readBuf, uint16_t readLen);
int      dw_port_spi_write(const uint8_t *header, uint16_t headerLen,
                           const uint8_t *bodyBuf, uint16_t bodyLen);

/* DW3000 은 초기(IDLE_RC 진입 전) 최대 2MHz, 이후 고속 SPI 가능.    */
/* 필요 시 CubeMX prescaler 변경 또는 런타임 baudrate 전환에 사용.   */
void     dw_port_spi_set_slow(void);    /* < 2 MHz  */
void     dw_port_spi_set_fast(void);    /* 고속      */

#endif /* DW3000_PORT_H */
