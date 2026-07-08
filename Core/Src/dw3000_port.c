/**
 * @file    dw3000_port.c
 * @brief   STM32 HAL 포팅 레이어 구현
 */
#include "dw3000_port.h"
#include <string.h>

/* SPI 타임아웃 (ms) */
#define DW_SPI_TIMEOUT   100U

/* 한 트랜잭션 최대 길이 (헤더 2 + 프레임 127 + 여유) */
#define DW_SPI_MAXLEN    140U

void dw_port_cs_low(void)
{
    HAL_GPIO_WritePin(DW_CS_GPIO_Port, DW_CS_Pin, GPIO_PIN_RESET);
}

void dw_port_cs_high(void)
{
    HAL_GPIO_WritePin(DW_CS_GPIO_Port, DW_CS_Pin, GPIO_PIN_SET);
}

void dw_port_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

uint8_t dw_port_irq_level(void)
{
    return (HAL_GPIO_ReadPin(DW_IRQ_GPIO_Port, DW_IRQ_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

/**
 * @brief  DW3000 하드웨어 리셋.
 *         RSTn 은 Open-Drain 으로, Low 로 끌어내렸다가 입력(Hi-Z)으로 풀어줍니다.
 *         (RSTn 을 강제로 High 로 구동하지 말 것 - 칩 내부에서 구동)
 */
void dw_port_hard_reset(void)
{
    GPIO_InitTypeDef g = {0};

    /* RSTn 을 출력(Open-Drain)으로 Low 구동 */
    g.Pin   = DW_RST_Pin;
    g.Mode  = GPIO_MODE_OUTPUT_OD;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DW_RST_GPIO_Port, &g);

    HAL_GPIO_WritePin(DW_RST_GPIO_Port, DW_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(2);   /* 최소 리셋 펄스 유지 */

    /* RSTn 을 다시 입력(Hi-Z)으로 풀어 칩이 자체 해제하게 함 */
    g.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(DW_RST_GPIO_Port, &g);

    HAL_Delay(5);   /* 부팅 + IDLE_RC 안정화 대기 */
}

int dw_port_spi_read(const uint8_t *header, uint16_t headerLen,
                     uint8_t *readBuf, uint16_t readLen)
{
    HAL_StatusTypeDef st;
    uint8_t txb[DW_SPI_MAXLEN];
    uint8_t rxb[DW_SPI_MAXLEN];
    uint16_t total = (uint16_t)(headerLen + readLen);

    if (total > DW_SPI_MAXLEN) return -1;

    /* 헤더 뒤에 더미(0x00) 채워 한 번에 연속 클럭 */
    memcpy(txb, header, headerLen);
    memset(txb + headerLen, 0x00, readLen);

    dw_port_cs_low();
    /* 풀듀플렉스 1회 전송: 헤더+데이터를 끊김 없이 클럭하고 동시에 수신.
       (Transmit→Receive 분리 시 RX FIFO 잔류로 바이트 정렬이 틀어짐) */
    st = HAL_SPI_TransmitReceive(DW3000_SPI, txb, rxb, total, DW_SPI_TIMEOUT);
    dw_port_cs_high();

    if (st != HAL_OK) return -1;

    /* 헤더 구간 수신은 버리고 데이터 구간만 복사 */
    memcpy(readBuf, rxb + headerLen, readLen);
    return 0;
}

int dw_port_spi_write(const uint8_t *header, uint16_t headerLen,
                      const uint8_t *bodyBuf, uint16_t bodyLen)
{
    HAL_StatusTypeDef st;

    dw_port_cs_low();
    st = HAL_SPI_Transmit(DW3000_SPI, (uint8_t *)header, headerLen, DW_SPI_TIMEOUT);
    if (st == HAL_OK && bodyLen > 0)
    {
        st = HAL_SPI_Transmit(DW3000_SPI, (uint8_t *)bodyBuf, bodyLen, DW_SPI_TIMEOUT);
    }
    dw_port_cs_high();

    return (st == HAL_OK) ? 0 : -1;
}

/* ----- (선택) SPI 속도 전환 --------------------------------------- *
 * CubeMX 의 기본 prescaler 를 그대로 쓰면 아래 함수는 비워두어도 됩니다.
 * DW3000 데이터시트상 초기화 구간은 ≤ 2MHz 권장.
 * 아래는 prescaler 만 바꿔 재초기화하는 예시입니다.
 * ------------------------------------------------------------------ */
void dw_port_spi_set_slow(void)
{
    /* 링잉/신호품질 문제 진단용으로 매우 낮게(≈0.94MHz, 120MHz/128).
       배선이 깨끗해지면 _32(3.75MHz) 등으로 올려도 됨. */
    DW3000_SPI->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
    HAL_SPI_Init(DW3000_SPI);
}

void dw_port_spi_set_fast(void)
{
    /* 점퍼선 신호품질을 고려해 우선 ≈7.5MHz(120MHz/16)로.
       배선 정리(짧게/직렬저항/GND 보강) 후 _8(15MHz),_4(30MHz)로 올려도 됨. */
    DW3000_SPI->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    HAL_SPI_Init(DW3000_SPI);
}
