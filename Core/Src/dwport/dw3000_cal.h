/**
 * @file    dw3000_cal.h
 * @brief   안테나 딜레이 캘리브레이션 값 영구 저장 (STM32L4 내부 Flash)
 *
 *  캘리브레이션으로 구한 안테나 딜레이를 Flash 마지막 페이지에 저장하고,
 *  부팅 시 로드해서 재컴파일 없이 유지한다.
 */
#ifndef DW3000_CAL_H
#define DW3000_CAL_H

#include <stdint.h>
#include <stdbool.h>

/* 안테나 딜레이를 Flash 에 저장 (성공 true) */
bool dw3000_cal_save(uint16_t antenna_delay);

/* Flash 에서 로드. 유효한 저장값이 없으면 default_delay 반환 */
uint16_t dw3000_cal_load(uint16_t default_delay);

/* 저장값 존재 여부 */
bool dw3000_cal_exists(void);

#endif /* DW3000_CAL_H */
