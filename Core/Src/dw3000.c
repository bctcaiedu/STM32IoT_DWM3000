/**
 * @file    dw3000.c
 * @brief   DW3000(DWM3000) 최소 드라이버 구현 (STM32 HAL 전용)
 *
 *  - SPI 트랜잭션 헤더(2바이트 full-addressed / 1바이트 fast-command) 생성
 *  - 레지스터 read/write, fast command
 *  - 초기화(리셋 + ID 확인 + IDLE_RC 대기) 및 simple TX/RX
 *
 *  주의: 본 드라이버는 "통신 검증 + 단순 송수신" 에 초점을 둔 최소 구현입니다.
 *  채널/PRF/프리앰블 등 RF 세부값은 ch5, 64MHz PRF, preamble code 9 의
 *  일반적 기본값을 가정합니다. 정밀 레인징(거리측정)에는 Qorvo 정식
 *  드라이버의 RF/PLL 캘리브레이션 값을 추가로 반영해야 합니다.
 *  (README 의 "한계와 다음 단계" 참고)
 */
#include "dw3000.h"
#include "dw3000_port.h"

/* SPI 모드 플래그: 최상위 비트가 R/W (1=write) */
#define DW_WRITE_FLAG   0x8000U
#define DW_READ_FLAG    0x0000U

/* ------------------------------------------------------------------ *
 *  헤더 생성                                                          *
 *  addr   = (reg_file << 9) | (reg_offset << 2)                       *
 *  header[0] = (rw | addr) >> 8     (bit7 = R/W)                      *
 *  header[1] = addr & 0xFF          (plain RW 는 하위 2비트 0)        *
 * ------------------------------------------------------------------ */
static uint16_t dw_make_header(uint8_t *hdr, uint32_t regId, uint16_t rwFlag)
{
    uint16_t reg_file   = (uint16_t)((regId >> 16) & 0x1FU);
    uint16_t reg_offset = (uint16_t)( regId        & 0x7FU);
    uint16_t addr       = (uint16_t)((reg_file << 9) | (reg_offset << 2));

    hdr[0] = (uint8_t)(((rwFlag | addr) >> 8) & 0xFFU);

    if (reg_offset == 0U)
    {
        /* FARW: offset 0 → 1-octet (bit6=0) */
        return 1U;
    }

    /* EAMRW: offset != 0 → 2-octet, bit6=1(0x40) 이 확장주소 마커 */
    hdr[0] |= 0x40U;
    hdr[1] = (uint8_t)( addr & 0xFFU);
    return 2U;
}

/* ------------------------------------------------------------------ *
 *  임의 길이 read / write                                            *
 * ------------------------------------------------------------------ */
void dw_read_buf(uint32_t regId, uint8_t *data, uint16_t len)
{
    uint8_t hdr[2];
    uint16_t hlen = dw_make_header(hdr, regId, DW_READ_FLAG);
    dw_port_spi_read(hdr, hlen, data, len);
}

void dw_write_buf(uint32_t regId, const uint8_t *data, uint16_t len)
{
    uint8_t hdr[2];
    uint16_t hlen = dw_make_header(hdr, regId, DW_WRITE_FLAG);
    dw_port_spi_write(hdr, hlen, data, len);
}

/* ------------------------------------------------------------------ *
 *  폭별 read / write (little-endian)                                  *
 * ------------------------------------------------------------------ */
