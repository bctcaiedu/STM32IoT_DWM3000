/**
 * @file    uwb_app.h
 * @brief   UWB 레인징 + (옵션)BLE 통합 실행 진입점.
 *          dw3000_app_init() 가 0 을 리턴한 뒤 main 에서 uwb_app_run() 호출.
 */
#ifndef UWB_APP_H
#define UWB_APP_H

void uwb_app_run(void);   /* 무한 루프 (리턴 안 함) */

#endif /* UWB_APP_H */
