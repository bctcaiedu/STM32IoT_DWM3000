/**
 * @file    dw3000_twr_irq.c
 * @brief   DS-TWR 인터럽트 버전 — DW3000 IRQ(PB2/EXTI2) → dwt_isr() → 콜백 상태머신.
 *
 *  상태 전이
 *  ─────────
 *  INITIATOR (태그)
 *      start()  ── POLL TX(즉시, RESPONSE_EXPECTED) ─→ I_WAIT_RESP
 *      cbRxOk(RESP)   ── FINAL TX(지연, RESPONSE_EXPECTED) ─→ I_WAIT_REPORT
 *      cbRxOk(REPORT) ── 거리 확정 ─→ IDLE (result = OK)
 *      cbRxTo/cbRxErr/포맷불일치/지연TX실패 ─→ IDLE (result = FAIL)
 *
 *  RESPONDER (앵커)
 *      start()  ── RX enable(timeout 0) ─→ R_WAIT_POLL
 *      cbRxOk(POLL)  ── RESP TX(지연, RESPONSE_EXPECTED) ─→ R_WAIT_FINAL
 *      cbRxOk(FINAL) ── ToF 계산 + REPORT TX(지연) ─→ R_TX_REPORT
 *      cbTxDone(REPORT) ── RX 재무장 ─→ R_WAIT_POLL
 *      그 외 이벤트 ─→ RX 재무장
 *
 *  ⚠ 아래 콜백은 전부 EXTI2 인터럽트 컨텍스트에서 돈다.
 *    - printf 금지 (UART 블로킹)
 *    - SPI(dwt_*) 호출은 OK. decamutexon() 이 DW EXTI 를 막아주고,
 *      BLE 는 SPI3 라 버스가 겹치지 않는다.
 *    - 메인과 공유하는 변수는 전부 volatile.
 */
#include "dw3000_twr_irq.h"
#include "dw3000_twr.h"        /* dw3000_twr_init() — 안테나 지연 로드 공유 */
#include "dw3000_twr_proto.h"  /* 프레임 포맷 / 타이밍 (폴링판과 공용) */
#include "dw3000_hw.h"
#include "deca_device_api.h"
#include "main.h"              /* HAL_GetTick */
#include <string.h>

/* ------------------------------------------------------------------ *
 * 프레임 버퍼 (폴링판과 동일 포맷)
 * ------------------------------------------------------------------ */
static uint8_t tx_poll_msg[]   = { 0x41,0x88,0,0xCA,0xDE,'W','A','V','E',FUNC_POLL, 0,0 };
static uint8_t tx_resp_msg[]   = { 0x41,0x88,0,0xCA,0xDE,'V','E','W','A',FUNC_RESP, 0,0 };
static uint8_t tx_final_msg[]  = { 0x41,0x88,0,0xCA,0xDE,'W','A','V','E',FUNC_FINAL,
                                   0,0,0,0, 0,0,0,0, 0,0,0,0 };          /* +12 ts */
static uint8_t tx_report_msg[] = { 0x41,0x88,0,0xCA,0xDE,'V','E','W','A',FUNC_REPORT,
                                   0,0,0,0 };                            /* +4 dist */
static uint8_t rx_buf[28];

/* 우리가 실제로 쓰는 이벤트만 enable (Qorvo ds_twr 예제와 동일 조합).
   여기 없는 비트는 IRQ 라인을 올리지 않으므로 dwt_isr() 도 안 불린다. */
#define TWR_INT_MASK ((uint32_t)DWT_INT_TXFRS_BIT_MASK | (uint32_t)DWT_INT_RXFCG_BIT_MASK \
                    | (uint32_t)DWT_INT_RXFTO_BIT_MASK | (uint32_t)DWT_INT_RXPTO_BIT_MASK \
                    | (uint32_t)DWT_INT_RXPHE_BIT_MASK | (uint32_t)DWT_INT_RXFCE_BIT_MASK \
                    | (uint32_t)DWT_INT_RXFSL_BIT_MASK | (uint32_t)DWT_INT_RXSTO_BIT_MASK \
                    | (uint32_t)DWT_INT_ARFE_BIT_MASK)

/* ------------------------------------------------------------------ *
 * 상태
 * ------------------------------------------------------------------ */
typedef enum {
    ST_IDLE = 0,
    ST_I_WAIT_RESP,
    ST_I_WAIT_REPORT,
    ST_R_WAIT_POLL,
    ST_R_WAIT_FINAL,
    ST_R_TX_REPORT
} twr_state_t;

