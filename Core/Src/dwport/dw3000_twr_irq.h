/**
 * @file    dw3000_twr_irq.h
 * @brief   DS-TWR 인터럽트(EXTI + dwt_isr) 버전 — 논블로킹 상태머신.
 *
 *  폴링판(dw3000_twr.c)과 프레임 포맷·타이밍은 완전히 동일하다
 *  (dw3000_twr_proto.h 공용). 달라지는 건 "누가 이벤트를 기다리느냐"뿐:
 *
 *    폴링판  : while(!(dwt_readsysstatuslo() & ...)) {}   ← CPU 가 SPI 로 계속 폴링
 *    인터럽트: DW3000 IRQ(PB2) → EXTI2 → dwt_isr() → 아래 콜백들이 다음 단계 진행
 *
 *  덕분에 교환이 진행되는 동안 메인 루프는 BLE 처리나 __WFI() 로 쉴 수 있고,
 *  SPI 폴링 트래픽이 사라져 지연송신 타이밍도 안정적이다.
 *
 *  사용 (initiator/태그):
 *      dw3000_twr_irq_init();
 *      for (;;) {
 *          dw3000_twr_irq_initiator_start();
 *          int r; float d;
 *          while ((r = dw3000_twr_irq_initiator_poll(&d)) == 0) { ...BLE/WFI... }
 *          if (r > 0) 사용(d);
 *          deca_sleep(100);
 *      }
 *
 *  사용 (responder/앵커):
 *      dw3000_twr_irq_init();
 *      dw3000_twr_irq_responder_start();
 *      for (;;) {
 *          float d;
 *          if (dw3000_twr_irq_responder_poll(&d)) printf(...);
 *          dw3000_twr_irq_responder_service();   // 워치독
 *          __WFI();
 *      }
 */
#ifndef DW3000_TWR_IRQ_H
#define DW3000_TWR_IRQ_H

#include <stdint.h>
#include <stdbool.h>

/* 한 번의 교환(POLL→RESP→FINAL→REPORT)이 이 시간을 넘기면 실패 처리 */
#define TWR_EXCHANGE_TIMEOUT_MS   20U
/* 앵커가 이 시간 동안 아무 이벤트도 못 받으면 RX 재무장 */
#define TWR_RESPONDER_IDLE_MS     200U

/* 안테나 지연 로드 + 콜백 등록 + DW 인터럽트 마스크 + EXTI 오픈.
 * dwt_initialise()/dwt_configure() 성공 뒤 1회. 0 = OK */
int  dw3000_twr_irq_init(void);

/* 인터럽트를 끄고 폴링판(dw3000_twr.c)으로 되돌린다 (캘리브레이션 등). */
void dw3000_twr_irq_stop(void);

/* ---------------- INITIATOR (태그) ---------------- */
/* POLL 을 쏘고 즉시 리턴. 나머지는 ISR 이 진행. */
void dw3000_twr_irq_initiator_start(void);
/* 결과 확인:  1 = 성공(*distance_m 유효) / -1 = 실패 / 0 = 진행 중.
 * 0 이 아닌 값을 한 번 리턴하면 상태는 IDLE 로 리셋된다. */
int  dw3000_twr_irq_initiator_poll(float *distance_m);
/* 진행 중인 교환을 강제 종료 (TRX off + IDLE). */
void dw3000_twr_irq_abort(void);

/* ---------------- RESPONDER (앵커) ---------------- */
/* RX 무한 대기 무장. 이후는 전부 ISR 이 처리. */
void dw3000_twr_irq_responder_start(void);
/* 새로 계산된 거리가 있으면 1 리턴하며 *distance_m 채움 (로그용). 없으면 0. */
int  dw3000_twr_irq_responder_poll(float *distance_m);
/* 워치독: 지연송신 실패 등으로 상태머신이 멈춘 경우 RX 재무장. 주기 호출. */
void dw3000_twr_irq_responder_service(void);

/* 통계 (디버그용) */
uint32_t dw3000_twr_irq_get_irq_count(void);

#endif /* DW3000_TWR_IRQ_H */
