# B-L4S5I + DWM3000 (DW3000) SPI 통신 코드

STMicroelectronics **B-L4S5I-IOT01A** (STM32L4S5VI) 보드에서 Qorvo **DWM3000** (DW3000 UWB 칩) 을
**STM32 HAL SPI** 만으로 제어하는 최소 드라이버입니다. Qorvo 정식 드라이버 의존성이 없습니다.

목표: 두 보드 간 UWB 프레임 **송신(TX) / 수신(RX)**.

---

## 1. 파일 구성

| 파일 | 설명 |
|------|------|
| `dw3000_regs.h` | DW3000 레지스터 주소 / 비트 정의 |
| `dw3000_port.h/.c` | STM32 HAL 포팅 (SPI, CS, RST, IRQ, delay) — **보드 의존부** |
| `dw3000.h/.c` | 드라이버 코어 (레지스터 R/W, fast command, init, send, receive) |
| `app_tx.c` | 송신 예제 (1초마다 "WAVE n" 전송) |
| `app_rx.c` | 수신 예제 (수신 후 UART 출력) |

---

## 2. 하드웨어 배선 (DWM3000EVB Arduino 쉴드 기준)

DWM3000EVB 는 Arduino 폼팩터라 B-L4S5I-IOT01A 의 Arduino 헤더에 그대로 꽂힙니다.
SPI 는 보드의 **Arduino SPI(=SPI1)** 핀을 사용합니다.

| 신호 | Arduino 핀 | STM32 핀(예시) | 비고 |
|------|-----------|----------------|------|
| SCK  | D13 | PA5 | SPI1_SCK |
| MISO | D12 | PA6 | SPI1_MISO |
| MOSI | D11 | PA7 | SPI1_MOSI |
| CS   | D10 | PD14 | GPIO Output (기본 High) |
| RSTn | D9? | PG12 | GPIO, **Open-Drain** |
| IRQ  | D8? | PG11 | GPIO Input (폴링이면 미사용 가능) |
| 3V3 / GND | - | - | 전원 |

> ⚠️ **핀 번호는 반드시 본인 배선/CubeMX 설정으로 확인하세요.**
> 위 STM32 핀은 일반적인 예시이며, 보드 리비전·쉴드 매핑에 따라 다를 수 있습니다.
> 실제 CubeMX 핀 라벨을 `dw3000_port.h` 의 `DW_CS_*`, `DW_RST_*`, `DW_IRQ_*` 매크로에 맞추면 됩니다.

---

## 3. CubeMX(.ioc) 설정

1. **SPI1** 활성화
   - Mode: `Full-Duplex Master`
   - Data Size: **8 Bits**, First Bit: **MSB First**
   - **CPOL = Low, CPHA = 1 Edge** (SPI Mode 0) — DW3000 은 Mode 0 사용
   - Baud Rate: 초기엔 **≤ 2 MHz** 권장 (코드에서 `dw_port_spi_set_slow/fast()` 로 전환)
2. **GPIO**
   - CS 핀: `GPIO_Output`, 초기값 High, User Label = `DW_CS`
   - RST 핀: `GPIO_Output` (코드에서 런타임에 Open-Drain 으로 재설정), User Label = `DW_RST`
   - IRQ 핀: `GPIO_Input` (또는 EXTI), User Label = `DW_IRQ`
3. (옵션) **USART1** 활성화 — 수신 로그를 PC 로 보고 싶을 때 (ST-LINK VCP)
4. 코드 생성 후, 위 6개 파일을 `Core/Src`, `Core/Inc` (또는 임의 폴더)에 추가하고
   include path 에 포함시키세요.

> User Label 을 `DW_CS` / `DW_RST` / `DW_IRQ` 로 지정하면 CubeMX 가
> `DW_CS_Pin`, `DW_CS_GPIO_Port` 매크로를 자동 생성하므로 `dw3000_port.h` 를
> 수정할 필요가 없습니다.

---

## 4. 사용법

`main.c` 의 USER CODE 영역:

```c
/* USER CODE BEGIN Includes */
#include "dw3000.h"
/* USER CODE END Includes */

/* --- 송신 보드 --- */
/* USER CODE BEGIN 2 */
app_tx_setup();
/* USER CODE END 2 */
/* USER CODE BEGIN WHILE */
while (1) {
    app_tx_loop();
}

/* --- 수신 보드 --- */
/* USER CODE BEGIN 2 */
app_rx_setup();
/* USER CODE END 2 */
while (1) {
    app_rx_loop();
}
```

보드 2개에 각각 TX/RX 펌웨어를 올리면 송신 보드가 1초마다 `"WAVE n"` 을 보내고
수신 보드가 받아 LED 를 토글(+UART 출력)합니다.

---

## 5. 통신이 안 될 때 점검 순서

1. **Device ID 확인이 1순위.** `dw3000_read_dev_id()` 가 `0xDECA0302` 를 반환해야 합니다.
   - 다른 값(0x00000000, 0xFFFFFFFF)이면 → SPI 배선(특히 MISO/CS), SPI 모드(CPOL/CPHA), 전원 문제.
2. SPI 클럭을 초기엔 2MHz 이하로.
3. RSTn 을 Open-Drain 으로 다뤘는지 확인 (High 강제 구동 금지).
4. CS 가 각 트랜잭션마다 Low→High 로 토글되는지 (본 코드가 자동 처리).

---

## 6. 한계와 다음 단계 (중요)

이 드라이버는 **SPI 통신 검증 + 단순 프레임 송수신** 에 초점을 둔 최소 구현입니다.
다음 사항을 알아두세요.

- **RF/PLL 정밀 설정 미포함**: 채널5·PRF64·preamble code 9 의 일반적 기본값만 설정합니다.
  링크가 불안정하거나 거리가 짧게만 동작하면, Qorvo DW3000 API 의 `dwt_configure()` /
  PLL 캘리브레이션(`dwt_initialise` 의 OTP/RF 시퀀스) 값을 `dw3000_default_config()` 에
  추가 반영해야 합니다.
- **레인징(거리측정) 미포함**: 본 코드는 메시지 송수신까지만 다룹니다. TWR(Two-Way Ranging)
  은 TX/RX 타임스탬프(`RX_TIME`/`TX_TIME`)와 지연송신(CMD_DTX)을 추가로 사용해야 합니다.
- **폴링 방식**: IRQ 핀 기반 인터럽트 구동이 아니라 SYS_STATUS 폴링입니다.
  저전력/고성능이 필요하면 EXTI 인터럽트로 전환하세요.
- **레지스터 오프셋 검증**: 핵심 레지스터는 DW3000 User Manual / Qorvo API 기준이지만,
  실제 칩 리비전에서 한 번 더 대조 검증하는 것을 권장합니다.

가장 먼저 할 일: **Device ID 가 `0xDECA0302` 로 읽히는지** 확인하세요. 이게 되면 배선과
SPI 설정은 정상이고, 이후 TX/RX 와 RF 튜닝으로 넘어가면 됩니다.
