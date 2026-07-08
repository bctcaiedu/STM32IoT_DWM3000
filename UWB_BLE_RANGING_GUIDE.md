# UWB 거리측정 → BLE → 폰 앱 통합 가이드

목표: **태그 보드**가 **앵커 보드**와 SS-TWR로 거리를 재고, 그 거리를 **BLE(GATT)**로
스마트폰 앱에 실시간 전송한다.

```
[앵커 보드 B]  ◀──UWB(SS-TWR)──▶  [태그 보드 A] ──BLE(GATT)──▶ [폰: Web Bluetooth 앱]
 RESPONDER                          INITIATOR + BlueNRG-MS
```

준비물: b_l4s5i_iot01a 보드 **2개** + DWS3000 실드 2개 (둘 다 앞서 PLL까지 정상 확인된 것).

---

## 추가된 파일

| 파일 | 역할 |
|---|---|
| `Core/Src/dwport/dw3000_twr.c/.h` | SS-TWR 거리측정 (initiator/responder) |
| `Core/Src/dwport/uwb_ble.c/.h` | BLE UWB 서비스 + 거리 characteristic (BlueNRG-MS) |
| `Core/Src/dwport/uwb_app.c/.h` | 역할 기반 실행 루프 (+ BLE notify) |
| `phone_app/uwb_ranging.html` | 폰 앱 (Web Bluetooth) |

`main.c` 는 `dw3000_app_init()` 성공 후 `uwb_app_run()` 을 호출하도록 수정됨.

> CubeIDE 프로젝트에 새 .c 가 자동 포함 안 되면, 기존 dwport 파일들과 같은 소스 폴더라
> 보통 그대로 빌드된다. 안 되면 프로젝트 우클릭 → Refresh (F5).

---

## 1단계: BLE 없이 레인징부터 검증 (권장)

먼저 거리측정 자체가 되는지 UART로 확인한다. BLE는 그 다음.

역할/BLE 설정은 **`Core/Src/dwport/uwb_config.h`** 한 파일에서만 바꾼다.

### 앵커 보드 B (RESPONDER)
`uwb_config.h`:
```c
#define UWB_ROLE        UWB_ROLE_RESPONDER
#define UWB_ENABLE_BLE  0
```
빌드 → 보드 B 플래시. UART에 `UWB RESPONDER (anchor) 시작`.

### 태그 보드 A (INITIATOR)
`uwb_config.h`:
```c
#define UWB_ROLE        UWB_ROLE_INITIATOR
#define UWB_ENABLE_BLE  0
```
빌드 → 보드 A 플래시. UART(115200)에:
```
UWB INITIATOR (tag) 시작
distance = 73 cm
distance = 74 cm
...
```
두 보드를 떨어뜨리며 값이 따라 변하면 SS-TWR 성공.

### 거리 보정 (선택)
표시값이 실제보다 일정하게 치우치면 **안테나 지연** 때문이다.
`dw3000_twr.c` 의 `TX_ANT_DLY`/`RX_ANT_DLY`(기본 16385)를 조정한다.
지연 1 단위 ≈ 약 **4.7 mm**. 예: 측정이 실제보다 30 cm 크면 두 지연을 각각 ~32씩 올린다.
(정밀 측정 아니면 기본값으로 충분.)

---

## 2단계: BLE 스택 추가 (CubeMX X-CUBE-BLE1)

보드의 BLE 모듈은 **SPBTLE-RF (BlueNRG-MS)**, 이미 핀이 잡혀 있다:

| 신호 | 핀 | main.h 매크로 |
|---|---|---|
| SPI3 SCK / MISO / MOSI | PC10 / PC11 / PC12 | `INTERNAL_SPI3_*` |
| CS (NSS) | **PD13** | `SPBTLE_RF_SPI3_CSN` |
| IRQ (EXTI9_5) | **PE6** | `SPBTLE_RF_IRQ_EXTI6` |
| RESET | **PA8** | `SPBTLE_RF_RST` |

### 2-1. 소프트웨어 팩 활성화
1. CubeIDE에서 `STM32IoT.ioc` 더블클릭 → CubeMX 뷰.
2. 상단 **Software Packs → Select Components**.
3. **STMicroelectronics.X-CUBE-BLE1** (BlueNRG-MS) 체크 →
   `BLE_Manager`/`Controller` 가 아니라 **BlueNRG-MS** 컴포넌트 선택, **Ok**.
4. 좌측 트리에 생긴 **X-CUBE-BLE1** 클릭 → 활성화(Activate).
   - SPI instance: **SPI3**
   - CS GPIO: **PD13**, EXTI(IRQ): **PE6**, Reset: **PA8**
   (대부분 보드 BSP 기본값으로 자동 매칭됨.)
5. **Project Manager → Code Generator**: "Copy only necessary library files" 권장.
6. **GENERATE CODE**.

> 생성 후 `Drivers/BSP`, `Middlewares/.../BlueNRG-MS`, `Core/Src/app_bluenrg_ms.c`,
> `hci_tl_interface.c` 등이 추가된다. `main()` 에 `MX_BlueNRG_MS_Init()` 호출도 생긴다.

