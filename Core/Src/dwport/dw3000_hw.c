/**
 * @file    dw3000_hw.c
 * @brief   DW3000 HW 플랫폼 계층 (STM32L4 HAL / B-L4S5I-IOT01A)
 *          리셋 / IRQ / wakeup. nrf-sdk dw3000_hw.c 를 STM32 로 이식.
 *
 *  검증값: RSTn=PA4 (Open-Drain, Low 펄스로 리셋), IRQ=PA15.
 */
#include "deca_device_api.h"
#include "dw3000_hw.h"
#include "dw3000_spi.h"
#include "config.h"
#include "log.h"

/* ------------------------------------------------------------------ */
int dw3000_hw_init(void)
{
    GPIO_InitTypeDef g = {0};

    LOG_INF("HW init (RST=PA4, SPIPOL=PA0, SPIPHA=PA1, WAKEUP=PA15)");

    /* RESET: 입력(Hi-Z)으로 두고 칩이 reset 에서 빠져나왔는지(High) 확인 */
    g.Pin  = CONFIG_DW3000_GPIO_RESET_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(CONFIG_DW3000_GPIO_RESET_PORT, &g);

    int timeout = 1000;
    while (HAL_GPIO_ReadPin(CONFIG_DW3000_GPIO_RESET_PORT,
                            CONFIG_DW3000_GPIO_RESET_PIN) == GPIO_PIN_RESET
           && --timeout > 0) {
        HAL_Delay(1);
    }
    if (timeout <= 0) {
        LOG_ERR("did not come out of reset");
        return -1;
    }

    /* ★ DW3000 SPI 모드 선택 핀 — 레퍼런스(검증됨)와 동일하게 구동.
       SPIPOL(PA0)=HIGH, SPIPHA(PA1)=HIGH → 칩을 SPI Mode 0 으로 고정.
       (이 핀들을 띄워두면 칩 SPI 모드가 미정의 → 2-octet 쓰기 깨짐 → PLL 실패) */
    g.Pin   = GPIO_PIN_0 | GPIO_PIN_1;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0 | GPIO_PIN_1, GPIO_PIN_SET);  /* 둘 다 HIGH */

    /* ★ WAKEUP(PA15)=HIGH 로 구동 (칩을 깨어있게). PA15 는 IRQ 가 아니라 WAKEUP 핀.
       진짜 IRQ 는 D8 이며 폴링 브링업에서는 미사용. */
    g.Pin   = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, GPIO_PIN_SET);

    return dw3000_spi_init();
}

void dw3000_hw_fini(void) { dw3000_spi_fini(); }

/* ------------------------------------------------------------------ *
 * 하드 리셋: RSTn 을 Open-Drain Low → 해제(Hi-Z)
 *   (검증: PA4 Low 시 DEV_ID=0, 해제 시 0xDECA0302)
 * ------------------------------------------------------------------ */
void dw3000_hw_reset(void)
{
    GPIO_InitTypeDef g = {0};
    LOG_INF("HW reset");

    /* 레퍼런스 방식: Low(assert) → Hi-Z(해제). RSTn 은 open-drain 이므로
       절대 High 로 능동구동하지 않는다(칩과 충돌). 해제 시 칩 내부 풀업이 High 로. */
    g.Pin   = CONFIG_DW3000_GPIO_RESET_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CONFIG_DW3000_GPIO_RESET_PORT, &g);

    HAL_GPIO_WritePin(CONFIG_DW3000_GPIO_RESET_PORT,
                      CONFIG_DW3000_GPIO_RESET_PIN, GPIO_PIN_RESET);  /* assert low */
    HAL_Delay(2);

    g.Mode = GPIO_MODE_INPUT;     /* 해제(Hi-Z) — 칩 풀업으로 라이징 */
    HAL_GPIO_Init(CONFIG_DW3000_GPIO_RESET_PORT, &g);
    HAL_Delay(2);
}

void dw3000_hw_wakeup(void)
{
    /* WAKEUP 핀 미사용: CS 를 잠깐 Low 로 떨궈 깨움 */
    HAL_GPIO_WritePin(CONFIG_DW3000_SPI_CS_PORT, CONFIG_DW3000_SPI_CS_PIN, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(CONFIG_DW3000_SPI_CS_PORT, CONFIG_DW3000_SPI_CS_PIN, GPIO_PIN_SET);
    HAL_Delay(1);
}

void dw3000_hw_wakeup_pin_low(void) { /* WAKEUP 핀 미사용 */ }

/* ------------------------------------------------------------------ *
 * IRQ — 폴링 우선 bring-up 이면 미사용. EXTI 로 dwt_isr() 호출 시 사용.
 *   EXTI 설정/콜백은 CubeMX 또는 stm32l4xx_it.c 에서 PA15(EXTI15_10)로 연결.
 * ------------------------------------------------------------------ */
int dw3000_hw_init_interrupt(void)
{
    /* CubeMX 에서 PA15 를 GPIO_EXTI(rising) 로 설정하고 NVIC EXTI15_10 enable.
       여기서는 NVIC enable 만. */
    HAL_NVIC_SetPriority(CONFIG_DW3000_GPIO_IRQ_EXTI, 5, 0);
    HAL_NVIC_EnableIRQ(CONFIG_DW3000_GPIO_IRQ_EXTI);
    return 0;
}

void dw3000_hw_interrupt_enable(void)  { HAL_NVIC_EnableIRQ(CONFIG_DW3000_GPIO_IRQ_EXTI); }
void dw3000_hw_interrupt_disable(void) { HAL_NVIC_DisableIRQ(CONFIG_DW3000_GPIO_IRQ_EXTI); }

bool dw3000_hw_interrupt_is_enabled(void)
{
    return NVIC_GetEnableIRQ(CONFIG_DW3000_GPIO_IRQ_EXTI) != 0U;
}
