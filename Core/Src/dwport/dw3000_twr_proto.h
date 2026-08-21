/**
 * @file    dw3000_twr_proto.h
 * @brief   DS-TWR 프레임 포맷 / 타이밍 상수 — 폴링판·인터럽트판 공용.
 *
 *  dw3000_twr.c (폴링) 과 dw3000_twr_irq.c (인터럽트) 가 같은 값을 써야
 *  하므로 한곳에 모았다. 타이밍(uus)을 손보면 양쪽 모두에 자동 반영된다.
 */
#ifndef DW3000_TWR_PROTO_H
#define DW3000_TWR_PROTO_H

/* ---- 안테나 지연 (기본값. Flash 캘리브레이션 값이 있으면 그쪽 우선) ---- */
#define TX_ANT_DLY 16385
#define RX_ANT_DLY 16385
#define CAL_TOL_M  0.02

/* ---- 메시지 포맷 ---- */
#define MSG_SN_IDX    2
#define MSG_FUNC_IDX  9
#define FUNC_POLL     0x21
#define FUNC_RESP     0x10
#define FUNC_FINAL    0x23
#define FUNC_REPORT   0x24
#define TS_LEN        4
#define TWR_FCS_LEN       2
/* FINAL 내 타임스탬프 오프셋 */
#define FIN_POLL_TX_IDX   10
#define FIN_RESP_RX_IDX   14
#define FIN_FINAL_TX_IDX  18
/* REPORT 내 거리(mm) 오프셋 */
#define REP_DIST_IDX      10

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

#endif /* DW3000_TWR_PROTO_H */
