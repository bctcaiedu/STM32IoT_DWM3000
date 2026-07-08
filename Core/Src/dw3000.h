/**
 * @file    dw3000.h
 * @brief   DW3000(DWM3000) 최소 드라이버 - 공개 API
 */
#ifndef DW3000_H
#define DW3000_H

#include <stdint.h>
#include "dw3000_regs.h"

/* 반환 코드 */
#define DW_OK     0
#define DW_ERR   (-1)

/* ---------------- 저수준 레지스터 접근 ---------------- */
uint32_t dw_read32(uint32_t regId);
uint16_t dw_read16(uint32_t regId);
uint8_t  dw_read8 (uint32_t regId);
void     dw_write32(uint32_t regId, uint32_t val);
void     dw_write16(uint32_t regId, uint16_t val);
void     dw_write8 (uint32_t regId, uint8_t  val);

/* 임의 길이 read/write (regId = 파일ID|오프셋) */
void     dw_read_buf (uint32_t regId, uint8_t *data, uint16_t len);
void     dw_write_buf(uint32_t regId, const uint8_t *data, uint16_t len);

/* 1바이트 fast command (DW3000_CMD_xxx) */
void     dw_fast_cmd(uint8_t cmd);

/* ---------------- 고수준 API ---------------- */

/**
 * @brief  DW3000 초기화: HW 리셋 → SPI 통신 확인 → IDLE_RC 대기 → 기본 설정.
 * @return DW_OK 성공, DW_ERR 실패(Device ID 불일치 등)
 */
int      dw3000_init(void);

/** @brief  Device ID 읽기 (정상 시 0xDECA0302) */
uint32_t dw3000_read_dev_id(void);

/**
 * @brief  프레임 송신 (blocking). FCS(2바이트)는 칩이 자동 부가.
 * @param  data  송신할 페이로드
 * @param  len   페이로드 길이 (바이트)
 * @return DW_OK 송신 완료, DW_ERR 타임아웃
 */
int      dw3000_send(const uint8_t *data, uint16_t len);

/**
 * @brief  프레임 수신 (blocking). 한 프레임을 받을 때까지 대기.
 * @param  buf      수신 버퍼
 * @param  bufSize  버퍼 크기
 * @param  outLen   실제 수신된 페이로드 길이(FCS 제외) 반환
 * @param  timeoutMs  0 이면 무한 대기
 * @return DW_OK 정상 수신, DW_ERR 에러/타임아웃
 */
int      dw3000_receive(uint8_t *buf, uint16_t bufSize,
                        uint16_t *outLen, uint32_t timeoutMs);

#endif /* DW3000_H */
