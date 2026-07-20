/**
 * @file    dw3000_twr.c
 * @brief   SS-TWR 거리 측정 구현 (Qorvo ss_twr 예제 호환, STM32 포팅)
 *
 *  거리 = ToF * c,  ToF = (Tround - Treply*(1-clkOffset)) / 2
 *    Tround = resp_rx_ts - poll_tx_ts   (initiator 측)
 *    Treply = resp_tx_ts - poll_rx_ts   (responder 가 메시지에 실어 보냄)
 */
#include "dw3000_twr.h"
#include "deca_device_api.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- 안테나 지연 (채널5 기본값. 정밀도 필요 시 실측 캘리브레이션) ---- */
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385

/* 런타임 안테나 딜레이(TX=RX 동일값). 캘리브레이션이 이 값을 갱신한다. */
static uint16_t g_ant_dly = TX_ANT_DLY;

/* 캘리브레이션 수렴 허용 오차 (m) */
#define CAL_TOL_M  0.02

/* ---- 메시지 포맷 (Qorvo SS-TWR 호환) ----
 *  공통 헤더 10B: FC(2) seq(1) PANID(2) addr/tag(4) func(1)
 *  poll  : 헤더 + FCS               (12B)
 *  resp  : 헤더 + poll_rx_ts(4) + resp_tx_ts(4) + FCS   (20B) */
#define MSG_SN_IDX            2
#define MSG_FUNC_IDX          9
#define FUNC_POLL             0x21
#define FUNC_RESP             0x10
#define RESP_POLL_RX_TS_IDX   10
#define RESP_RESP_TX_TS_IDX   14
#define TS_LEN                4
#define FCS_LEN               2

static uint8_t tx_poll_msg[] = { 0x41,0x88,0,0xCA,0xDE,'W','A','V','E',FUNC_POLL, 0,0 };
static uint8_t tx_resp_msg[] = { 0x41,0x88,0,0xCA,0xDE,'V','E','W','A',FUNC_RESP,
                                 0,0,0,0, 0,0,0,0 };
static uint8_t rx_buffer[24];

/* ---- 타이밍 (UWB microseconds, uus) ----
 *  1 uus = 512/499.2 MHz ≈ 1.026 µs.  채널5/6.8Mbps/PLEN128 기준 여유값. */
#define UUS_TO_DWT_TIME             65536
#define POLL_TX_TO_RESP_RX_DLY_UUS  240   /* initiator: TX 후 RX 켜기까지 지연 */
#define RESP_RX_TIMEOUT_UUS         400   /* initiator: 응답 대기 타임아웃 */
#define POLL_RX_TO_RESP_TX_DLY_UUS  450   /* responder: poll 수신 후 응답 송신까지 */

#define SPEED_OF_LIGHT 299702547.0   /* m/s (공기 중) */

static uint8_t frame_seq_nb = 0;

/* 40-bit/32-bit 타임스탬프 직렬화 helper */
static void ts_set(uint8_t *f, uint64_t ts)
{
    for (int i = 0; i < TS_LEN; i++) f[i] = (uint8_t)(ts >> (i * 8));
}
static void ts_get(const uint8_t *f, uint32_t *ts)
{
    *ts = 0;
    for (int i = 0; i < TS_LEN; i++) *ts += ((uint32_t)f[i]) << (i * 8);
}

/* ------------------------------------------------------------------ */
void dw3000_twr_init(void)
{
    dwt_setrxantennadelay(g_ant_dly);
    dwt_settxantennadelay(g_ant_dly);
    /* initiator 용: TX 완료 후 자동 RX 활성까지 지연 + 응답 타임아웃.
       responder 에서도 무해. */
    dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
    dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
}

/* 런타임 안테나 딜레이 설정/조회 (TX=RX 동일값) */
void dw3000_twr_set_antdelay(uint16_t d)
{
    g_ant_dly = d;
    dwt_settxantennadelay(d);
    dwt_setrxantennadelay(d);
}
uint16_t dw3000_twr_get_antdelay(void) { return g_ant_dly; }

/* ------------------------------------------------------------------ *
 * 안테나 딜레이 캘리브레이션 (APS014 기반, INITIATOR 에서 실행)
 *   - 앵커(RESPONDER)를 known_m 거리에 정확히(LOS) 두고 실행.
 *   - N회 측정 평균 → 오차를 DTU 로 환산 → 딜레이 보정 → 수렴까지 반복.
 *   - 반환: 최종 안테나 딜레이 (이 값을 TX_ANT_DLY/RX_ANT_DLY 에 반영).
 *   참고: 문서 권장은 DS-TWR 이나 본 포팅은 SS-TWR → 표본수를 늘려 평균으로 보완.
 * ------------------------------------------------------------------ */