static volatile twr_state_t g_state       = ST_IDLE;
static volatile int         g_result      = 0;      /* 1=OK, -1=FAIL, 0=진행중 */
static volatile float       g_dist_m      = 0.0f;   /* initiator 결과 */
static volatile float       g_r_dist_m    = 0.0f;   /* responder 가 계산한 거리 */
static volatile bool        g_r_new       = false;
static volatile uint32_t    g_last_evt_ms = 0;
static uint32_t             g_start_ms    = 0;      /* 메인 컨텍스트 전용 */
static uint8_t              frame_seq_nb  = 0;

/* responder 중간값 (ISR 내부에서만 사용) */
static uint64_t r_poll_rx_u;
static uint32_t r_resp_tx_ts;
static uint8_t  r_sn;

/* ------------------------------------------------------------------ *
 * helper (폴링판과 동일 로직)
 * ------------------------------------------------------------------ */
static void ts_set(uint8_t *f, uint32_t ts)
{
    for (int i = 0; i < TS_LEN; i++) f[i] = (uint8_t)(ts >> (i * 8));
}
static void ts_get(const uint8_t *f, uint32_t *ts)
{
    *ts = 0;
    for (int i = 0; i < TS_LEN; i++) *ts += ((uint32_t)f[i]) << (i * 8);
}
static uint64_t rx_ts_u64(void)
{
    uint8_t b[5]; uint64_t t = 0;
    dwt_readrxtimestamp(b, DWT_COMPAT_NONE);
    for (int i = 0; i < 5; i++) t += ((uint64_t)b[i]) << (i * 8);
    return t;
}
/* 지연송신 예약시각(u32, >>8) → 실제 RMARKER 타임스탬프(u32).
   폴링판과 동일하게 컴파일타임 TX_ANT_DLY 를 쓴다(측정값 호환 유지). */
static uint32_t sched_tx_ts32(uint32_t sched)
{
    return (uint32_t)(((uint64_t)(sched & 0xFFFFFFFEUL) << 8) + TX_ANT_DLY);
}

/* ================================================================== *
 * INITIATOR 콜백 처리
 * ================================================================== */
static void i_finish(int result)
{
    dwt_forcetrxoff();
    g_state  = ST_IDLE;
    g_result = result;
}

static void i_on_resp(void)
{
    if (rx_buf[MSG_FUNC_IDX] != FUNC_RESP) { i_finish(-1); return; }

    uint32_t poll_tx_ts = dwt_readtxtimestamplo32();
    uint64_t resp_rx_u  = rx_ts_u64();
    uint32_t resp_rx_ts = (uint32_t)resp_rx_u;
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
    dwt_writetxdata(sizeof(tx_final_msg), tx_final_msg, 0);
    dwt_writetxfctrl(sizeof(tx_final_msg) + TWR_FCS_LEN, 0, 1);

    if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
        i_finish(-1);            /* 예약 시각이 이미 지남 */
        return;
    }
    g_state = ST_I_WAIT_REPORT;
}

static void i_on_report(void)
{
    if (rx_buf[MSG_FUNC_IDX] != FUNC_REPORT) { i_finish(-1); return; }

    uint32_t dist_mm_u;
    ts_get(&rx_buf[REP_DIST_IDX], &dist_mm_u);
    g_dist_m = (float)((int32_t)dist_mm_u) / 1000.0f;
    i_finish(1);
}

/* ================================================================== *
 * RESPONDER 콜백 처리
 * ================================================================== */
