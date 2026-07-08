/**
 * @file    dw3000_regs.h
 * @brief   Qorvo DW3000 (DWM3000) register map - 최소 정의
 *
 * DW3000 의 레지스터는 "파일ID(상위) + 오프셋(하위)" 으로 구성됩니다.
 * 본 드라이버에서는 32비트 ID 로 표현하며 상위가 파일ID, 하위가 오프셋입니다.
 *   reg_file   = (ID >> 16) & 0x1F
 *   reg_offset = (ID & 0x7F)
 *
 * 값들은 DW3000 Family User Manual / Qorvo DW3000 API(deca_regs.h) 기준입니다.
 */
#ifndef DW3000_REGS_H
#define DW3000_REGS_H

/* ------------------------------------------------------------------ */
/* File 0x00 : GEN_CFG_AES_0                                          */
/* ------------------------------------------------------------------ */
#define DW3000_DEV_ID          0x000000UL  /* RO  : 0xDECA0302 (DW3000) */
#define DW3000_EUI_64          0x000004UL
#define DW3000_PANADR          0x00000CUL
#define DW3000_SYS_CFG         0x000010UL
#define DW3000_FF_CFG          0x000014UL
#define DW3000_SYS_TIME        0x00001CUL
#define DW3000_TX_FCTRL        0x000024UL  /* TX frame control (lo 32b) */
#define DW3000_TX_FCTRL_HI     0x000028UL
#define DW3000_DX_TIME         0x00002CUL  /* delayed send/receive time */
#define DW3000_RX_FWTO         0x000034UL  /* RX frame wait timeout */
#define DW3000_SYS_CTRL        0x000038UL
#define DW3000_SYS_ENABLE      0x00003CUL  /* interrupt mask (SYS_MASK) */
#define DW3000_SYS_STATUS      0x000044UL  /* system event status (32b) */
#define DW3000_SYS_STATUS_HI   0x000048UL  /* system event status (hi)  */
#define DW3000_RX_FINFO        0x00004CUL  /* RX frame info             */
#define DW3000_RX_TIME         0x000064UL  /* RX timestamp              */
#define DW3000_TX_TIME         0x000074UL  /* TX timestamp              */

/* SYS_CFG bits */
#define DW3000_SYS_CFG_DIS_FCE    (1UL << 18) /* disable auto frame-check error */
#define DW3000_SYS_CFG_PHR_MODE   (1UL << 3)

/* SYS_STATUS bits (32-bit, reg 0x44) */
/* ⚠ DW3000 정확한 비트 위치 (이전 정의는 DW1000 기준이라 틀렸었음) */
#define DW3000_SYS_STATUS_IRQS    (1UL << 0)   /* interrupt request    */
#define DW3000_SYS_STATUS_CPLOCK  (1UL << 1)   /* clock PLL lock       */
#define DW3000_SYS_STATUS_AAT     (1UL << 3)   /* auto-ack pending     */
#define DW3000_SYS_STATUS_TXFRB   (1UL << 4)   /* TX frame begins      */
#define DW3000_SYS_STATUS_TXFRS   (1UL << 7)   /* TX frame sent        */
#define DW3000_SYS_STATUS_RXPHE   (1UL << 12)  /* RX PHY header error  */
#define DW3000_SYS_STATUS_RXFR    (1UL << 13)  /* RX frame ready       */
#define DW3000_SYS_STATUS_RXFCG   (1UL << 14)  /* RX FCS good          */
#define DW3000_SYS_STATUS_RXFCE   (1UL << 15)  /* RX FCS error         */
#define DW3000_SYS_STATUS_RXFSL   (1UL << 16)  /* RX sync loss         */
#define DW3000_SYS_STATUS_RXFTO   (1UL << 17)  /* RX frame wait timeout*/
#define DW3000_SYS_STATUS_SPIRDY  (1UL << 23)  /* SPI ready            */
#define DW3000_SYS_STATUS_RCINIT  (1UL << 24)  /* device init (IDLE_RC)*/
#define DW3000_SYS_STATUS_RXSTO   (1UL << 26)  /* RX SFD timeout       */
#define DW3000_SYS_STATUS_HPDWARN (1UL << 27)  /* half period delay warn*/
#define DW3000_SYS_STATUS_ARFE    (1UL << 29)  /* auto frame filter rej*/