uint16_t dw3000_twr_calibrate(float known_m, int n_samples, int max_iter)
{
    for (int it = 0; it < max_iter; it++) {
        double sum = 0.0; int got = 0;
        /* 유효 측정 n_samples 개 확보 (최대 3배 시도) */
        for (int i = 0; i < n_samples * 3 && got < n_samples; i++) {
            float d;
            if (dw3000_twr_initiator_once(&d)) { sum += d; got++; }
            deca_sleep(15);
        }
        if (got < n_samples / 2) {
            printf("[CAL] iter%d: 측정 부족(%d) - 앵커/거리/LOS 확인\r\n", it, got);
            return g_ant_dly;
        }
        double avg = sum / got;
        double err = avg - (double)known_m;                 /* + 이면 측정이 큼 */
        printf("[CAL] iter%d n=%d avg=%dmm err=%dmm antdly=%u\r\n",
               it, got, (int)(avg * 1000.0), (int)(err * 1000.0), g_ant_dly);

        if (fabs(err) <= CAL_TOL_M) {
            printf("[CAL] 수렴 OK. 최종 antenna delay = %u\r\n", g_ant_dly);
            return g_ant_dly;
        }
        /* 오차(m) → 시간(s) → DTU(ticks). 딜레이를 키우면 측정거리가 줄어든다.
           TX/RX 동일값 가정 → 절반씩 보정(문서 절차 4.2 / 6). */
        double err_dtu = (err / SPEED_OF_LIGHT) / DWT_TIME_UNITS;
        int32_t corr = (int32_t)(err_dtu / 2.0);
        if (corr == 0) corr = (err > 0) ? 1 : -1;           /* 최소 1틱 이동 */
        int32_t nd = (int32_t)g_ant_dly + corr;
        if (nd < 0) nd = 0;
        if (nd > 0xFFFF) nd = 0xFFFF;
        g_ant_dly = (uint16_t)nd;
        dwt_settxantennadelay(g_ant_dly);
        dwt_setrxantennadelay(g_ant_dly);
    }
    printf("[CAL] 최대 반복 도달. antenna delay = %u\r\n", g_ant_dly);
    return g_ant_dly;
}

/* ------------------------------------------------------------------ *
 * INITIATOR
 * ------------------------------------------------------------------ */
bool dw3000_twr_initiator_once(float *distance_m)
{
    /* 1) poll 송신 (response expected → TX 끝나면 자동 RX) */
    tx_poll_msg[MSG_SN_IDX] = frame_seq_nb;
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
    dwt_writetxfctrl(sizeof(tx_poll_msg) + FCS_LEN, 0, 1); /* ranging=1 */
    dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

    /* 2) RXFCG(수신성공) 또는 타임아웃/에러 대기 */
    uint32_t status;
    while (!((status = dwt_readsysstatuslo()) &
             (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) { }

    frame_seq_nb++;

    if (!(status & DWT_INT_RXFCG_BIT_MASK)) {
        dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
        return false;
    }
    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

    /* 3) 프레임 읽기 */
    uint8_t  rng;
    uint16_t frame_len = dwt_getframelength(&rng);
    if (frame_len == 0 || frame_len > sizeof(rx_buffer)) return false;
    dwt_readrxdata(rx_buffer, frame_len, 0);
    if (rx_buffer[MSG_FUNC_IDX] != FUNC_RESP) return false;

    /* 4) 타임스탬프 수집 */
    uint32_t poll_tx_ts = dwt_readtxtimestamplo32();
    uint32_t resp_rx_ts = dwt_readrxtimestamplo32(DWT_COMPAT_NONE);
    uint32_t poll_rx_ts, resp_tx_ts;
    ts_get(&rx_buffer[RESP_POLL_RX_TS_IDX], &poll_rx_ts);
    ts_get(&rx_buffer[RESP_RESP_TX_TS_IDX], &resp_tx_ts);

    /* 5) 클럭 오프셋 보정 + ToF → 거리 */
    double clockOffsetRatio = ((double)dwt_readclockoffset()) / (double)(1 << 26);
    double Tround = (double)(uint32_t)(resp_rx_ts - poll_tx_ts);  /* uint32 wrap-safe */
    double Treply = (double)(uint32_t)(resp_tx_ts - poll_rx_ts);
    double tof    = ((Tround - Treply * (1.0 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;

    *distance_m = (float)(tof * SPEED_OF_LIGHT);
    return true;
}

/* ------------------------------------------------------------------ *
 * RESPONDER (앵커)
 * ------------------------------------------------------------------ */
void dw3000_twr_responder_once(void)
{
    /* poll 무한 대기 (타임아웃 끄기) */
    dwt_setrxtimeout(0);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);

    uint32_t status;
    while (!((status = dwt_readsysstatuslo()) &
             (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR))) { }

    if (!(status & DWT_INT_RXFCG_BIT_MASK)) {
        dwt_writesysstatuslo(SYS_STATUS_ALL_RX_ERR);
        return;
    }
    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);

    uint8_t  rng;
    uint16_t frame_len = dwt_getframelength(&rng);
    if (frame_len == 0 || frame_len > sizeof(rx_buffer)) return;
    dwt_readrxdata(rx_buffer, frame_len, 0);
    if (rx_buffer[MSG_FUNC_IDX] != FUNC_POLL) return;

    /* poll 수신 타임스탬프 (40-bit) */
    uint8_t  ts_b[5];
    dwt_readrxtimestamp(ts_b, DWT_COMPAT_NONE);
    uint64_t poll_rx_ts = 0;
    for (int i = 0; i < 5; i++) poll_rx_ts += ((uint64_t)ts_b[i]) << (i * 8);

    /* response 지연 송신 시각 계산 */
    uint32_t resp_tx_time =
        (uint32_t)((poll_rx_ts + ((uint64_t)POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8);
    dwt_setdelayedtrxtime(resp_tx_time);

    /* 실제 송신 타임스탬프 = (지연시각<<8) + 안테나지연 */
    uint64_t resp_tx_ts = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

    /* 메시지에 타임스탬프 채워서 송신 */
    ts_set(&tx_resp_msg[RESP_POLL_RX_TS_IDX], poll_rx_ts);
    ts_set(&tx_resp_msg[RESP_RESP_TX_TS_IDX], resp_tx_ts);
    tx_resp_msg[MSG_SN_IDX] = rx_buffer[MSG_SN_IDX];

    dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
    dwt_writetxfctrl(sizeof(tx_resp_msg) + FCS_LEN, 0, 1);

    if (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS) {
        while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK)) { }
        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    }
}
