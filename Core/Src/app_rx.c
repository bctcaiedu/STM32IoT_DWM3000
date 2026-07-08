/**
 * @file    app_rx.c
 * @brief   수신측 예제 - 프레임 수신 후 UART 로 출력
 *
 * 사용법:
 *   app_rx_setup() 한 번 호출, while(1) 에서 app_rx_loop() 반복.
 *   수신 내용을 PC 로 보고 싶으면 CubeMX 에서 USART(예: huart1) 활성화 후
 *   아래 huart 핸들을 맞춰주세요. (B-L4S5I 의 ST-LINK VCP 사용 가능)
 */
#include "main.h"
#include "dw3000.h"
#include "dw3000_port.h"
#include <string.h>
#include <stdio.h>

/* UART 로 로그를 보고 싶다면 주석 해제하고 핸들을 맞추세요. */
extern UART_HandleTypeDef huart1;

static void rx_log(const char *s)
{
    /* UART 출력 (옵션) */
    HAL_UART_Transmit(&huart1, (uint8_t *)s, strlen(s), 100);
    (void)s;
}

void app_rx_setup(void)
{
    char line[64];

    /* ---- 디버그: RST(PA4) 핀이 실제로 칩에 연결됐는지 판정 ----
       RSTn 을 LOW 로 잡은 채 DEV_ID 를 읽는다.
       - RST 연결됨: 칩이 리셋되어 DEVID != 0xDECA0302 (0 또는 0xFFFFFFFF)
       - RST 미연결: 칩이 멀쩡해서 DEVID == 0xDECA0302 그대로  */
    {
        char dbg[112];
        GPIO_InitTypeDef g = {0};
        uint32_t id_reset, id_free;

        dw_port_cs_high();
        dw_port_spi_set_slow();

        /* RSTn(PA4) 을 Open-Drain LOW 로 구동(리셋 어서트) */
        g.Pin = GPIO_PIN_4; g.Mode = GPIO_MODE_OUTPUT_OD;
        g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOA, &g);
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);
        HAL_Delay(3);
        id_reset = dw_read32(DW3000_DEV_ID);

        /* RSTn 해제(Hi-Z) */
        g.Mode = GPIO_MODE_INPUT; HAL_GPIO_Init(GPIOA, &g);
        HAL_Delay(5);
        id_free = dw_read32(DW3000_DEV_ID);

        snprintf(dbg, sizeof(dbg),
                 "RST=LOW DEVID=0x%08lX | RST=free DEVID=0x%08lX\r\n",
                 (unsigned long)id_reset, (unsigned long)id_free);
        rx_log(dbg);
        rx_log("(RST=LOW 에서도 0xDECA0302 면 RST 핀 미연결)\r\n");
    }
    /* ----------------------------------------- */

    if (dw3000_init() != DW_OK)
    {
        snprintf(line, sizeof(line),
                 "DW3000 init FAIL (DEVID=0x%08lX)\r\n",
                 (unsigned long)dw3000_read_dev_id());
        rx_log(line);
        while (1)
        {
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
            HAL_Delay(100);
        }
    }
    snprintf(line, sizeof(line),
             "DW3000 RX ready (DEVID=0x%08lX)\r\n",
             (unsigned long)dw3000_read_dev_id());
    rx_log(line);
}

void app_rx_loop(void)
{
    uint8_t  buf[127];
    uint16_t len = 0;
    char     line[160];

    /* 한 프레임 수신 (2초 타임아웃) */
    if (dw3000_receive(buf, sizeof(buf) - 1, &len, 2000) == DW_OK)
    {
        buf[len] = '\0';  /* 문자열 출력용 널 종단 */

        snprintf(line, sizeof(line), "RX(%u): %s\r\n", (unsigned)len, (char *)buf);
        rx_log(line);

        /* 수신 성공 시 LED 토글 */
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    }
    else
    {
        rx_log("RX timeout/err\r\n");
    }
}

/* ---- main() 직접 사용 예시 ----
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();
    // MX_USART1_UART_Init();

    app_rx_setup();
    while (1) { app_rx_loop(); }
}
-------------------------------- */
