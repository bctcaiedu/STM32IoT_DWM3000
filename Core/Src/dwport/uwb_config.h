/**
 * @file    uwb_config.h
 * @brief   UWB 앱 빌드 설정 — 역할(태그/앵커)과 BLE 사용 여부를 한곳에서 관리.
 *
 *  ┌──────────────────────────────────────────────────────────────┐
 *  │  보드 2개를 각각 다른 역할로 빌드해서 플래시한다.              │
 *  │   · 태그(폰 연결) 보드 : UWB_ROLE = UWB_ROLE_INITIATOR        │
 *  │   · 앵커 보드          : UWB_ROLE = UWB_ROLE_RESPONDER        │
 *  │                                                                │
 *  │  UWB_ENABLE_BLE:                                               │
 *  │   · 0 = BLE 미사용 (레인징 결과를 UART 로만 출력) — 1단계 검증 │
 *  │   · 1 = BLE notify 사용 (INITIATOR 전용).                      │
 *  │         ※ 반드시 CubeMX 로 X-CUBE-BLE1 을 추가한 뒤에 1 로!    │
 *  │           (그 전에 1 로 두면 BlueNRG 헤더가 없어 빌드 실패)    │
 *  └──────────────────────────────────────────────────────────────┘
 */
#ifndef UWB_CONFIG_H
#define UWB_CONFIG_H

#define UWB_ROLE_INITIATOR  1
#define UWB_ROLE_RESPONDER  2

/* === 여기만 바꾸면 됨 === */
#define UWB_ROLE        UWB_ROLE_INITIATOR
#define UWB_ENABLE_BLE  1

/* 안테나 딜레이 캘리브레이션 (INITIATOR 전용).
 *  1 로 두고 앵커(RESPONDER)를 UWB_CAL_KNOWN_MM 거리에 정확히 배치 후 플래시.
 *  시리얼에 나오는 최종 antenna delay 값을 dw3000_twr.c 의
 *  TX_ANT_DLY/RX_ANT_DLY 에 반영한 뒤 다시 0 으로 되돌린다. */
#define UWB_CALIBRATE     0
#define UWB_CAL_KNOWN_MM  2000     /* 레이저로 잰 실제 기준 거리 (mm) */
/* ======================== */

#endif /* UWB_CONFIG_H */