uint32_t dw_read32(uint32_t regId)
{
    uint8_t b[4] = {0};
    dw_read_buf(regId, b, 4);
    return ((uint32_t)b[0]) | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint16_t dw_read16(uint32_t regId)
{
    uint8_t b[2] = {0};
    dw_read_buf(regId, b, 2);
    return (uint16_t)(b[0] | (b[1] << 8));
}

uint8_t dw_read8(uint32_t regId)
{
    uint8_t b = 0;
    dw_read_buf(regId, &b, 1);
    return b;
}

void dw_write32(uint32_t regId, uint32_t val)
{
    uint8_t b[4];
    b[0] = (uint8_t)(val      );
    b[1] = (uint8_t)(val >> 8 );
    b[2] = (uint8_t)(val >> 16);
    b[3] = (uint8_t)(val >> 24);
    dw_write_buf(regId, b, 4);
}

void dw_write16(uint32_t regId, uint16_t val)
{
    uint8_t b[2];
    b[0] = (uint8_t)(val     );
    b[1] = (uint8_t)(val >> 8);
    dw_write_buf(regId, b, 2);
}

void dw_write8(uint32_t regId, uint8_t val)
{
    dw_write_buf(regId, &val, 1);
}

/* ------------------------------------------------------------------ *
 *  Fast command : 1바이트 헤더 = 0x80(write) | 0x01(FAC) | (cmd<<1)   *
 * ------------------------------------------------------------------ */
void dw_fast_cmd(uint8_t cmd)
{
    uint8_t hdr = (uint8_t)(0x80U | 0x01U | ((cmd & 0x1FU) << 1));
    dw_port_spi_write(&hdr, 1, NULL, 0);
}

/* 상태 비트 클리어 (write-1-to-clear) */
static void dw_clear_status(uint32_t bits)
{
    dw_write32(DW3000_SYS_STATUS, bits);
}

/* ------------------------------------------------------------------ *
 *  기본 설정 (채널 5, preamble code 9 가정)                          *
 *  - 통신 검증 및 단순 송수신용 최소 설정                            *
 * ------------------------------------------------------------------ */
static void dw3000_default_config(void)
{
    /* CHAN_CTRL : ch5(=0), TX/RX preamble code = 9 */
    uint32_t chan = 0;
    chan &= ~DW3000_CHAN_CTRL_RF_CHAN;                  /* channel 5 */
    chan |= (9UL << DW3000_CHAN_CTRL_TX_PCODE_SHIFT);
    chan |= (9UL << DW3000_CHAN_CTRL_RX_PCODE_SHIFT);
    dw_write32(DW3000_CHAN_CTRL, chan);

    /* 프레임필터 비활성(모든 프레임 수신), 자동 FCS 처리는 유지 */
    /* SYS_CFG 는 리셋 기본값 유지. 필요 시 여기서 조정. */
}

/* ------------------------------------------------------------------ *
 *  초기화                                                            *
 * ------------------------------------------------------------------ */
int dw3000_init(void)
{
    uint32_t id;
    uint32_t guard;

    /* CS 를 먼저 High(idle) 로. CubeMX 가 PA2(CS) 를 Low 로 초기화하므로
       이 호출이 없으면 첫 트랜잭션에 CS 하강에지가 생기지 않아
       DW3000 이 응답하지 않고 DEV_ID 가 0 으로 읽힘 */
    dw_port_cs_high();
    dw_port_delay_ms(1);

    /* 초기화 구간은 느린 SPI 권장 */
    dw_port_spi_set_slow();

    /* HW 리셋 (CS High 유지 상태에서 수행) */
    dw_port_hard_reset();

    /* SPI 통신 확인: Device ID */
    id = dw3000_read_dev_id();
    if (id != DW3000_DEVICE_ID_EXPECTED)
    {
        /* ID 가 맞지 않으면 배선/SPI 설정 문제. */
        return DW_ERR;
    }

    /* IDLE_RC 진입(RCINIT) + SPI 준비(SPIRDY) 대기 */
    guard = 0;
    while (((dw_read32(DW3000_SYS_STATUS) &
            (DW3000_SYS_STATUS_RCINIT | DW3000_SYS_STATUS_SPIRDY)) !=
            (DW3000_SYS_STATUS_RCINIT | DW3000_SYS_STATUS_SPIRDY)))
    {
        dw_port_delay_ms(1);
        if (++guard > 1000U) return DW_ERR;  /* 1초 타임아웃 */
    }

    /* 상태비트 클리어 */
    dw_clear_status(0xFFFFFFFFUL);

    /* 기본 RF/채널 설정 */
    dw3000_default_config();

    /* 통신 안정화 이후 고속 SPI 전환 */
    dw_port_spi_set_fast();

    return DW_OK;
}

uint32_t dw3000_read_dev_id(void)
{
    return dw_read32(DW3000_DEV_ID);
}

/* ------------------------------------------------------------------ *
 *  송신 (blocking)                                                   *
 *  1) TX_BUFFER 에 페이로드 기록                                     *
 *  2) TX_FCTRL 에 길이 설정 (FCS 2바이트 포함 길이)                  *
 *  3) CMD_TX 발행                                                    *
 *  4) SYS_STATUS.TXFRS(전송완료) 폴링                               *
 * ------------------------------------------------------------------ */
int dw3000_send(const uint8_t *data, uint16_t len)
{
    uint32_t fctrl;
    uint32_t guard = 0;
    uint16_t txLen = (uint16_t)(len + 2U); /* +FCS(2) */

    if (len == 0U || len > 125U) return DW_ERR; /* 표준 프레임 최대 127바이트 */

    /* 1) TX 버퍼에 페이로드 기록 */
    dw_write_buf(DW3000_TX_BUFFER, data, len);

    /* 2) TX_FCTRL: [9:0]=TXFLEN(길이), [12:10]=TXBR/offset 등.
     *    여기서는 길이 필드만 갱신하고 나머지는 read-modify-write. */
    fctrl  = dw_read32(DW3000_TX_FCTRL);
    fctrl &= ~0x000003FFUL;                 /* TXFLEN[9:0] 클리어 */
    fctrl |=  (uint32_t)(txLen & 0x3FFU);   /* 전송 길이(FCS 포함) */
    /* TXFOFFS(버퍼 오프셋)=0 으로 둠 */
    fctrl &= ~(0x3FFUL << 16);
    dw_write32(DW3000_TX_FCTRL, fctrl);

    /* 3) 이전 TX 상태 클리어 후 송신 시작 */
    dw_clear_status(DW3000_SYS_STATUS_TXFRS | DW3000_SYS_STATUS_TXFRB);
    dw_fast_cmd(DW3000_CMD_TX);

    /* 4) 전송 완료 폴링 */
    while ((dw_read32(DW3000_SYS_STATUS) & DW3000_SYS_STATUS_TXFRS) == 0U)
    {
        dw_port_delay_ms(1);
        if (++guard > 100U) return DW_ERR;  /* 100ms 타임아웃 */
    }

    dw_clear_status(DW3000_SYS_STATUS_TXFRS);
    return DW_OK;
}

/* ------------------------------------------------------------------ *
 *  수신 (blocking)                                                   *
 *  1) CMD_RX 로 수신 enable                                          *
 *  2) RXFCG(정상) / 에러 / 타임아웃 폴링                            *
 *  3) RX_FINFO 로 길이 확인 후 RX_BUFFER_0 에서 읽기                 *
 * ------------------------------------------------------------------ */
int dw3000_receive(uint8_t *buf, uint16_t bufSize,
                   uint16_t *outLen, uint32_t timeoutMs)
{
    uint32_t status;
    uint32_t elapsed = 0;
    uint16_t frameLen;

    /* 이전 상태 클리어 후 수신 시작 */
    dw_clear_status(DW3000_SYS_STATUS_ALL_RX_GOOD |
                    DW3000_SYS_STATUS_ALL_RX_ERR  |
                    DW3000_SYS_STATUS_ALL_RX_TO);
    dw_fast_cmd(DW3000_CMD_RX);

    /* 폴링 */
    for (;;)
    {
        status = dw_read32(DW3000_SYS_STATUS);

        if (status & DW3000_SYS_STATUS_RXFCG)        /* 정상 수신 */
            break;

        if (status & (DW3000_SYS_STATUS_ALL_RX_ERR |
                      DW3000_SYS_STATUS_ALL_RX_TO))  /* 에러/타임아웃 */
        {
            dw_fast_cmd(DW3000_CMD_TXRXOFF);
            dw_clear_status(DW3000_SYS_STATUS_ALL_RX_ERR |
                            DW3000_SYS_STATUS_ALL_RX_TO);
            return DW_ERR;
        }

        dw_port_delay_ms(1);
        elapsed++;
        if (timeoutMs != 0U && elapsed > timeoutMs)
        {
            dw_fast_cmd(DW3000_CMD_TXRXOFF);
            return DW_ERR;
        }
    }

    /* 수신 길이 (FCS 2바이트 포함) */
    frameLen = (uint16_t)(dw_read32(DW3000_RX_FINFO) & DW3000_RX_FINFO_RXFLEN_MASK);
    if (frameLen < 2U) { dw_clear_status(DW3000_SYS_STATUS_RXFCG); return DW_ERR; }

    frameLen -= 2U;  /* FCS 제거 → 순수 페이로드 길이 */
    if (frameLen > bufSize) frameLen = bufSize;

    dw_read_buf(DW3000_RX_BUFFER_0, buf, frameLen);
    if (outLen) *outLen = frameLen;

    dw_clear_status(DW3000_SYS_STATUS_RXFCG | DW3000_SYS_STATUS_RXFR);
    return DW_OK;
}
