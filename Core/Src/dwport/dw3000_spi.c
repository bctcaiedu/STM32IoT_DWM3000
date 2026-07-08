/**
 * @file    dw3000_spi.c
 * @brief   DW3000 SPI 플랫폼 계층 (STM32L4 HAL / B-L4S5I-IOT01A)
 *          br101/dw3000-decadriver-source 의 nrf-sdk dw3000_spi.c 를
 *          STM32 HAL + 우리 검증 설정으로 이식.
 *
 *  검증값: SPI1, Mode 3, CS=PA2. 읽기는 STM32L4 RX-FIFO 정렬문제 회피를 위해
 *          헤더+더미를 한 버퍼에 담아 풀듀플렉스(TransmitReceive) 1회로 수행.
 */
#include <string.h>

#include "deca_device_api.h"   /* DWT_SUCCESS/DWT_ERROR, decaIrqStatus_t */
#include "dw3000_spi.h"
#include "config.h"
#include "log.h"

#define DW_SPI            DW3000_HSPI
#define DW_SPI_TIMEOUT    100U
#define DW_SPI_MAXLEN     256U      /* 헤더+본문 최대. 필요 시 키울 것 */

/* deca_port.c 제공 */
extern decaIrqStatus_t decamutexon(void);
extern void            decamutexoff(decaIrqStatus_t s);

static inline void cs_low(void)
{
    HAL_GPIO_WritePin(CONFIG_DW3000_SPI_CS_PORT, CONFIG_DW3000_SPI_CS_PIN, GPIO_PIN_RESET);
}
static inline void cs_high(void)
{
    HAL_GPIO_WritePin(CONFIG_DW3000_SPI_CS_PORT, CONFIG_DW3000_SPI_CS_PIN, GPIO_PIN_SET);
}

/* ------------------------------------------------------------------ */
int dw3000_spi_init(void)
{
    /* SPI 주변장치는 CubeMX MX_SPI1_Init 에서 Mode 3 로 이미 초기화됨.
       여기서는 CS 를 idle High 로 보장하고 저속으로 시작. */
    GPIO_InitTypeDef g = {0};
    g.Pin   = CONFIG_DW3000_SPI_CS_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CONFIG_DW3000_SPI_CS_PORT, &g);
    cs_high();

    dw3000_spi_speed_slow();
    LOG_INF("SPI init (CS=PA2, Mode0)");
    return 0;
}

void dw3000_spi_fini(void) { /* no-op */ }

/* APB2=120MHz 기준 prescaler. /64 ≈ 1.9MHz, /8 ≈ 15MHz, /4 = 30MHz */
void dw3000_spi_speed_slow(void)
{
    /* OTP/PLL 읽기 신뢰성 테스트용으로 더 느리게(≈0.94MHz, 120MHz/128) */
    DW_SPI->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
    HAL_SPI_Init(DW_SPI);
}

void dw3000_spi_speed_fast(void)
{
#if (CONFIG_DW3000_SPI_MAX_MHZ >= 30)
    DW_SPI->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;    /* 30MHz */
#elif (CONFIG_DW3000_SPI_MAX_MHZ >= 15)
    DW_SPI->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;    /* 15MHz */
#else
    DW_SPI->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;   /* 7.5MHz */
#endif
    HAL_SPI_Init(DW_SPI);
}

/* ------------------------------------------------------------------ *
 * write : 헤더 + 본문 연속 송신
 * ------------------------------------------------------------------ */
int32_t dw3000_spi_write(uint16_t headerLength, const uint8_t* headerBuffer,
                         uint16_t bodyLength,   const uint8_t* bodyBuffer)
{
    static uint8_t txb[DW_SPI_MAXLEN];
    uint16_t total = (uint16_t)(headerLength + bodyLength);
    decaIrqStatus_t stat;
    HAL_StatusTypeDef st;

    if (total > DW_SPI_MAXLEN) return DWT_ERROR;

    /* 헤더+바디를 한 버퍼에 이어붙여 '한 번의 연속 전송'으로.
       Transmit 를 두 번 나누면 그 사이 간극에 DW3000 이 데이터를 잘못 샘플링함
       (= 2-octet 쓰기 깨짐 → OTP/PLL 실패의 근본원인이었음) */
    stat = decamutexon();
    memcpy(txb, headerBuffer, headerLength);
    if (bodyLength > 0U) memcpy(txb + headerLength, bodyBuffer, bodyLength);

    cs_low();
    st = HAL_SPI_Transmit(DW_SPI, txb, total, DW_SPI_TIMEOUT);
    cs_high();

    decamutexoff(stat);
    return (st == HAL_OK) ? DWT_SUCCESS : DWT_ERROR;
}

/* ------------------------------------------------------------------ *
 * read : 헤더+더미를 한 번에 풀듀플렉스로 클럭, 데이터 구간만 복사
 *        (STM32L4 2LINES 에서 Transmit→Receive 분리 시 정렬 깨짐)
 * ------------------------------------------------------------------ */
int32_t dw3000_spi_read(uint16_t headerLength, uint8_t* headerBuffer,
                        uint16_t readLength,   uint8_t* readBuffer)
{
    static uint8_t txb[DW_SPI_MAXLEN];
    static uint8_t rxb[DW_SPI_MAXLEN];
    uint16_t total = (uint16_t)(headerLength + readLength);
    decaIrqStatus_t stat;
    HAL_StatusTypeDef st;

    if (total > DW_SPI_MAXLEN) return DWT_ERROR;

    stat = decamutexon();
    memcpy(txb, headerBuffer, headerLength);
    memset(txb + headerLength, 0x00, readLength);

    cs_low();
    st = HAL_SPI_TransmitReceive(DW_SPI, txb, rxb, total, DW_SPI_TIMEOUT);
    cs_high();
    decamutexoff(stat);

#if DW3000_LOG_ENABLE
    {
        extern int g_dwspi_dbg;
        if (g_dwspi_dbg < 12) {
            printf("RD hl=%u rl=%u hdr=%02X %02X | rxb[0..6]=%02X %02X %02X %02X %02X %02X %02X\r\n",
                   headerLength, readLength,
                   headerBuffer[0], (headerLength > 1) ? headerBuffer[1] : 0,
                   rxb[0], rxb[1], rxb[2], rxb[3], rxb[4], rxb[5], rxb[6]);
            g_dwspi_dbg++;
        }
    }
#endif

    if (st != HAL_OK) return DWT_ERROR;
    memcpy(readBuffer, rxb + headerLength, readLength);
    return DWT_SUCCESS;
}

int g_dwspi_dbg = 0;

int32_t dw3000_spi_write_crc(uint16_t headerLength, const uint8_t* headerBuffer,
                             uint16_t bodyLength,   const uint8_t* bodyBuffer,
                             uint8_t crc8)
{
    decaIrqStatus_t stat = decamutexon();
    HAL_StatusTypeDef st;

    cs_low();
    st = HAL_SPI_Transmit(DW_SPI, (uint8_t*)headerBuffer, headerLength, DW_SPI_TIMEOUT);
    if (st == HAL_OK && bodyLength > 0U)
        st = HAL_SPI_Transmit(DW_SPI, (uint8_t*)bodyBuffer, bodyLength, DW_SPI_TIMEOUT);
    if (st == HAL_OK)
        st = HAL_SPI_Transmit(DW_SPI, &crc8, 1U, DW_SPI_TIMEOUT);
    cs_high();

    decamutexoff(stat);
    return (st == HAL_OK) ? DWT_SUCCESS : DWT_ERROR;
}
