# DW3000(DWM3000) — Qorvo 정식 드라이버 STM32L4(B-L4S5I-IOT01A) 이식 가이드

## 0. 배경 — 검증 완료된 보드 의존 값

직접 만든 미니 드라이버로 디버깅하며 **보드 의존 계층을 전부 확정**했고, 그대로 재사용합니다.

| 항목 | 값 |
|------|-----|
| SPI | `SPI1`(`hspi1`), **Mode 3** (CPOL=High, CPHA=2Edge) |
| SCK/MISO/MOSI | PA5 / PA6 / PA7 |
| CS | **PA2** (idle High) |
| RST | **PA4** (Open-Drain, Low 펄스) |
| IRQ | **PA15** |

DEV_ID(0xDECA0302) 읽기, 레지스터 read/write/clear, 하드리셋 모두 검증 완료.
남은 문제(RCINIT/IDLE_RC 미진입)는 OTP/PLL 등 정식 부팅 시퀀스가 필요 → 이 드라이버로 해결.

---

## 1. 드라이버 소스 받기 (베어메탈, Zephyr 의존 없음)

**저장소:** https://github.com/br101/dw3000-decadriver-source
(Qorvo 정식 `dwt_uwb_driver` v08.02.02 소스 + 플랫폼 코드. ISC/Qorvo 라이선스)

받는 법: 저장소 페이지 → **Code ▸ Download ZIP** → 압축 해제.

필요한 것은 **`dwt_uwb_driver/` 폴더 통째**입니다 (수정 불필요). 안에 들어있는 것:
```
deca_device.c              ← 핵심 구현 (dwt_initialise/dwt_configure/dwt_xfer3000 등)
deca_device_api.h
deca_interface.h           ← dwt_spi_s / dwt_probe_s / dwt_driver_s 구조체
deca_probe_interface.h
deca_regs.h, deca_types.h, deca_version.h, deca_vals.h, deca_rsl.h ...
dw3000_*.c (칩별 드라이버, dw3000_driver 심볼 포함)
```
> `platform/` 폴더(zephyr/esp-idf/nrf-sdk)는 **안 씁니다.** STM32 플랫폼은 우리가 `Core/Src/dwport/`에 이미 만들어 두었습니다.

이 드라이버 코어가 우리가 손으로 디버깅한 모든 것(1/2-octet 헤더, EAMRW 0x40 마커, SYS_STATUS 비트, IDLE_RC 시퀀스)을 **정확히 내장**하고 있습니다.

---

## 2. CubeIDE 프로젝트에 추가

### 2-1. 드라이버 파일 복사 (ZIP 다운로드의 실제 파일로)

> ⚠ 큰 파일은 자동 전송 불가(크기 제한)라 **반드시 ZIP에서 수동 복사**. 폴더 구조와
> placeholder 는 `Drivers/dwt_uwb_driver/` 에 미리 만들어 두었으니, ZIP의 동일 경로
> 파일로 **덮어쓰기**만 하면 됩니다.

빌드에 필요한 파일(나머지 CMake/utest/zephyr/esp/nrf 폴더는 불필요):

> ★★ 중요 — `deca_compat.c` 는 repo에 **두 개**가 있습니다.
> 빌드에 쓸 것은 **`platform/deca_compat.c`** (ioctl 제거판) 이며,
> `dwt_uwb_driver/deca_compat.c`(원본, ioctl 사용)를 쓰면
> `'struct dwt_ops_s' has no member named 'ioctl'` 에러가 납니다.
> → ZIP의 `platform/deca_compat.c` 와 `platform/deca_ull.h` 를
>   `Drivers/dwt_uwb_driver/` 에 넣으세요(전자는 덮어쓰기).
>
> 또한 `CONFIG_DW3000_CHIP_DW3000=1` 를 **컴파일러 Define 심볼**에 추가해야
> 칩 레지스터 헤더가 include 됩니다 (.cproject 에 이미 추가해 둠).

```
Drivers/dwt_uwb_driver/
  deca_compat.c              ← ★ ZIP의 platform/deca_compat.c 로 덮어쓰기!
  deca_ull.h                 ← ★ ZIP의 platform/deca_ull.h 복사
  deca_device_api.h          ← (placeholder 덮어쓰기)
  deca_interface.c
  deca_interface.h
  deca_private.h
  deca_rsl.c
  deca_rsl.h
  deca_types.h
  deca_version.h
  dw3000/
    dw3000_device.c          ← (placeholder 덮어쓰기) 칩 드라이버(dw3000_driver)
    dw3000_deca_regs.h
    dw3000_deca_vals.h
  lib/qmath/
    include/qmath.h
    src/qmath.c
```
**의존성 검증 완료** — 위 파일들이 전부이며, repo `platform/` 의
`deca_ull.h`·`deca_probe_interface.h`·`dw3000.h` 는 **불필요**합니다
(probe 구조체는 우리 `deca_port.c` 가 직접 정의함).

가장 쉬운 방법: ZIP의 **`dwt_uwb_driver` 폴더를 통째로** `Drivers/` 에 복사(placeholder 덮어쓰기).
그 안의 `utest/`, `lib/qmath/utest`, `CMakeLists.txt`, `LICENSES/` 등은 있어도 빌드에서
자동 제외되거나 무해함. 꼭 컴파일돼야 하는 `.c` 는: `deca_compat.c`, `deca_interface.c`,
`deca_rsl.c`, `dw3000/dw3000_device.c`, `lib/qmath/src/qmath.c`.