### 2-2. 먼저 샘플로 BLE 동작 확인
생성된 샘플(`app_bluenrg_ms.c`)을 그대로 한 번 빌드/플래시해서,
폰의 **nRF Connect** 로 보드 광고가 보이는지 확인한다(스택/전송계층 정상 확인).
※ 이때 우리 `uwb_app_run()` 의 무한루프가 `MX_BlueNRG_MS_Process()` 를 막지 않도록,
   2-3에서 통합한다.

### 2-3. UWB 서비스로 교체
`app_bluenrg_ms.c` 에서 **샘플 서비스 초기화/업데이트 호출을 제거**하고 우리 것을 쓴다.

- `MX_BlueNRG_MS_Init()` 안: `hci_init(...)` 와 BlueNRG 하드웨어 reset 까지는 **유지**,
  그 뒤의 샘플 `Add_Sample_Service()`/`aci_gap_init` 등 **사용자 코드 블록은 주석 처리**.
  (GATT/GAP/서비스 등록은 우리 `uwb_ble_init()` 가 담당.)
- `MX_BlueNRG_MS_Process()` 안의 `hci_user_evt_proc()` 호출은 **유지**해도 되고,
  우리 `uwb_ble_process()` 가 같은 일을 하므로 둘 중 하나만 있으면 된다.

그다음 `uwb_config.h` 에서 BLE 켜기:
```c
#define UWB_ROLE        UWB_ROLE_INITIATOR
#define UWB_ENABLE_BLE  1          // ← 켬 (X-CUBE-BLE1 추가 후에만!)
```

호출 순서 보장: `main()` 에서 **`MX_BlueNRG_MS_Init()` 가 `uwb_app_run()` 보다 먼저** 실행돼야 한다
(HCI 전송계층이 먼저 떠야 `uwb_ble_init()` 의 GATT 호출이 동작).
보통 생성 코드가 `/* USER CODE BEGIN 2 */` 위쪽에서 `MX_BlueNRG_MS_Init()` 를 부르므로,
우리 `dw3000_app_init()`/`uwb_app_run()` 호출(USER CODE END 2 직전)이 자연히 뒤에 온다.

### 2-4. 헤더 이름 확인
`uwb_ble.c` 상단 include 는 BlueNRG-MS 표준 헤더 기준이다. 생성된 미들웨어 버전에 따라
`bluenrg_aci.h` / `bluenrg_gatt_aci.h` 등 경로·이름이 다르면 빨간 줄이 뜬다 →
생성된 폴더의 실제 헤더명으로 맞춰주면 된다(함수명 `aci_gatt_add_serv` 등은 동일).

빌드 → 태그 보드 A 재플래시. UART에 `BLE ready: advertising as 'UWB-RANGE'` 가 뜬다.

---

## 3단계: 폰 앱으로 보기

`phone_app/uwb_ranging.html` 은 **Web Bluetooth** 앱이다 (앱스토어 불필요).

- **Android**: Chrome 으로 열면 됨. 단 Web Bluetooth 는 **HTTPS 또는 localhost** 에서만 동작:
  - 가장 쉬움: 이 파일을 GitHub Pages / Netlify 등 HTTPS 에 올리거나,
  - PC에서 `python -m http.server` 후 폰과 같은 망에서 접속(단 http는 일부 제한) →
    확실하게는 HTTPS 권장.
- **iOS**: Safari는 Web Bluetooth 미지원 → **Bluefy** 브라우저 앱에서 이 HTML 열기.

사용:
1. 보드 A 전원 ON (광고 중), 보드 B(앵커)도 ON.
2. 앱에서 **📡 보드에 연결** → 목록에서 **UWB-RANGE** 선택.
3. 게이지/숫자에 거리(m)가 실시간 표시, 하단에 그래프·Min/Max·Hz.

게이지 풀스케일은 HTML 상단 `GAUGE_MAX_M`(기본 5 m)로 조정.

---

## GATT 명세 (커스텀 앱 만들 때 참고)

| 항목 | UUID | 비고 |
|---|---|---|
| UWB Service | `12345678-1234-5678-1234-56789abcdef0` | Primary |
| Distance Char | `12345678-1234-5678-1234-56789abcdef1` | Read + Notify, **float32 LE (미터)** |

광고 이름: `UWB-RANGE`. 값은 4바이트 little-endian float(거리, m).

---

## 트러블슈팅

- **앵커 응답 없음 (`no response`)**: 두 보드의 `dwt_config_t`(채널/프리앰블/코드)가 동일한지 확인.
  거리가 너무 멀거나(>실내 수십 m) 안테나 가림도 원인.
- **거리 음수/튐**: 안테나 지연 보정(1단계) 또는 다중경로. `GAUGE_MAX_M`와 무관.
- **BLE 광고 안 뜸**: `MX_BlueNRG_MS_Init()` 호출 여부, BlueNRG `RST`(PA8) 펄스, SPI3 동작 확인.
  먼저 2-2 샘플로 스택 단독 검증.
- **폰에서 서비스 안 보임**: 앱은 이름(UWB)으로 필터 후 서비스 UUID로 접근한다.
  UUID 가 펌웨어(`uwb_ble.c`)와 HTML(`SERVICE_UUID`)에서 동일한지 확인.
- **거리 표시 `--` 고정**: characteristic notify 구독 실패 → 보드가 연결 후 `uwb_ble_update_distance()`
  를 호출하는지(=레인징 성공 중인지) UART로 확인.
- **float printf**: 펌웨어는 cm 정수로 출력하므로 newlib float-printf 옵션 불필요.
```
