/**
 * @file    uwb_app.c
 * @brief   역할(태그/앵커) 기반 SS-TWR 실행 + (옵션) BLE notify.
 *
 *  ┌─────────────────────── 빌드 설정 ───────────────────────┐
 *  │ 두 보드를 각각 다른 역할로 빌드해서 플래시한다.            │
 *  │  · 보드 A(태그, 폰 연결)  : UWB_ROLE = UWB_ROLE_INITIATOR │
 *  │  · 보드 B(앵커)          : UWB_ROLE = UWB_ROLE_RESPONDER │
 *  │ BLE 는 INITIATOR 에서만. X-CUBE-BLE1 활성화 후 1 로.       │
 *  └──────────────────────────────────────────────────────────┘
 */
#include "main.h"             /* HAL, __WFI() */
#include "uwb_app.h"
#include "uwb_config.h"       /* 역할/BLE 설정은 여기서 */
#include "dw3000_twr.h"
#include "dw3000_twr_irq.h"
#include "deca_device_api.h"
#include <stdio.h>
#include <math.h>

#if (UWB_ROLE == UWB_ROLE_INITIATOR) && UWB_ENABLE_BLE
#include "uwb_ble.h"
#endif

#if (UWB_ROLE == UWB_ROLE_INITIATOR)
/* ============== 거리 필터 ==============
 *  스파이크 제거(미디언) + 평활(EMA) + 이상치 게이트.
 *  SS-TWR 1회 측정 노이즈를 줄여 "튀는" 현상을 완화. */
#define FILT_WIN        5
#define JUMP_GATE_M     1.5f   /* 직전 추정 대비 이 이상 점프하면 일시 스파이크로 간주 */
#define EMA_ALPHA       0.3f   /* 작을수록 더 부드럽지만 반응 느림 */

static float filt_buf[FILT_WIN];
static int   filt_cnt = 0, filt_idx = 0;
static float filt_ema = -1.0f;

static float median_of(const float *src, int n)
{
    float a[FILT_WIN];
    for (int i = 0; i < n; i++) a[i] = src[i];
    for (int i = 1; i < n; i++) {              /* insertion sort */
        float k = a[i]; int j = i - 1;
        while (j >= 0 && a[j] > k) { a[j + 1] = a[j]; j--; }
        a[j + 1] = k;
    }
    return a[n / 2];
}

static float dist_filter(float raw)
{
    static int reject_run = 0;
    /* 직전 추정 대비 큰 점프: 일시 스파이크는 최대 3회 무시하되,
       3회 연속이면 '실제 이동'으로 보고 필터를 리셋해 새 거리로 즉시 스냅.
       (예전엔 계속 거부해 값이 멈추는 버그가 있었음) */
    if (filt_ema > 0.0f && fabsf(raw - filt_ema) > JUMP_GATE_M) {
        if (reject_run < 3) { reject_run++; return filt_ema; }
        filt_cnt = 0; filt_idx = 0; filt_ema = -1.0f;   /* 리셋 → 새 위치로 */
    }
    reject_run = 0;

    filt_buf[filt_idx] = raw;
    filt_idx = (filt_idx + 1) % FILT_WIN;
    if (filt_cnt < FILT_WIN) filt_cnt++;

    float med = median_of(filt_buf, filt_cnt);
    if (filt_ema < 0.0f) filt_ema = med;
    else                 filt_ema = EMA_ALPHA * med + (1.0f - EMA_ALPHA) * filt_ema;
    return filt_ema;
}
#endif

