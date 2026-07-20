/**
 * @file    dw3000_twr.c
 * @brief   DS-TWR (Double-Sided Two-Way Ranging) 거리 측정 — STM32 포팅
 *
 *  메시지 4개:
 *    1) POLL     Initiator → Responder
 *    2) RESPONSE Responder → Initiator
 *    3) FINAL    Initiator → Responder  (poll_tx, resp_rx, final_tx 타임스탬프 포함)
 *    4) REPORT   Responder → Initiator  (리스판더가 계산한 거리[mm]를 태그로 반환)
 *
 *  DS-TWR ToF = (Ra*Rb - Da*Db) / (Ra+Rb+Da+Db)
 *    Ra = resp_rx - poll_tx     (initiator 왕복1)
 *    Rb = final_rx - resp_tx    (responder 왕복2)
 *    Da = final_tx - resp_rx    (initiator 응답지연)
 *    Db = resp_tx - poll_rx     (responder 응답지연)
 *  SS-TWR 대비 클럭 드리프트에 강해 정확도↑ (±10~20cm).
 *
 *  ※ 타이밍 상수(uus)는 현재 채널/프리앰블(PLEN256)에 맞춰 넉넉히 잡았으나,
 *    실측에서 무응답이 나면 이 값들을 조정해야 할 수 있음(가장 흔한 튜닝 포인트).
 */
#include "dw3000_twr.h"
#include "deca_device_api.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- 안테나 지연 ---- */
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385
static uint16_t g_ant_dly = TX_ANT_DLY;
#define CAL_TOL_M  0.02

/* ---- 메시지 포맷 ---- */
#define MSG_SN_IDX    2
#define MSG_FUNC_IDX  9
#define FUNC_POLL     0x21
#define FUNC_RESP     0x10
#define FUNC_FINAL    0x23
#define FUNC_REPORT   0x24
#define TS_LEN        4
#define FCS_LEN       2
/* FINAL 내 타임스탬프 오프셋 */
#define FIN_POLL_TX_IDX   10
#define FIN_RESP_RX_IDX   14
#define FIN_FINAL_TX_IDX  18
/* REPORT 내 거리(mm) 오프셋 */
#define REP_DIST_IDX      10

static uint8_t tx_poll_msg[]   = { 0x41,0x88,0,0xCA,0xDE,'W','A','V','E',FUNC_POLL, 0,0 };
static uint8_t tx_resp_msg[]   = { 0x41,0x88,0,0xCA,0xDE,'V','E','W','A',FUNC_RESP, 0,0 };
static uint8_t tx_final_msg[]  = { 0x41,0x88,0,0xCA,0xDE,'W','A','V','E',FUNC_FINAL,
                                   0,0,0,0, 0,0,0,0, 0,0,0,0 };            /* +12 ts */
static uint8_t tx_report_msg[] = { 0x41,0x88,0,0xCA,0xDE,'V','E','W','A',FUNC_REPORT,
                                   0,0,0,0 };                             /* +4 dist */
static uint8_t rx_buffer[28];

/* ---- 타이밍 (UWB microseconds) — PLEN256 대비 넉넉히 ---- */
#define UUS_TO_DWT_TIME              65536
/* initiator */
#define POLL_TX_TO_RESP_RX_DLY_UUS   300
#define RESP_RX_TIMEOUT_UUS          2000
#define RESP_RX_TO_FINAL_TX_DLY_UUS  1500
#define FINAL_TX_TO_REPORT_RX_DLY_UUS 200
#define REPORT_RX_TIMEOUT_UUS        2000
/* responder */
#define POLL_RX_TO_RESP_TX_DLY_UUS   700
#define RESP_TX_TO_FINAL_RX_DLY_UUS  300
#define FINAL_RX_TIMEOUT_UUS         3000
#define REPORT_TX_DLY_UUS            900

#define SPEED_OF_LIGHT 299702547.0

