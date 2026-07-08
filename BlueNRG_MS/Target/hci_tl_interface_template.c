/**
  ******************************************************************************
  * @file    hci_tl_interface_template.c
  * @brief   BlueNRG-MS HCI Transport Layer 인터페이스 구현
  *          (B-L4S5I-IOT01A 온보드 SPBTLE-RF: SPI3, CS=PD13, IRQ=PE6, RST=PA8)
  ******************************************************************************
  */
#include "hci_tl_interface.h"
#include "hci_tl.h"
#include "main.h"
#include <string.h>

/* main.c 에서 생성된 SPI3 핸들 */
extern SPI_HandleTypeDef hspi3;

/* 온보드 SPBTLE-RF 핀 (main.h 매크로) */
#define HCI_CS_PORT     SPBTLE_RF_SPI3_CSN_GPIO_Port   /* PD13 */
#define HCI_CS_PIN      SPBTLE_RF_SPI3_CSN_Pin
#define HCI_IRQ_PORT    SPBTLE_RF_IRQ_EXTI6_GPIO_Port  /* PE6  */
#define HCI_IRQ_PIN     SPBTLE_RF_IRQ_EXTI6_Pin
#define HCI_RST_PORT    SPBTLE_RF_RST_GPIO_Port        /* PA8  */
#define HCI_RST_PIN     SPBTLE_RF_RST_Pin

#define HEADER_SIZE       5U
#define MAX_BUFFER_SIZE   255U
#define TIMEOUT_DURATION  15U
#define TIMEOUT_SEND_MS   1000U

/* BlueNRG IRQ 핀이 High = 읽을 데이터 있음 */
static int32_t IsDataAvailable(void)
{
    return (HAL_GPIO_ReadPin(HCI_IRQ_PORT, HCI_IRQ_PIN) == GPIO_PIN_SET);
}

static int32_t hci_get_tick(void) { return (int32_t)HAL_GetTick(); }

/* ---- IO bus 함수 ---- */
int32_t HCI_TL_SPI_Init(void* pConf)
{
    (void)pConf;
    /* BlueNRG-MS 용 SPI 재설정:
       - 8비트 전송 (보드 기본이 4BIT)
       - 분주비 ↓ : BlueNRG-MS 는 ~8MHz 이하 권장 (기본 /4 는 너무 빠름) */
    hspi3.Init.DataSize         = SPI_DATASIZE_8BIT;
    hspi3.Init.NSS              = SPI_NSS_SOFT;
    hspi3.Init.NSSPMode         = SPI_NSS_PULSE_DISABLE;
    hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;   /* 안전하게 저속 */
    HAL_SPI_Init(&hspi3);
    HAL_GPIO_WritePin(HCI_CS_PORT, HCI_CS_PIN, GPIO_PIN_SET);  /* CS idle High */
    return 0;
}

int32_t HCI_TL_SPI_DeInit(void) { return 0; }

/* BlueNRG-MS 하드 리셋: RST Low 펄스 */
int32_t HCI_TL_SPI_Reset(void)
{
    HAL_GPIO_WritePin(HCI_RST_PORT, HCI_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(5);
    HAL_GPIO_WritePin(HCI_RST_PORT, HCI_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(5);
    return 0;
}

/* SPI 수신: 헤더(0x0b) 보내고 ready(0x02) 확인 후 페이로드 읽기 */
int32_t HCI_TL_SPI_Receive(uint8_t* buffer, uint16_t size)
{
    uint16_t byte_count;
    uint8_t  len = 0;
    uint8_t  header_master[HEADER_SIZE] = {0x0b, 0x00, 0x00, 0x00, 0x00};
    uint8_t  header_slave[HEADER_SIZE];

    HAL_GPIO_WritePin(HCI_CS_PORT, HCI_CS_PIN, GPIO_PIN_RESET);

    HAL_SPI_TransmitReceive(&hspi3, header_master, header_slave, HEADER_SIZE, TIMEOUT_DURATION);

    if (header_slave[0] == 0x02) {                        /* BlueNRG ready */
        byte_count = (uint16_t)((header_slave[4] << 8) | header_slave[3]);
        if (byte_count > 0U) {
            if (byte_count > size) byte_count = size;
            for (len = 0; len < byte_count; len++) {
                uint8_t dummy = 0xFF, rx = 0;
                HAL_SPI_TransmitReceive(&hspi3, &dummy, &rx, 1, TIMEOUT_DURATION);
                buffer[len] = rx;
            }
        }
    }
    HAL_GPIO_WritePin(HCI_CS_PORT, HCI_CS_PIN, GPIO_PIN_SET);
    return len;
}

/* SPI 송신: 헤더(0x0a) 보내고 ready + 버퍼여유 확인 후 페이로드 송신 */
int32_t HCI_TL_SPI_Send(uint8_t* buffer, uint16_t size)
{
    int32_t  result;
    uint16_t tx_space;
    uint8_t  header_master[HEADER_SIZE] = {0x0a, 0x00, 0x00, 0x00, 0x00};
    uint8_t  header_slave[HEADER_SIZE];
    static uint8_t scratch[MAX_BUFFER_SIZE];
    uint32_t tickstart = HAL_GetTick();

    do {
        result = -2;
        HAL_GPIO_WritePin(HCI_CS_PORT, HCI_CS_PIN, GPIO_PIN_RESET);
        HAL_SPI_TransmitReceive(&hspi3, header_master, header_slave, HEADER_SIZE, TIMEOUT_DURATION);
        tx_space = (uint16_t)((header_slave[2] << 8) | header_slave[1]);
        if (header_slave[0] == 0x02 && tx_space >= size) {
            HAL_SPI_TransmitReceive(&hspi3, buffer, scratch, size, TIMEOUT_DURATION);
            result = 0;
        }
        HAL_GPIO_WritePin(HCI_CS_PORT, HCI_CS_PIN, GPIO_PIN_SET);

        if (result == 0) break;
        if ((HAL_GetTick() - tickstart) > TIMEOUT_SEND_MS) { result = -3; break; }
    } while (1);

    return result;
}

/* ---- 스택이 부르는 진입점 ---- */
void hci_tl_lowlevel_init(void)
{
    tHciIO fops;
    memset(&fops, 0, sizeof(fops));
    fops.Init    = HCI_TL_SPI_Init;
    fops.DeInit  = HCI_TL_SPI_DeInit;
    fops.Send    = HCI_TL_SPI_Send;
    fops.Receive = HCI_TL_SPI_Receive;
    fops.Reset   = HCI_TL_SPI_Reset;
    fops.GetTick = hci_get_tick;
    hci_register_io_bus(&fops);
    /* PE6 EXTI(상승엣지) + EXTI9_5 NVIC 는 CubeMX(MX_GPIO_Init)에서 이미 설정됨.
       인터럽트 → hci_tl_lowlevel_isr 라우팅은 아래 HAL_GPIO_EXTI_Callback 에서. */
}

void hci_tl_lowlevel_isr(void)
{
    while (IsDataAvailable()) {
        hci_notify_asynch_evt(NULL);
    }
}

/* EXTI9_5_IRQHandler → HAL_GPIO_EXTI_IRQHandler(PE6) → 여기로.
   PE6(SPBTLE_RF IRQ)만 BlueNRG 로 라우팅. */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == HCI_IRQ_PIN) {
        hci_tl_lowlevel_isr();
    }
}