void uwb_app_run(void)
{
    dw3000_twr_init();

#if (UWB_ROLE == UWB_ROLE_RESPONDER)
    /* -------- 앵커: poll 받으면 response 송신 무한 반복 -------- */
  #if UWB_USE_IRQ
    printf("UWB RESPONDER (anchor) 시작 [인터럽트 모드]\r\n");
    if (dw3000_twr_irq_init() != 0) {
        printf("IRQ init FAIL - 폴링으로 폴백\r\n");
        while (1) { dw3000_twr_responder_once(); }
    }
    dw3000_twr_irq_responder_start();
    uint32_t hb = HAL_GetTick();
    while (1) {
        float d;
        if (dw3000_twr_irq_responder_poll(&d)) {
            /* 계산은 ISR 이 이미 끝냈고 여기선 로그만 (ISR 안에서 printf 금지) */
            printf("anchor: distance = %d cm\r\n", (int)(d * 100.0f));
        }
        dw3000_twr_irq_responder_service();   /* 무이벤트 워치독 */

        if ((HAL_GetTick() - hb) >= 3000U) {  /* 3초 하트비트 (irq=0 이면 배선 문제) */
            hb = HAL_GetTick();
            printf("anchor: waiting... irq=%lu\r\n",
                   (unsigned long)dw3000_twr_irq_get_irq_count());
        }
        __WFI();                              /* 인터럽트 올 때까지 대기 */
    }
  #else
    printf("UWB RESPONDER (anchor) 시작 [폴링 모드]\r\n");
    while (1) {
        dw3000_twr_responder_once();
    }
  #endif

#else
    /* -------- 태그: 주기적 레인징 + (옵션)BLE notify -------- */
    printf("UWB INITIATOR (tag) 시작\r\n");

  #if UWB_CALIBRATE
    /* ===== 안테나 딜레이 캘리브레이션 모드 =====
       앵커를 UWB_CAL_KNOWN_MM 거리에 정확히 두고 실행. */
    printf("=== 안테나 딜레이 캘리브레이션 (기준 %d mm) ===\r\n", UWB_CAL_KNOWN_MM);
    uint16_t antdly = dw3000_twr_calibrate((float)UWB_CAL_KNOWN_MM / 1000.0f, 100, 12);
    printf("=== 완료: antenna delay = %u (Flash 자동 저장됨) ===\r\n", antdly);
    printf("UWB_CALIBRATE=0 으로 재빌드하면 저장값이 부팅 시 자동 적용됩니다.\r\n");
    while (1) { deca_sleep(1000); }   /* 정지 */
  #endif

  #if UWB_ENABLE_BLE
    uwb_ble_init();
  #endif

  #if UWB_USE_IRQ
    if (dw3000_twr_irq_init() != 0) {
        printf("IRQ init FAIL\r\n");
    }
    printf("[인터럽트 모드] IRQ = PB2(ARD_D8) / EXTI2\r\n");
  #else
    printf("[폴링 모드]\r\n");
  #endif

    uint32_t miss = 0;
    while (1) {
      #if UWB_ENABLE_BLE
        uwb_ble_process();        /* BLE 이벤트 펌프 */
      #endif

        float dist = 0.0f;
        bool  ok;

      #if UWB_USE_IRQ
        /* POLL 만 쏘고 곧바로 리턴. 나머지 3프레임(RESP/FINAL/REPORT)은 ISR 이
           진행한다. 교환이 도는 ~6ms 동안 CPU 는 BLE 를 돌리거나 __WFI() 로 쉰다. */
        dw3000_twr_irq_initiator_start();
        int r;
        while ((r = dw3000_twr_irq_initiator_poll(&dist)) == 0) {
          #if UWB_ENABLE_BLE
            uwb_ble_process();
          #else
            __WFI();
          #endif
        }
        ok = (r > 0);
      #else
        ok = dw3000_twr_initiator_once(&dist);
      #endif

        if (ok) {
            float f = dist_filter(dist);     /* 필터링된 값 */
            printf("distance = %d cm (raw %d)\r\n",
                   (int)(f * 100.0f), (int)(dist * 100.0f));
          #if UWB_ENABLE_BLE
            uwb_ble_update_distance(f);       /* 폰엔 필터값 전송 */
          #endif
            miss = 0;
        } else {
            if (++miss % 20 == 0) {
              #if UWB_USE_IRQ
                /* irq=0 이면 EXTI 가 한 번도 안 떴다 = IRQ 배선/핀맵 문제.
                   (DWM3000EVB IRQ 가 아두이노 D8=PB2 에 와 있는지 확인) */
                printf("(no response from anchor, irq=%lu)\r\n",
                       (unsigned long)dw3000_twr_irq_get_irq_count());
              #else
                printf("(no response from anchor)\r\n");
              #endif
            }
        }

        deca_sleep(100);          /* 약 10 Hz */
    }
#endif
}
