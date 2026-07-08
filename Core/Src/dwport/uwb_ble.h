/**
 * @file    uwb_ble.h
 * @brief   UWB 거리값을 BLE(GATT)로 폰에 전송 — BlueNRG-MS(X-CUBE-BLE1) 위에 구현.
 *
 *  GATT 구조:
 *    UWB Service          UUID 12345678-1234-5678-1234-56789abcdef0
 *      └ Distance char    UUID 12345678-1234-5678-1234-56789abcdef1
 *                         값: float32 (미터), Read + Notify
 *
 *  사용 순서 (initiator/태그 펌웨어):
 *    1) uwb_ble_init();                    // GATT/GAP 초기화 + 광고 시작
 *    2) 메인 루프:
 *         uwb_ble_process();               // BLE 이벤트 처리 (자주 호출)
 *         if (dw3000_twr_initiator_once(&d))
 *             uwb_ble_update_distance(d);  // 거리 notify
 *
 *  참고: uwb_ble_init() 이 내부에서 hci_init()(전송계층+BlueNRG 하드리셋 포함)을
 *        직접 호출하므로 별도 MX_BlueNRG_MS_Init() 호출은 필요 없음.
 *        BlueNRG IRQ(PE6) EXTI 가 hci_tl_interface 의 ISR 로 큐를 채움.
 */
#ifndef UWB_BLE_H
#define UWB_BLE_H

#include <stdbool.h>

/* GATT/GAP 초기화, UWB 서비스 등록, 광고(discoverable) 시작. 1회 호출. */
void uwb_ble_init(void);

/* BLE 스택 이벤트 펌프. 메인 루프에서 자주 호출. */
void uwb_ble_process(void);

/* 폰이 연결돼 있으면 거리값(미터)을 characteristic 에 써서 notify. */
void uwb_ble_update_distance(float distance_m);

/* 현재 폰 연결 여부. */
bool uwb_ble_is_connected(void);

#endif /* UWB_BLE_H */
