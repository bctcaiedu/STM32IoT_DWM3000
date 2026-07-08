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
#include "uwb_app.h"
#include "uwb_config.h"       /* 역할/BLE 설정은 여기서 */
#include "dw3000_twr.h"
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
    /* 큰 점프는 일시 스파이크로 보고 최대 3회까지 무시(진짜 이동이면 곧 수용) */
    if (filt_ema > 0.0f && fabsf(raw - filt_ema) > JUMP_GATE_M && reject_run < 3) {
        reject_run++;
        return filt_ema;
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
    printf("UWB RESPONDER (anchor) 시작\r\n");
    while (1) {
        dw3000_twr_responder_once();
    }

#else
    /* -------- 태그: 주기적 레인징 + (옵션)BLE notify -------- */
    printf("UWB INITIATOR (tag) 시작\r\n");
  #if UWB_ENABLE_BLE
    uwb_ble_init();
  #endif

    uint32_t miss = 0;
    while (1) {
      #if UWB_ENABLE_BLE
        uwb_ble_process();        /* BLE 이벤트 펌프 */
      #endif

        float dist;
        if (dw3000_twr_initiator_once(&dist)) {
            float f = dist_filter(dist);     /* 필터링된 값 */
            printf("distance = %d cm (raw %d)\r\n",
                   (int)(f * 100.0f), (int)(dist * 100.0f));
          #if UWB_ENABLE_BLE
            uwb_ble_update_distance(f);       /* 폰엔 필터값 전송 */
          #endif
            miss = 0;
        } else {
            if (++miss % 20 == 0) printf("(no response from anchor)\r\n");
        }

        deca_sleep(100);          /* 약 10 Hz */
    }
#endif
}