static uint8_t frame_seq_nb = 0;

/* 직렬화 helper (LE 4바이트) */
static void ts_set(uint8_t *f, uint32_t ts)
{
    for (int i = 0; i < TS_LEN; i++) f[i] = (uint8_t)(ts >> (i * 8));
}
static void ts_get(const uint8_t *f, uint32_t *ts)
{
    *ts = 0;
    for (int i = 0; i < TS_LEN; i++) *ts += ((uint32_t)f[i]) << (i * 8);
}
/* 40-bit RX 타임스탬프 (u64) */
static uint64_t rx_ts_u64(void)
{
    uint8_t b[5]; uint64_t t = 0;
    dwt_readrxtimestamp(b, DWT_COMPAT_NONE);
    for (int i = 0; i < 5; i++) t += ((uint64_t)b[i]) << (i * 8);
    return t;
}
/* 지연송신 예약시각(u32, >>8) → 실제 RMARKER 타임스탬프(u32) */
static uint32_t sched_tx_ts32(uint32_t sched)
{
    return (uint32_t)(((uint64_t)(sched & 0xFFFFFFFEUL) << 8) + TX_ANT_DLY);
}

/* ------------------------------------------------------------------ */
void dw3000_twr_init(void)
{
    dwt_setrxantennadelay(g_ant_dly);
    dwt_settxantennadelay(g_ant_dly);
    /* rxaftertxdelay / rxtimeout 은 단계별로 인라인 설정한다. */
}

void dw3000_twr_set_antdelay(uint16_t d)
{
    g_ant_dly = d;
    dwt_settxantennadelay(d);
    dwt_setrxantennadelay(d);
}
uint16_t dw3000_twr_get_antdelay(void) { return g_ant_dly; }

/* ================================================================== *
 * INITIATOR (태그) — Poll → (Response) → Final → (Report=거리)
 * ================================================================== */
