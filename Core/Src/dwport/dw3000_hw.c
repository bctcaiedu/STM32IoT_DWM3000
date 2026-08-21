/**
 * @file    dw3000_hw.c
 * @brief   DW3000 HW 플랫폼 계층 (STM32L4 HAL / B-L4S5I-IOT01A)
 *          리셋 / IRQ / wakeup. nrf-sdk dw3000_hw.c 를 STM32 로 이식.
 *
 *  핀맵(아두이노 헤더 기준): RSTn=PA4(D7, Open-Drain), IRQ=PB2(D8), WAKEUP=PA15(D9).
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

    LOG_INF("HW init (RST=PA4, SPIPOL=PA0, SPIPHA=PA1, WAKEUP=PA15, IRQ=PB2)");

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

    /* ★ WAKEUP(PA15 = ARD_D9)=HIGH 로 구동 (칩을 깨어있게). PA15 는 IRQ 가 아니라 WAKEUP 핀. */
    g.Pin   = CONFIG_DW3000_GPIO_WAKEUP_PIN;
    HAL_GPIO_Init(CONFIG_DW3000_GPIO_WAKEUP_PORT, &g);
    HAL_GPIO_WritePin(CONFIG_DW3000_GPIO_WAKEUP_PORT,
                      CONFIG_DW3000_GPIO_WAKEUP_PIN, GPIO_PIN_SET);

    /* ★ IRQ(PB2 = ARD_D8) 는 DW3000 이 push-pull 로 구동하는 '출력'이다.
       CubeMX MX_GPIO_Init 이 이 핀을 OUTPUT_PP/Low 로 잡아두므로, 칩이 IRQ 를
       High 로 올리는 순간 핀 충돌이 난다(폴링 모드에선 인터럽트를 전부 마스크
       해 둬서 드러나지 않았을 뿐). 여기서 무조건 입력으로 되돌린다.
       EXTI 활성화는 dw3000_hw_init_interrupt() 에서. */
    g.Pin   = CONFIG_DW3000_GPIO_IRQ_PIN;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLDOWN;          /* IRQ active-high, 칩 미구동 구간엔 Low 유지 */
    HAL_GPIO_Init(CONFIG_DW3000_GPIO_IRQ_PORT, &g);

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
 * IRQ — PB2(ARD_D8) 상승엣지 EXTI2 → stm32l4xx_it.c 의 EXTI2_IRQHandler
 *       → dw3000_hw_isr() → dwt_isr()
 * ------------------------------------------------------------------ */
int dw3000_hw_init_interrupt(void)
{
    GPIO_InitTypeDef g = {0};

    LOG_INF("IRQ init (PB2 / EXTI2, rising)");

    g.Pin  = CONFIG_DW3000_GPIO_IRQ_PIN;
    g.Mode = GPIO_MODE_IT_RISING;
    g.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(CONFIG_DW3000_GPIO_IRQ_PORT, &g);

    __HAL_GPIO_EXTI_CLEAR_IT(CONFIG_DW3000_GPIO_IRQ_PIN);
    HAL_NVIC_ClearPendingIRQ(CONFIG_DW3000_GPIO_IRQ_EXTI);

    /* DW3000 은 지연송신(delayed TX) 예약 시각을 ISR 안에서 계산해 넣으므로
       레이턴시에 민감하다. BLE(EXTI9_5, BlueNRG PE6)는 SPI3 를 길게 물고
       있을 수 있어 DW 를 더 높은 선점우선순위(작은 숫자)로 둔다.
       (SPI1 ↔ SPI3 로 버스가 달라 서로 선점해도 안전) */
    HAL_NVIC_SetPriority(CONFIG_DW3000_GPIO_IRQ_EXTI, 0, 0);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 3, 0);

    HAL_NVIC_EnableIRQ(CONFIG_DW3000_GPIO_IRQ_EXTI);
    return 0;
}

/* ------------------------------------------------------------------ *
 * EXTI2_IRQHandler 에서 부르는 실제 처리부.
 *
 *  DW3000 의 IRQ 는 '레벨'이다 — enable 된 SYS_STATUS 비트가 하나라도 남아
 *  있으면 High 를 유지한다. 상승엣지 EXTI 로만 받으면, dwt_isr() 처리 중
 *  새 이벤트가 들어와 라인이 계속 High 인 경우 새 엣지가 없어 인터럽트를
 *  영영 놓친다(=레인징 멈춤). 그래서 라인이 Low 로 떨어질 때까지 돈다.
 * ------------------------------------------------------------------ */
static volatile uint32_t s_irq_count = 0;

uint32_t dw3000_hw_get_irq_count(void) { return s_irq_count; }

void dw3000_hw_isr(void)
{
    uint32_t guard = 0;

    s_irq_count++;

    while (HAL_GPIO_ReadPin(CONFIG_DW3000_GPIO_IRQ_PORT,
                            CONFIG_DW3000_GPIO_IRQ_PIN) == GPIO_PIN_SET) {
        dwt_isr();
        if (++guard > 16U) {
            /* 비정상: dwt_isr() 이 못 지우는 이벤트가 계속 셋 되어 있다.
               인터럽트를 죽이면 레인징이 조용히 멈추므로, 상태를 통째로
               클리어해서 라인을 내리고 빠져나온다. */
            dwt_writesysstatuslo(0xFFFFFFFFUL);
            break;
        }
    }
}

void dw3000_hw_interrupt_enable(void)  { HAL_NVIC_EnableIRQ(CONFIG_DW3000_GPIO_IRQ_EXTI); }
void dw3000_hw_interrupt_disable(void) { HAL_NVIC_DisableIRQ(CONFIG_DW3000_GPIO_IRQ_EXTI); }

bool dw3000_hw_interrupt_is_enabled(void)
{
    return NVIC_GetEnableIRQ(CONFIG_DW3000_GPIO_IRQ_EXTI) != 0U;
}