/* 수신 에러 통합 마스크 (오류 발생 시 RX 재시작 판단용) */
#define DW3000_SYS_STATUS_ALL_RX_ERR  ( DW3000_SYS_STATUS_RXPHE | \
                                        DW3000_SYS_STATUS_RXFCE | \
                                        DW3000_SYS_STATUS_RXFSL | \
                                        DW3000_SYS_STATUS_RXSTO | \
                                        DW3000_SYS_STATUS_ARFE )
#define DW3000_SYS_STATUS_ALL_RX_TO   ( DW3000_SYS_STATUS_RXFTO )
#define DW3000_SYS_STATUS_ALL_RX_GOOD ( DW3000_SYS_STATUS_RXFR | \
                                        DW3000_SYS_STATUS_RXFCG )

/* RX_FINFO : 하위 10비트가 수신 프레임 길이 (FCS 2바이트 포함) */
#define DW3000_RX_FINFO_RXFLEN_MASK   0x000003FFUL

/* ------------------------------------------------------------------ */
/* File 0x01 : GEN_CFG_AES_1                                          */
/* ------------------------------------------------------------------ */
#define DW3000_CHAN_CTRL       0x010014UL  /* 채널/프리앰블코드 설정 */

/* CHAN_CTRL bits */
#define DW3000_CHAN_CTRL_RF_CHAN        (1UL << 0)  /* 0=ch5, 1=ch9 */
#define DW3000_CHAN_CTRL_SFD_TYPE_SHIFT 1
#define DW3000_CHAN_CTRL_TX_PCODE_SHIFT 3
#define DW3000_CHAN_CTRL_RX_PCODE_SHIFT 8

/* ------------------------------------------------------------------ */
/* File 0x06 : DRX  (digital RX)                                      */
/* ------------------------------------------------------------------ */
#define DW3000_DTUNE0          0x060000UL  /* PAC size */
#define DW3000_DTUNE3          0x06000CUL  /* SFD timeout */

/* ------------------------------------------------------------------ */
/* File 0x09 : FS_CTRL  (PLL)                                        */
/* ------------------------------------------------------------------ */
#define DW3000_PLL_CFG         0x090000UL
#define DW3000_PLL_CAL         0x090008UL

/* ------------------------------------------------------------------ */
/* File 0x0A : AON                                                   */
/* ------------------------------------------------------------------ */
#define DW3000_AON_DIG_CFG     0x0A0000UL
#define DW3000_AON_CTRL        0x0A0004UL

/* ------------------------------------------------------------------ */
/* File 0x11 : PMSC                                                  */
/* ------------------------------------------------------------------ */
#define DW3000_CLK_CTRL        0x110004UL  /* 클럭 제어 */
#define DW3000_SEQ_CTRL        0x110008UL
#define DW3000_SOFT_RST        0x110010UL  /* 소프트 리셋 */

/* ------------------------------------------------------------------ */
/* 데이터 버퍼 (파일 단위)                                          */
/* ------------------------------------------------------------------ */
#define DW3000_TX_BUFFER       0x140000UL  /* TX 데이터 버퍼 */
#define DW3000_RX_BUFFER_0     0x120000UL  /* RX 데이터 버퍼 0 */

/* ------------------------------------------------------------------ */
/* Fast command (1바이트 헤더) 코드                                  */
/* ------------------------------------------------------------------ */
#define DW3000_CMD_TXRXOFF     0x00  /* TX/RX off + IDLE  */
#define DW3000_CMD_TX          0x01  /* 즉시 송신         */
#define DW3000_CMD_RX          0x02  /* 즉시 수신 enable  */
#define DW3000_CMD_DTX         0x03  /* 지연 송신         */
#define DW3000_CMD_DRX         0x04  /* 지연 수신         */
#define DW3000_CMD_CLR_IRQS    0x0D  /* 인터럽트 클리어   */

/* 예상 device id */
#define DW3000_DEVICE_ID_EXPECTED  0xDECA0302UL

#endif /* DW3000_REGS_H */
