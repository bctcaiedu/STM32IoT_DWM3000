/**
 * @file    deca_port.c
 * @brief   DW3000 드라이버 ↔ 보드 연결 (STM32L4 HAL)
 *          - decamutexon/off, deca_sleep/usleep
 *          - probe 인터페이스 구조체(dw3000_probe_interf) 등록
 *
 *  대상: br101/dw3000-decadriver-source 의 dwt_uwb_driver v08.02.02
 *        (deca_version.h 의 DRIVER_VERSION_HEX >= 0x080202)
 */
#include "deca_device_api.h"
#include "deca_interface.h"
#include "deca_version.h"

#include "dw3000_hw.h"
#include "dw3000_spi.h"
#include "config.h"

/* ------------------------------------------------------------------ *
 * 인터럽트 임계영역
 *   폴링 bring-up 단계에선 DW IRQ 가 enable 안 돼 있어 사실상 no-op.
 *   dwt_isr() 인터럽트를 쓰기 시작하면 자동으로 마스킹된다.
 * ------------------------------------------------------------------ */
decaIrqStatus_t decamutexon(void)
{
    bool s = dw3000_hw_interrupt_is_enabled();
    if (s) dw3000_hw_interrupt_disable();
    return (decaIrqStatus_t)s;
}

void decamutexoff(decaIrqStatus_t s)
{
    if (s) dw3000_hw_interrupt_enable();
}

/* ------------------------------------------------------------------ *
 * 지연
 * ------------------------------------------------------------------ */
void deca_sleep(unsigned int time_ms)
{
    HAL_Delay(time_ms);
}

void deca_usleep(unsigned long time_us)
{
    /* 간이 us 지연. 정밀 ranging 단계에선 타이머/DWT 사이클카운터로 교체 권장. */
    uint32_t loops = (HAL_RCC_GetHCLKFreq() / 4000000UL) * (uint32_t)time_us;
    while (loops--) { __NOP(); }
}

void wakeup_device_with_io(void)
{
    dw3000_hw_wakeup();
}

/* ------------------------------------------------------------------ *
 * SPI 함수 묶음 + probe 인터페이스
 *   dwt_probe(&dw3000_probe_interf) 로 드라이버에 등록한다.
 * ------------------------------------------------------------------ */
static const struct dwt_spi_s dw3000_spi_fct = {
    .readfromspi       = dw3000_spi_read,
    .writetospi        = dw3000_spi_write,
    .writetospiwithcrc = dw3000_spi_write_crc,
    .setslowrate       = dw3000_spi_speed_slow,
    .setfastrate       = dw3000_spi_speed_fast,
};

extern const struct dwt_driver_s dw3000_driver;
static const struct dwt_driver_s* dw3000_driver_list[] = { &dw3000_driver };

const struct dwt_probe_s dw3000_probe_interf = {
    .dw                    = NULL,
    .spi                   = (void*)&dw3000_spi_fct,
    .wakeup_device_with_io = dw3000_hw_wakeup,
    .driver_list           = (struct dwt_driver_s**)dw3000_driver_list,
    .dw_driver_num         = 1,
};
