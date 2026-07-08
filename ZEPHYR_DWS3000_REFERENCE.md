# Zephyr v3.6 로 DWS3000 레퍼런스 빌드 (B-L4S5I-IOT01A 보드)

목적: **검증된 DW3000 드라이버**를 지금 쓰는 **B-L4S5I-IOT01A + DWS3000 실드**에 그대로 올려서,
"하드웨어/칩은 정상인데 우리 베어메탈 포팅이 문제"인지 "하드웨어 문제"인지 확정한다.
Zephyr 는 `b_l4s5i_iot01a` 보드와 `qorvo_dws3000` 실드를 모두 기본 지원하므로 추가 보드가 필요 없다.

> 권장: **Linux 또는 Windows의 WSL2(Ubuntu)** 에서 진행 (Windows 네이티브보다 훨씬 안정적).
> 아래는 Ubuntu(WSL2 포함) 기준. Windows 네이티브는 7번 참고.

---

## 1. 의존 패키지 설치 (Ubuntu / WSL2)

```bash
sudo apt update
sudo apt install --no-install-recommends -y git cmake ninja-build gperf \
  ccache dfu-util device-tree-compiler wget python3-dev python3-pip \
  python3-venv python3-setuptools python3-tk python3-wheel xz-utils file \
  make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1
```

CMake ≥ 3.20.5 필요 (Ubuntu 22.04는 OK).

## 2. Python 가상환경 + west

```bash
python3 -m venv ~/zephyrproject/.venv
source ~/zephyrproject/.venv/bin/activate
pip install west
```

## 3. Zephyr v3.6.0 가져오기

```bash
west init --mr v3.6.0 ~/zephyrproject
cd ~/zephyrproject
west update            # 수 분 소요 (모듈 git clone)
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
```

## 4. Zephyr SDK 0.16.x 설치

```bash
cd ~
wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/zephyr-sdk-0.16.8_linux-x86_64.tar.xz
tar xf zephyr-sdk-0.16.8_linux-x86_64.tar.xz
cd zephyr-sdk-0.16.8 && ./setup.sh -t arm-zephyr-eabi -h -c
```

## 5. DWS3000 드라이버 + 예제 추가

br101 의 멀티플랫폼 드라이버와 예제를 Zephyr 모듈로 추가한다.

```bash
cd ~/zephyrproject
git clone https://github.com/br101/dw3000-decadriver-source.git modules/dw3000
git clone https://github.com/br101/zephyr-dw3000-examples.git dw3000-examples
```

`zephyr-dw3000-examples` 의 README 에 모듈 등록/빌드 방법이 있다. 핵심은
`west` 가 `modules/dw3000/platform` 을 ZEPHYR_EXTRA_MODULES 로 인식하게 하는 것.
(예제 repo 의 `west.yml` 또는 CMake 설정을 따른다.)

## 6. B-L4S5I-IOT01A + DWS3000 실드로 빌드 & 플래시

```bash
cd ~/zephyrproject
source .venv/bin/activate

# 예: 디바이스 ID 읽기/기본 예제 (예제 repo의 앱 경로에 맞게 수정)
west build -p always -b b_l4s5i_iot01a --shield qorvo_dws3000 dw3000-examples/<app_dir>

# ST-LINK 로 플래시 (보드가 PC에 USB 연결된 상태)
west flash
```

> `--shield qorvo_dws3000` 가 DWS3000 의 SPI/RST/IRQ 핀을 아두이노 헤더에 매핑해 준다.
> 우리가 베어메탈에서 쓴 핀(SPI1=PA5/6/7, CS=PA2, RST=PA4, IRQ=PA15)과 동일한 아두이노 D10/D11/D12/D13/D7/D9 라인이다.

플래시가 안 되면(west flash 실패), 빌드 산출물 `build/zephyr/zephyr.hex` 를
**STM32CubeProgrammer**로 직접 구워도 된다 (ST-LINK).

## 7. (대안) Windows 네이티브

Windows 면 chocolatey 로 `cmake ninja gperf python git dtc-msys2 wget 7zip` 설치 후
2~6번을 PowerShell 에서 동일하게 진행. (자세히는 Zephyr Getting Started Guide v3.6 참고:
https://docs.zephyrproject.org/3.6.0/develop/getting_started/index.html )

---

## 8. 결과 해석 (가장 중요)

레퍼런스 예제가 **DEV_ID 0xDECA0302 읽고, dwt_initialise/dwt_configure 성공, 레인징/TX/RX 동작**하면:
- → **실드·칩·B-L4S5I-IOT01A 보드 모두 정상.** 문제는 우리 베어메탈 포팅의 칩 브링업 시퀀스.
- → 그 Zephyr 예제의 **리셋·클럭·init 순서**(특히 dw3000_hw.c / deca_port.c / SPI 설정)를
  우리 `Core/Src/dwport/` 와 비교하면 차이가 드러난다. 그 init 코드를 공유해 주시면 맞춰 드린다.

레퍼런스도 **PLL/IDLE_RC 에서 똑같이 실패**하면:
- → 펌웨어가 아니라 **하드웨어/칩 문제** 확정 (전원/실드/모듈).

---

## 참고 링크
- Zephyr Getting Started v3.6: https://docs.zephyrproject.org/3.6.0/develop/getting_started/index.html
- DW3000 멀티플랫폼 드라이버: https://github.com/br101/dw3000-decadriver-source
- DW3000 Zephyr 예제: https://github.com/br101/zephyr-dw3000-examples
- (참고) DWS3000 실드 정의: dw3000-decadriver-source/platform/zephyr/boards/shields/qorvo_dws3000
