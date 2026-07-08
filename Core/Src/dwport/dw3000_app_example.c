/**
 * @file    dw3000_app_example.c
 * @brief   Qorvo dwt_uwb_driver(v08.02.02) 초기화 예제 (STM32L4 / DWM3000)
 *
 *  사용법: main() 에서 dw3000_app_init() 한 번 호출.
 *          드라이버 코어(dwt_uwb_driver/)가 프로젝트에 추가돼야 빌드됨.
 *
 *  ⚠ 상수/구조체 필드 이름은 받은 드라이버 버전의 deca_device_api.h 에 맞춰
 *     필요 시 미세조정. (DWT_DW3000_DEV_ID, DWT_DW_INIT 등은 버전에 따라 다를 수 있음)
 */
#include "main.h"
#include "deca_device_api.h"
#include "dw3000_hw.h"
#include "dw3000_spi.h"
#include <stdio.h>

extern const struct dwt_probe_s dw3000_probe_interf;

/* 채널5 / 64MHz PRF / preamble code 9 — 송·수신측 동일하게 사용 */
static dwt_config_t dw_cfg = {
    5,                  /* channel */
    DWT_PLEN_128,       /* preamble length */
    DWT_PAC8,           /* PAC size */
    9, 9,               /* TX/RX preamble code */
    1,                  /* SFD: 1 = non-standard 8-symbol */
    DWT_BR_6M8,         /* data rate */
    DWT_PHRMODE_STD,
    DWT_PHRRATE_STD,
    (129 + 8 - 8),      /* SFD timeout */
    DWT_STS_MODE_OFF,
    DWT_STS_LEN_64,
    DWT_PDOA_M0
};

/* 채널5 TX 스펙트럼 설정 (PG delay / TX power / PG count) — Qorvo 기본값 */
static dwt_txconfig_t tx_rf_cfg = {
    0x34,           /* PGdly (채널5) */
    0xfdfdfdfd,     /* power */
    0x0             /* PGcount */
};

int dw3000_app_init(void)
{
    /* 1) 보드 HW: GPIO/SPI 초기화 + 하드리셋 */
    if (dw3000_hw_init() != 0) { printf("hw_init FAIL\r\n"); return -1; }

    /* 1.5) SLEEP/DEEPSLEEP 일 수 있으니 강제로 깨우기: CS 를 길게 Low(>500us).
            deep sleep 은 RST 로 안 깨어나고 CS Low 로만 깨어남. */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_RESET);  /* CS = PA2 low */
    HAL_Delay(2);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_2, GPIO_PIN_SET);    /* CS high */
    HAL_Delay(2);

    dw3000_hw_reset();
    deca_sleep(2);


    /* 2) 드라이버에 SPI/칩 등록 — ★ 반드시 dwt_* 호출보다 먼저!
          (probe 가 SPI 함수포인터/칩 구조체를 세팅. 없이 dwt_checkidlerc 호출 시 NULL 역참조로 HardFault) */
    if (dwt_probe((struct dwt_probe_s*)&dw3000_probe_interf) != DWT_SUCCESS) {
        printf("dwt_probe FAIL\r\n"); return -3;
    }

    /* --- 디버그: probe 직후 DEV_ID 와 SYS_STATUS 확인 ---
       (SPI 는 Mode 0 으로 고정, 모드핀 PA0/PA1 구동 — 더 이상 모드 스윕 불필요) */
    printf("after probe: DEVID=0x%08lX SYS_STATUS=0x%08lX\r\n",
           (unsigned long)dwt_readdevid(),
           (unsigned long)dwt_readsysstatuslo());

    /* --- SPI 쓰기 무결성 테스트: EUI 레지스터 write → read back ---
       11..88 을 그대로 읽어오면 쓰기 정상. 다르면 우리 SPI 쓰기가 깨지는 것 = 근본원인 */
    {
        uint8_t euiw[8] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
        uint8_t euir[8] = {0};
        dwt_seteui(euiw);
        dwt_geteui(euir);
        printf("EUI wr-test rd= %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
               euir[0],euir[1],euir[2],euir[3],euir[4],euir[5],euir[6],euir[7]);
    }

    /* --- 칩 실제 상태(SYS_STATE_LO=0xF0030) + 클럭 동작 확인(SYS_TIME 2회) --- */
    printf("SYS_STATE = 0x%08lX\r\n", (unsigned long)dwt_read_reg(0xF0030UL));
    {
        uint32_t t1 = dwt_read_reg(0x1CUL);
        deca_sleep(1);
        uint32_t t2 = dwt_read_reg(0x1CUL);
        printf("SYS_TIME t1=0x%08lX t2=0x%08lX (다르면 클럭 동작중)\r\n",
               (unsigned long)t1, (unsigned long)t2);
    }

    /* 3) IDLE_RC 진입 대기 (레퍼런스 방식). 모드핀+Mode0 수정 후엔 정상 진입해야 함. */
    int t = 1000;
    while (!dwt_checkidlerc() && --t > 0) { deca_sleep(1); }
    if (t <= 0) {
        printf("WARN: not in IDLE_RC (SYS=0x%08lX) — 그래도 진행\r\n",
               (unsigned long)dwt_readsysstatuslo());
    } else {
        printf("IDLE_RC OK\r\n");
    }

    printf("DEVID = 0x%08lX\r\n", (unsigned long)dwt_readdevid());

    /* 4) 초기화 (OTP 로드 + PART/LOT ID 도 OTP 에서 읽도록 플래그 추가) */
    if (dwt_initialise(DWT_DW_INIT | DWT_READ_OTP_PID | DWT_READ_OTP_LID) != DWT_SUCCESS) {
        printf("dwt_initialise FAIL\r\n"); return -4;
    }

    /* --- 진단: OTP/크리스털 상태. PARTID/LOTID 가 0 이면 OTP 미프로그램(가품/불량) → PLL 실패 원인 --- */
    printf("PARTID=0x%08lX  LOTID=0x%08lX\r\n",
           (unsigned long)dwt_getpartid(), (unsigned long)dwt_getlotid());
    printf("OTP_rev=%u  XTAL_trim=0x%02X\r\n",
           (unsigned)dwt_otprevision(), (unsigned)dwt_getxtaltrim());
