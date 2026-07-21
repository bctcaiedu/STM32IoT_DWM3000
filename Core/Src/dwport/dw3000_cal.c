/**
 * @file    dw3000_cal.c
 * @brief   안테나 딜레이 캘리브레이션 값 Flash 저장/로드 (STM32L4S5, 2MB dual-bank)
 *
 *  저장 위치: Flash 마지막 4KB 페이지 (Bank2, Page255 @ 0x081FF000).
 *            펌웨어 코드는 0x08000000 부근(수십 KB)이라 이 페이지와 겹치지 않음.
 *  형식: 64-bit 더블워드 = [MAGIC(32)] | [antenna_delay(16)]
 *
 *  ⚠ 주의: 펌웨어를 다시 구울 때 "Full chip erase"를 하면 이 페이지도 지워져
 *          저장값이 사라진다. 일반 Download(사용 섹터만 소거)면 유지된다.
 */
#include "dw3000_cal.h"
#include "main.h"     /* HAL_FLASH_* */

/* STM32L4S5VI (2MB, dual-bank 4KB 페이지) 마지막 페이지 */
#define CAL_FLASH_ADDR   0x081FF000UL
#define CAL_FLASH_BANK   FLASH_BANK_2
#define CAL_FLASH_PAGE   255U
#define CAL_MAGIC        0xCA11B0DEUL   /* "CALIB DE" 느낌의 매직 */

/* ------------------------------------------------------------------ */
uint16_t dw3000_cal_load(uint16_t default_delay)
{
    uint64_t v = *(volatile uint64_t *)CAL_FLASH_ADDR;
    if ((uint32_t)(v >> 32) == CAL_MAGIC) {
        return (uint16_t)(v & 0xFFFFU);
    }
    return default_delay;   /* 저장값 없음(소거 상태 0xFFFF..) → 기본값 */
}

bool dw3000_cal_exists(void)
{
    uint64_t v = *(volatile uint64_t *)CAL_FLASH_ADDR;
    return (uint32_t)(v >> 32) == CAL_MAGIC;
}

bool dw3000_cal_save(uint16_t antenna_delay)
{
    uint64_t data = ((uint64_t)CAL_MAGIC << 32) | (uint64_t)antenna_delay;
    FLASH_EraseInitTypeDef er = {0};
    uint32_t pageErr = 0;
    bool ok = false;

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    er.TypeErase = FLASH_TYPEERASE_PAGES;
    er.Banks     = CAL_FLASH_BANK;
    er.Page      = CAL_FLASH_PAGE;
    er.NbPages   = 1;

    if (HAL_FLASHEx_Erase(&er, &pageErr) == HAL_OK) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              CAL_FLASH_ADDR, data) == HAL_OK) {
            ok = true;
        }
    }
    HAL_FLASH_Lock();
    return ok;
}