bool dw3000_twr_initiator_once(float *distance_m)
{
    uint32_t status; uint8_t rng; uint16_t flen;

    /* 1) POLL 송신 (응답 기대) */
    dwt_forcetrxoff();
    dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
    dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
    tx_poll_msg[MSG_SN_IDX] = frame_seq_nb;
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
    dwt_writetxfctrl(sizeof(tx_poll_msg) + FCS_LEN, 0, 1);
    dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

    /* RESPONSE 수신 대기 */
    while (!((status = dwt_readsysstatuslo()) &
             (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) { }
    frame_seq_nb++;
    if (!(status & DWT_INT_RXFCG_BIT_MASK)) {
        dwt_forcetrxoff();
        dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
        return false;
    }
    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
    flen = dwt_getframelength(&rng);
    if (flen == 0 || flen > sizeof(rx_buffer)) { dwt_forcetrxoff(); return false; }
    dwt_readrxdata(rx_buffer, flen, 0);
    if (rx_buffer[MSG_FUNC_IDX] != FUNC_RESP) { dwt_forcetrxoff(); return false; }

    /* 2) FINAL 송신 (poll_tx, resp_rx, final_tx 실어서, 지연송신 + Report 기대) */
    uint32_t poll_tx_ts  = dwt_readtxtimestamplo32();
    uint64_t resp_rx_u   = rx_ts_u64();
    uint32_t resp_rx_ts  = (uint32_t)resp_rx_u;
    uint32_t final_tx_time = (uint32_t)((resp_rx_u +
                             ((uint64_t)RESP_RX_TO_FINAL_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8);
    uint32_t final_tx_ts = sched_tx_ts32(final_tx_time);

    ts_set(&tx_final_msg[FIN_POLL_TX_IDX],  poll_tx_ts);
    ts_set(&tx_final_msg[FIN_RESP_RX_IDX],  resp_rx_ts);
    ts_set(&tx_final_msg[FIN_FINAL_TX_IDX], final_tx_ts);
    tx_final_msg[MSG_SN_IDX] = frame_seq_nb;

    dwt_setdelayedtrxtime(final_tx_time);
    dwt_setrxaftertxdelay(FINAL_TX_TO_REPORT_RX_DLY_UUS);
    dwt_setrxtimeout(REPORT_RX_TIMEOUT_UUS);
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    dwt_writetxdata(sizeof(tx_final_msg), tx_final_msg, 0);
    dwt_writetxfctrl(sizeof(tx_final_msg) + FCS_LEN, 0, 1);
    if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
        dwt_forcetrxoff(); return false;    /* 지연 final 실패(시각 지남) */
    }

    /* REPORT 수신 대기 (거리[mm]) */
    while (!((status = dwt_readsysstatuslo()) &
             (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) { }
    if (!(status & DWT_INT_RXFCG_BIT_MASK)) {
        dwt_forcetrxoff();
        dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
        return false;
    }
    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
    flen = dwt_getframelength(&rng);
    if (flen == 0 || flen > sizeof(rx_buffer)) { dwt_forcetrxoff(); return false; }
    dwt_readrxdata(rx_buffer, flen, 0);
    if (rx_buffer[MSG_FUNC_IDX] != FUNC_REPORT) { dwt_forcetrxoff(); return false; }

    uint32_t dist_mm_u;
    ts_get(&rx_buffer[REP_DIST_IDX], &dist_mm_u);
    *distance_m = (float)((int32_t)dist_mm_u) / 1000.0f;
    return true;
}

/* ================================================================== *
 * RESPONDER (앵커) — (Poll) → Response → (Final) → 계산 → Report
 * ================================================================== */
void dw3000_twr_responder_once(void)
{
    uint32_t status, guard = 0; uint8_t rng; uint16_t flen;

    /* POLL 수신 대기 (무한 + 갇힘 방지 가드) */
    dwt_forcetrxoff();
    dwt_setrxtimeout(0);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    while (!((status = dwt_readsysstatuslo()) &
             (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_ERR))) {
        if (++guard > 800000UL) { dwt_forcetrxoff(); return; }
    }
    if (!(status & DWT_INT_RXFCG_BIT_MASK)) {
        dwt_forcetrxoff(); dwt_writesysstatuslo(SYS_STATUS_ALL_RX_ERR); return;
    }
    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
    flen = dwt_getframelength(&rng);
    if (flen == 0 || flen > sizeof(rx_buffer)) { dwt_forcetrxoff(); return; }
    dwt_readrxdata(rx_buffer, flen, 0);
    if (rx_buffer[MSG_FUNC_IDX] != FUNC_POLL) { dwt_forcetrxoff(); return; }

    uint64_t poll_rx_u  = rx_ts_u64();
    uint32_t poll_rx_ts = (uint32_t)poll_rx_u;
    uint8_t  sn = rx_buffer[MSG_SN_IDX];

    /* RESPONSE 지연 송신 (Final 기대) */
    uint32_t resp_tx_time = (uint32_t)((poll_rx_u +
                            ((uint64_t)POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8);
    uint32_t resp_tx_ts = sched_tx_ts32(resp_tx_time);

    dwt_setdelayedtrxtime(resp_tx_time);
    dwt_setrxaftertxdelay(RESP_TX_TO_FINAL_RX_DLY_UUS);
    dwt_setrxtimeout(FINAL_RX_TIMEOUT_UUS);
    tx_resp_msg[MSG_SN_IDX] = sn;
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
    dwt_writetxfctrl(sizeof(tx_resp_msg) + FCS_LEN, 0, 1);
    if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
        dwt_forcetrxoff(); return;
    }

    /* FINAL 수신 대기 */
    while (!((status = dwt_readsysstatuslo()) &
             (DWT_INT_RXFCG_BIT_MASK | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) { }
    if (!(status & DWT_INT_RXFCG_BIT_MASK)) {
        dwt_forcetrxoff(); dwt_writesysstatuslo(SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR); return;
    }
    dwt_writesysstatuslo(DWT_INT_RXFCG_BIT_MASK);
    flen = dwt_getframelength(&rng);
    if (flen == 0 || flen > sizeof(rx_buffer)) { dwt_forcetrxoff(); return; }
    dwt_readrxdata(rx_buffer, flen, 0);
    if (rx_buffer[MSG_FUNC_IDX] != FUNC_FINAL) { dwt_forcetrxoff(); return; }

    uint64_t final_rx_u  = rx_ts_u64();
    uint32_t final_rx_ts = (uint32_t)final_rx_u;
    uint32_t poll_tx_ts, resp_rx_ts, final_tx_ts;
    ts_get(&rx_buffer[FIN_POLL_TX_IDX],  &poll_tx_ts);
    ts_get(&rx_buffer[FIN_RESP_RX_IDX],  &resp_rx_ts);
    ts_get(&rx_buffer[FIN_FINAL_TX_IDX], &final_tx_ts);

    /* DS-TWR ToF (32-bit 뺄셈은 wrap-safe) */
    double Ra = (double)(uint32_t)(resp_rx_ts  - poll_tx_ts);
    double Rb = (double)(uint32_t)(final_rx_ts - resp_tx_ts);
    double Da = (double)(uint32_t)(final_tx_ts - resp_rx_ts);
    double Db = (double)(uint32_t)(resp_tx_ts  - poll_rx_ts);
    double tof_dtu = (Ra * Rb - Da * Db) / (Ra + Rb + Da + Db);
    double dist_m  = tof_dtu * DWT_TIME_UNITS * SPEED_OF_LIGHT;
    int32_t dist_mm = (int32_t)(dist_m * 1000.0);

    /* REPORT 지연 송신 (거리[mm] → 태그). 태그 RX 창에 맞춰 지연. */
    uint32_t report_tx_time = (uint32_t)((final_rx_u +
                              ((uint64_t)REPORT_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8);
    ts_set(&tx_report_msg[REP_DIST_IDX], (uint32_t)dist_mm);
    tx_report_msg[MSG_SN_IDX] = sn;
    dwt_setdelayedtrxtime(report_tx_time);
    dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    dwt_writetxdata(sizeof(tx_report_msg), tx_report_msg, 0);
    dwt_writetxfctrl(sizeof(tx_report_msg) + FCS_LEN, 0, 0);
    if (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS) {
        uint32_t g2 = 0;
        while (!(dwt_readsysstatuslo() & DWT_INT_TXFRS_BIT_MASK)) { if (++g2 > 200000UL) break; }
        dwt_writesysstatuslo(DWT_INT_TXFRS_BIT_MASK);
    } else {
        dwt_forcetrxoff();
    }
}

/* ================================================================== *
 * 안테나 딜레이 캘리브레이션 (INITIATOR 에서 실행, DS-TWR 사용)
 * ================================================================== */
uint16_t dw3000_twr_calibrate(float known_m, int n_samples, int max_iter)
{
    for (int it = 0; it < max_iter; it++) {
        double sum = 0.0; int got = 0;
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
        double err = avg - (double)known_m;
        printf("[CAL] iter%d n=%d avg=%dmm err=%dmm antdly=%u\r\n",
               it, got, (int)(avg * 1000.0), (int)(err * 1000.0), g_ant_dly);
        if (fabs(err) <= CAL_TOL_M) {
            printf("[CAL] 수렴 OK. 최종 antenna delay = %u\r\n", g_ant_dly);
            return g_ant_dly;
        }
        double err_dtu = (err / SPEED_OF_LIGHT) / DWT_TIME_UNITS;
        int32_t corr = (int32_t)(err_dtu / 2.0);
        if (corr == 0) corr = (err > 0) ? 1 : -1;
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