static void r_rearm(void)
{
    dwt_forcetrxoff();
    dwt_setrxtimeout(0);
    g_state = ST_R_WAIT_POLL;
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static void r_on_poll(void)
{
    if (rx_buf[MSG_FUNC_IDX] != FUNC_POLL) { r_rearm(); return; }

    r_poll_rx_u = rx_ts_u64();
    r_sn        = rx_buf[MSG_SN_IDX];

    uint32_t resp_tx_time = (uint32_t)((r_poll_rx_u +
                            ((uint64_t)POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8);
    r_resp_tx_ts = sched_tx_ts32(resp_tx_time);

    dwt_setdelayedtrxtime(resp_tx_time);
    dwt_setrxaftertxdelay(RESP_TX_TO_FINAL_RX_DLY_UUS);
    dwt_setrxtimeout(FINAL_RX_TIMEOUT_UUS);
    tx_resp_msg[MSG_SN_IDX] = r_sn;
    dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
    dwt_writetxfctrl(sizeof(tx_resp_msg) + TWR_FCS_LEN, 0, 1);

    if (dwt_starttx(DWT_START_TX_DELAYED | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS) {
        r_rearm();
        return;
    }
    g_state = ST_R_WAIT_FINAL;
}

static void r_on_final(void)
{
    if (rx_buf[MSG_FUNC_IDX] != FUNC_FINAL) { r_rearm(); return; }

    uint64_t final_rx_u  = rx_ts_u64();
    uint32_t final_rx_ts = (uint32_t)final_rx_u;
    uint32_t poll_tx_ts, resp_rx_ts, final_tx_ts;
    ts_get(&rx_buf[FIN_POLL_TX_IDX],  &poll_tx_ts);
    ts_get(&rx_buf[FIN_RESP_RX_IDX],  &resp_rx_ts);
    ts_get(&rx_buf[FIN_FINAL_TX_IDX], &final_tx_ts);

    /* DS-TWR ToF (32-bit 뺄셈은 wrap-safe) */
    double Ra = (double)(uint32_t)(resp_rx_ts   - poll_tx_ts);
    double Rb = (double)(uint32_t)(final_rx_ts  - r_resp_tx_ts);
    double Da = (double)(uint32_t)(final_tx_ts  - resp_rx_ts);
    double Db = (double)(uint32_t)(r_resp_tx_ts - (uint32_t)r_poll_rx_u);
    double tof_dtu = (Ra * Rb - Da * Db) / (Ra + Rb + Da + Db);
    double dist_m  = tof_dtu * DWT_TIME_UNITS * SPEED_OF_LIGHT;
    int32_t dist_mm = (int32_t)(dist_m * 1000.0);

    g_r_dist_m = (float)dist_m;
    g_r_new    = true;

    /* REPORT 지연 송신 (거리[mm] → 태그). 태그 RX 창에 맞춰 지연. */
    uint32_t report_tx_time = (uint32_t)((final_rx_u +
                              ((uint64_t)REPORT_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8);
    ts_set(&tx_report_msg[REP_DIST_IDX], (uint32_t)dist_mm);
    tx_report_msg[MSG_SN_IDX] = r_sn;
    dwt_setdelayedtrxtime(report_tx_time);
    dwt_writetxdata(sizeof(tx_report_msg), tx_report_msg, 0);
    dwt_writetxfctrl(sizeof(tx_report_msg) + TWR_FCS_LEN, 0, 0);

    if (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS) {
        g_state = ST_R_TX_REPORT;    /* cbTxDone 에서 RX 재무장 */
    } else {
        r_rearm();
    }
}

/* ================================================================== *
 * dwt_isr() 콜백 (인터럽트 컨텍스트)
 * ================================================================== */
static void cb_tx_done(const dwt_cb_data_t *cb)
{
    (void)cb;
    g_last_evt_ms = HAL_GetTick();

    /* POLL/FINAL/RESP 의 TXFRS 는 무시. REPORT 송신 완료만 의미가 있다. */
    if (g_state == ST_R_TX_REPORT) r_rearm();
}

static void cb_rx_ok(const dwt_cb_data_t *cb)
{
    g_last_evt_ms = HAL_GetTick();

    uint16_t flen = cb->datalength;
    if (flen == 0U || flen > sizeof(rx_buf)) {
        if (g_state == ST_I_WAIT_RESP || g_state == ST_I_WAIT_REPORT) i_finish(-1);
        else                                                          r_rearm();
        return;
    }
    dwt_readrxdata(rx_buf, flen, 0);

    switch (g_state) {
    case ST_I_WAIT_RESP:   i_on_resp();   break;
    case ST_I_WAIT_REPORT: i_on_report(); break;
    case ST_R_WAIT_POLL:   r_on_poll();   break;
    case ST_R_WAIT_FINAL:  r_on_final();  break;
    default:               break;         /* IDLE 중 늦게 도착한 프레임 — 무시 */
    }
}

static void cb_rx_fail(const dwt_cb_data_t *cb)   /* timeout + error 공통 */
{
    (void)cb;
    g_last_evt_ms = HAL_GetTick();

    switch (g_state) {
    case ST_I_WAIT_RESP:
    case ST_I_WAIT_REPORT:
        i_finish(-1);
        break;
    case ST_R_WAIT_POLL:
    case ST_R_WAIT_FINAL:
    case ST_R_TX_REPORT:
        r_rearm();
        break;
    default:
        break;
    }
}

static dwt_callbacks_s g_cbs = {
    .cbTxDone    = cb_tx_done,
    .cbRxOk      = cb_rx_ok,
    .cbRxTo      = cb_rx_fail,
    .cbRxErr     = cb_rx_fail,
    .cbSPIErr    = NULL,
    .cbSPIRDErr  = NULL,
    .cbSPIRdy    = NULL,
    .cbDualSPIEv = NULL,
    .cbFrmRdy    = NULL,
    .cbCiaDone   = NULL,
    .devErr      = NULL,
    .cbSysEvent  = NULL,
};

/* ================================================================== *
 * 공개 API
 * ================================================================== */
int dw3000_twr_irq_init(void)
{
    /* 안테나 지연(+Flash 캘리브레이션 값) 은 폴링판과 공유 */
    dw3000_twr_init();

    dwt_setcallbacks(&g_cbs);

    /* 남은 상태비트 정리 → 우리가 쓰는 이벤트만 enable */
    dwt_writesysstatuslo(0xFFFFFFFFUL);
    dwt_setinterrupt(TWR_INT_MASK, 0, DWT_ENABLE_INT_ONLY);

    g_state       = ST_IDLE;
    g_result      = 0;
    g_last_evt_ms = HAL_GetTick();

    return dw3000_hw_init_interrupt();   /* PB2 EXTI2 오픈 */
}

void dw3000_twr_irq_stop(void)
{
    dw3000_hw_interrupt_disable();
    dwt_setinterrupt(DWT_INT_ALL_LO, DWT_INT_ALL_HI, DWT_DISABLE_INT);
    dwt_forcetrxoff();
    dwt_writesysstatuslo(0xFFFFFFFFUL);
    g_state  = ST_IDLE;
    g_result = 0;
}

uint32_t dw3000_twr_irq_get_irq_count(void) { return dw3000_hw_get_irq_count(); }

/* ---------------- INITIATOR ---------------- */
void dw3000_twr_irq_initiator_start(void)
{
    /* 셋업이 끝나기 전에 ISR 이 끼어들지 않도록 DW EXTI 를 잠근다. */
    bool was_on = dw3000_hw_interrupt_is_enabled();
    if (was_on) dw3000_hw_interrupt_disable();

    dwt_forcetrxoff();
    dwt_writesysstatuslo(0xFFFFFFFFUL);   /* 이전 교환 잔여 이벤트 제거 */

    g_result = 0;
    g_state  = ST_I_WAIT_RESP;

    dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
    dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
    tx_poll_msg[MSG_SN_IDX] = frame_seq_nb++;
    dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
    dwt_writetxfctrl(sizeof(tx_poll_msg) + TWR_FCS_LEN, 0, 1);
    dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

    g_start_ms = HAL_GetTick();
    if (was_on) dw3000_hw_interrupt_enable();
}

int dw3000_twr_irq_initiator_poll(float *distance_m)
{
    int r = g_result;

    if (r == 0) {
        /* 워치독: IRQ 를 놓쳤거나 지연송신이 어긋난 경우 여기서 빠져나온다. */
        if ((HAL_GetTick() - g_start_ms) > TWR_EXCHANGE_TIMEOUT_MS) {
            dw3000_twr_irq_abort();
            return -1;
        }
        return 0;
    }

    if (r > 0 && distance_m != NULL) *distance_m = g_dist_m;
    g_result = 0;
    return r;
}

void dw3000_twr_irq_abort(void)
{
    bool was_on = dw3000_hw_interrupt_is_enabled();
    if (was_on) dw3000_hw_interrupt_disable();

    dwt_forcetrxoff();
    dwt_writesysstatuslo(0xFFFFFFFFUL);
    g_state  = ST_IDLE;
    g_result = 0;

    if (was_on) dw3000_hw_interrupt_enable();
}

/* ---------------- RESPONDER ---------------- */
void dw3000_twr_irq_responder_start(void)
{
    bool was_on = dw3000_hw_interrupt_is_enabled();
    if (was_on) dw3000_hw_interrupt_disable();

    dwt_writesysstatuslo(0xFFFFFFFFUL);
    g_r_new = false;
    r_rearm();
    g_last_evt_ms = HAL_GetTick();

    if (was_on) dw3000_hw_interrupt_enable();
}

int dw3000_twr_irq_responder_poll(float *distance_m)
{
    if (!g_r_new) return 0;
    g_r_new = false;
    if (distance_m != NULL) *distance_m = g_r_dist_m;
    return 1;
}

void dw3000_twr_irq_responder_service(void)
{
    if ((HAL_GetTick() - g_last_evt_ms) < TWR_RESPONDER_IDLE_MS) return;

    /* 이 시간 동안 아무 이벤트도 없었다 = RX 가 꺼져있거나 상태머신이 멈춤.
       (예: 지연송신 실패 직후 인터럽트가 하나도 안 뜨는 경우)
       RX 대기 중 조용한 건 정상이지만, 재무장해도 손해가 없다. */
    bool was_on = dw3000_hw_interrupt_is_enabled();
    if (was_on) dw3000_hw_interrupt_disable();

    dwt_writesysstatuslo(0xFFFFFFFFUL);
    r_rearm();
    g_last_evt_ms = HAL_GetTick();

    if (was_on) dw3000_hw_interrupt_enable();
}
