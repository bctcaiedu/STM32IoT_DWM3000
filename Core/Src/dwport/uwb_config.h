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

/* UWB_USE_IRQ:
 *   0 = 폴링 (dw3000_twr.c) — SYS_STATUS 를 SPI 로 계속 읽으며 대기.
 *   1 = 인터럽트 (dw3000_twr_irq.c) — DW3000 IRQ(PB2/ARD_D8) → EXTI2 → dwt_isr().
 *       교환 중 CPU 가 놀지 않아 BLE 처리/저전력(__WFI)이 가능하고,
 *       SPI 폴링 트래픽이 사라져 지연송신 타이밍이 안정적이다.
 *       ★ 하드웨어 조건: DWM3000EVB 의 IRQ 가 아두이노 D8(PB2)에 연결돼 있어야 함.
 *         (실드 핀맵: D7=RST, D8=IRQ, D9=WAKEUP, D10=CS)
 * ※ UWB_CALIBRATE=1 일 때는 캘리브레이션이 폴링 루틴을 쓰므로
 *   이 값과 무관하게 폴링으로 동작한다. */
#define UWB_USE_IRQ     1

/* 안테나 딜레이 캘리브레이션 (INITIATOR 전용).
 *  1 로 두고 앵커(RESPONDER)를 UWB_CAL_KNOWN_MM 거리에 정확히 배치 후 플래시.
 *  시리얼에 나오는 최종 antenna delay 값을 dw3000_twr.c 의
 *  TX_ANT_DLY/RX_ANT_DLY 에 반영한 뒤 다시 0 으로 되돌린다. */
#define UWB_CALIBRATE     0
#define UWB_CAL_KNOWN_MM  2000     /* 레이저로 잰 실제 기준 거리 (mm) */
/* ======================== */

#endif /* UWB_CONFIG_H */