#if 0
    printf("SYS_CFG   = %08X\r\n",
           dwt_read_reg(0x10UL));

    printf("SYS_TIME  = %08X\r\n",
           dwt_read_reg(0x1CUL));

    printf("DEV_ID    = %08X\r\n",
           dwt_readdevid());
#endif
    /* 5) RF/PLL 구성 (PLL lock 포함) */
    int cfg = dwt_configure(&dw_cfg);
    printf("dwt_configure ret=%d  PLLstatus=0x%08lX  SYS=0x%08lX\r\n",
           cfg, (unsigned long)dwt_readpllstatus(),
           (unsigned long)dwt_readsysstatuslo());
    if (cfg != DWT_SUCCESS) {
        printf("dwt_configure FAIL (PLL?)\r\n"); return -5;
    }

    /* 6) TX 스펙트럼(파워/PG delay) 구성 — 채널5 기본값 */
    dwt_configuretxrf(&tx_rf_cfg);

    /* 7) 이후 고속 SPI */
    dw3000_spi_speed_fast();

    printf("DW3000 ready!\r\n");
    return 0;
}

/* ------------------------------------------------------------------ *
 *  간단한 송신 루프 — 802.15.4 blink 프레임을 0.5초마다 전송.
 *  dw3000_app_init() 이 0 을 리턴한 뒤 main 의 while(1) 대신 호출하면 됨.
 *  (레퍼런스 simple_tx.c 와 동일한 동작)
 * ------------------------------------------------------------------ */
/* blink 프레임: [0]=0xC5(blink), [1]=시퀀스번호, [2..9]=디바이스ID("DECAWAVE") */
static uint8_t tx_msg[] = { 0xC5, 0, 'D', 'E', 'C', 'A', 'W', 'A', 'V', 'E' };
#define TX_FRAME_SN_IDX  1
#define TX_FCS_LEN       2          /* 하드웨어가 붙이는 CRC 2바이트 */

void dw3000_tx_loop(void)
{
    uint32_t n = 0;
    while (1) {
        /* 프레임 데이터 적재 (offset 0) */
        dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
        /* 전송 길이 = 데이터 + FCS, offset 0, ranging=0 */
        dwt_writetxfctrl(sizeof(tx_msg) + TX_FCS_LEN, 0, 0);
        /* 즉시 송신 */
        dwt_starttx(DWT_START_TX_IMMEDIATE);
        /* TX 완료(TXFRS) 대기 */
        while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK)) { };
        /* 상태 플래그 클리어 */
        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);

        printf("TX Frame Sent #%lu\r\n", (unsigned long)++n);

        /* 다음 프레임 위해 시퀀스번호 증가 */
        tx_msg[TX_FRAME_SN_IDX]++;
        deca_sleep(500);   /* 0.5s */
    }
}