### 2-2. 우리 플랫폼 파일 (이미 `Core/Src/dwport/` 에 있음)
- `dw3000_spi.c/.h` — SPI (SPI1, Mode3, CS=PA2, 풀듀플렉스 읽기)
- `dw3000_hw.c/.h` — 리셋(PA4)/IRQ(PA15)
- `deca_port.c` — mutex/sleep + **probe 인터페이스 등록**(`dw3000_probe_interf`)
- `config.h`(핀), `log.h`(로그). (`deca_spi.c/.h` 빈 stub — 무방)

### 2-3. ⚠ Include paths — 하위폴더까지 모두 추가 (핵심 함정!)
헤더가 하위폴더에 흩어져 있어, 아래 **네 경로 전부** 추가해야 합니다.
(Project ▸ Properties ▸ C/C++ Build ▸ Settings ▸ MCU GCC Compiler ▸ Include paths)
```
../Drivers/dwt_uwb_driver
../Drivers/dwt_uwb_driver/dw3000
../Drivers/dwt_uwb_driver/lib/qmath/include
../Core/Src/dwport
```

### 2-4. 빌드 대상 .c 확인
다음 `.c` 들이 컴파일되는지 확인(Source Location / Exclude from build 해제):
`deca_compat.c`, `deca_interface.c`, `deca_rsl.c`, `dw3000/dw3000_device.c`,
`lib/qmath/src/qmath.c`, 그리고 `Core/Src/dwport/*.c`.

복사 후 `Project ▸ Refresh(F5)` → 빌드.

---

## 3. 초기화 호출 순서 (probe 인터페이스)

`deca_port.c` 가 `dw3000_probe_interf` 구조체를 정의해 두었습니다. 앱에서:

```c
#include "deca_device_api.h"
#include "dw3000_hw.h"

extern const struct dwt_probe_s dw3000_probe_interf;

int dw3000_app_init(void)
{
    dw3000_hw_init();          /* GPIO/SPI 초기화, CS High, 저속 SPI */
    dw3000_hw_reset();         /* PA4 하드리셋 */
    deca_sleep(2);

    /* ★ dwt_probe() 를 가장 먼저! SPI 함수포인터/칩 구조체 등록.
       이걸 빼고 dwt_checkidlerc() 등 호출하면 NULL 역참조로 HardFault. */
    if (dwt_probe((struct dwt_probe_s*)&dw3000_probe_interf) != DWT_SUCCESS)
        return -1;

    /* IDLE_RC 진입 대기 — 미니드라이버로는 못 넘던 부분.
       정식 드라이버는 부팅 시퀀스를 수행하므로 여기서 RCINIT 이 뜬다. */
    while (!dwt_checkidlerc()) { };

    /* DEV_ID 확인(선택) */
    if (dwt_readdevid() != (uint32_t)DWT_DW3000_DEV_ID) { /* 0xDECA0302 */ }

    if (dwt_initialise(DWT_DW_INIT) != DWT_SUCCESS) return -2;   /* OTP 로드 등 */
    if (dwt_configure(&dw_cfg)       != DWT_SUCCESS) return -3;  /* RF/PLL lock */

    dw3000_spi_speed_fast();   /* 이후 고속 SPI */
    return 0;
}
```

`dw_cfg` 예시 (채널5 / 64MHz PRF / preamble code 9, 송·수신측 동일하게):
```c
static dwt_config_t dw_cfg = {
    5, DWT_PLEN_128, DWT_PAC8, 9, 9, 1,
    DWT_BR_6M8, DWT_PHRMODE_STD, DWT_PHRRATE_STD,
    (129 + 8 - 8), DWT_STS_MODE_OFF, DWT_STS_LEN_64, DWT_PDOA_M0
};
```

> 구조체 필드/상수 이름은 받은 드라이버의 `deca_device_api.h` 정의에 맞춰 미세조정될 수 있음.

전체 예제는 `Core/Src/dwport/dw3000_app_example.c` 참고.

---

## 4. 빌드 시 흔한 이슈

- `deca_device_api.h: No such file` → 2-3번(드라이버 폴더 추가 + include path) 누락.
- `undefined reference to dw3000_driver` → `dwt_uwb_driver/` 의 칩 드라이버 `.c`(dw3000_device 등)가 빌드에 빠짐 → Source location 확인.
- `LOG_INF/printf` 링크 에러 → `log.h` 의 `DW3000_LOG_ENABLE` 을 0 으로 (또는 syscalls printf 리타게팅 유지).
- 함수 포인터 타입 불일치 경고 → 받은 버전의 `dwt_spi_s` 멤버 시그니처와 `dw3000_spi_*` 원형을 맞추면 됨(반환형 `int32_t`).

---

## 5. 동작 확인 순서

1. 빌드/다운로드 → `dwt_checkidlerc()` 통과, `dwt_initialise/dwt_configure` 성공 로그 확인.
2. 한쪽 `dwt_starttx`, 다른 쪽 `dwt_rxenable` 로 프레임 송수신.
3. 되면 SS-TWR/DS-TWR 레인징 예제로 확장 (libdeca 라이브러리 활용 가능).

---

## 6. 하드웨어 주의 (검증됨)

- SPI는 **Mode 3** 필수.
- init 구간 저속(≤~2MHz) → `dwt_configure` 후 `dw3000_spi_speed_fast()`.
- 점퍼선 신호품질(이전 SCK 5.2V 오버슈트): SCK 직렬 22~33Ω, 짧은 배선, GND 보강 → 고속 안정.
- CS(PA2) idle High 유지 (CubeMX Output Level=High 권장).
