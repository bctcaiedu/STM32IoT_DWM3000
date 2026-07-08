/**
 * @file    dw3000_twr.h
 * @brief   SS-TWR (Single-Sided Two-Way Ranging) — DW3000 거리 측정
 *
 *  사용:
 *   - 두 노드 모두 dwt_initialise()+dwt_configure() 완료 후 dw3000_twr_init() 1회 호출.
 *   - INITIATOR(태그, 폰 연결): 주기적으로 dw3000_twr_initiator_once(&dist) 호출.
 *   - RESPONDER(앵커): 루프에서 dw3000_twr_responder_once() 반복 호출.
 *   - 두 노드의 dwt_config_t(채널/프리앰블 등)는 반드시 동일해야 함.
 */
#ifndef DW3000_TWR_H
#define DW3000_TWR_H

#include <stdint.h>
#include <stdbool.h>

/* 안테나 지연/타이밍 세팅. init/configure 후 1회. */
void dw3000_twr_init(void);

/* INITIATOR: poll 송신 → response 수신 → 거리 계산.
 *  성공 시 *distance_m 에 거리(미터) 저장하고 true, 실패(타임아웃 등) 시 false. */
bool dw3000_twr_initiator_once(float *distance_m);

/* RESPONDER: poll 수신 → 타임스탬프 담아 response 지연송신 (블로킹 1회). */
void dw3000_twr_responder_once(void);

#endif /* DW3000_TWR_H */
