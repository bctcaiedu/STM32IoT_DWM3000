/**
 * @file    app_tx.c
 * @brief   송신측 예제 - "WAVE" + 카운터를 1초마다 전송
 *
 * 사용법:
 *   main.c 의 USER CODE 영역에서 app_tx_setup() 을 한 번,
 *   while(1) 루프에서 app_tx_loop() 를 호출하세요.
 *   (또는 main() 을 직접 이 코드로 대체)
 */
#include "main.h"
#include "dw3000.h"
#include <string.h>
#include <stdio.h>

static uint32_t tx_count = 0;

void app_tx_setup(void)
{
    if (dw3000_init() != DW_OK)
    {
        /* 초기화 실패: LED 빠르게 깜빡여 알림 (LED2 = PA5 예시) */
        while (1)
        {
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            HAL_Delay(100);
        }
    }
}

void app_tx_loop(void)
{
    uint8_t  msg[32];
    int      n;

    /* 페이로드: "WAVE <count>" */
    n = snprintf((char *)msg, sizeof(msg), "WAVE %lu", (unsigned long)tx_count);
    if (n < 0) n = 0;

    if (dw3000_send(msg, (uint16_t)n) == DW_OK)
    {
        /* 송신 성공 시 LED 토글 */
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        tx_count++;
    }

    HAL_Delay(1000);
}

/* ---- main() 을 직접 쓰고 싶다면 아래 형태 참고 ----
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();

    app_tx_setup();
    while (1) { app_tx_loop(); }
}
---------------------------------------------------- */
