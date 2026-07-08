/**
 * @file    uwb_ble.c
 * @brief   UWB 거리 → BLE GATT (BlueNRG-MS / X-CUBE-BLE1)
 *
 *  ※ X-CUBE-BLE1(BlueNRG-MS) 미들웨어가 추가돼 있어야 빌드됨 (UWB_ENABLE_BLE=1).
 */
#include "uwb_config.h"

/* BLE 미사용 빌드에서는 이 파일 전체를 비워 BlueNRG 헤더 의존성을 없앤다. */
#if UWB_ENABLE_BLE

#include "uwb_ble.h"
#include <string.h>
#include <stdio.h>

/* --- BlueNRG-MS 미들웨어 헤더 --- */
#include "hci.h"
#include "hci_tl.h"
#include "hci_const.h"
#include "bluenrg_aci.h"
#include "bluenrg_hal_aci.h"
#include "bluenrg_gap.h"
#include "bluenrg_gap_aci.h"
#include "bluenrg_gatt_aci.h"
#include "bluenrg_utils.h"
#include "sm.h"

/* ------------------------------------------------------------------ *
 *  128-bit UUID (BlueNRG = 16바이트 LSB-first)
 *  Service : 12345678-1234-5678-1234-56789abcdef0
 *  Char    : 12345678-1234-5678-1234-56789abcdef1
 * ------------------------------------------------------------------ */
static const uint8_t uwb_service_uuid[16] = {
    0xf0,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12
};
static const uint8_t uwb_dist_char_uuid[16] = {
    0xf1,0xde,0xbc,0x9a,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12,0x78,0x56,0x34,0x12
};

static const char local_name_str[] = "UWB-RANGE";   /* 9 chars */

static uint16_t g_service_handle   = 0;
static uint16_t g_dist_char_handle = 0;
static volatile uint8_t  g_connected = 0;
static uint16_t g_conn_handle = 0;

/* ------------------------------------------------------------------ */
static void start_advertising(void)
{
    uint8_t local_name[1 + 9];
    local_name[0] = AD_TYPE_COMPLETE_LOCAL_NAME;
    memcpy(&local_name[1], local_name_str, 9);
    aci_gap_set_discoverable(ADV_IND, 0x0020, 0x0040, PUBLIC_ADDR, NO_WHITE_LIST_USE,
                             sizeof(local_name), (const char *)local_name, 0, NULL, 0, 0);
}

/* BlueNRG-MS 이벤트 콜백 (hci_user_evt_proc 안에서 호출) */
static void uwb_user_notify(void *pData)
{
    hci_uart_pckt  *hci_pckt   = (hci_uart_pckt *)pData;
    hci_event_pckt *event_pckt = (hci_event_pckt *)hci_pckt->data;

    if (hci_pckt->type != HCI_EVENT_PKT) return;

    switch (event_pckt->evt) {
    case EVT_DISCONN_COMPLETE:
        g_connected = 0;
        printf("BLE disconnected — re-advertising\r\n");
        start_advertising();
        break;

    case EVT_LE_META_EVENT: {
        evt_le_meta_event *evt = (evt_le_meta_event *)event_pckt->data;
        if (evt->subevent == EVT_LE_CONN_COMPLETE) {
            evt_le_connection_complete *cc = (evt_le_connection_complete *)evt->data;
            g_connected   = 1;
            g_conn_handle = cc->handle;
            printf("BLE connected\r\n");
        }
    } break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
void uwb_ble_init(void)
{
    uint8_t  bdaddr[6] = { 0x01, 0xB0, 0x00, 0xE1, 0x80, 0x02 };  /* 임의 고정 public 주소 */
    uint16_t service_handle, dev_name_char_handle, appearance_char_handle;
    uint8_t  ret;

    /* HCI 스택 + 전송계층 초기화 + BlueNRG 하드리셋 (app_bluenrg_ms.c 없으므로 직접) */
    hci_init(uwb_user_notify, NULL);
    HAL_Delay(200);

    /* public 주소 */
    aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET, CONFIG_DATA_PUBADDR_LEN, bdaddr);

    /* GATT/GAP */
    aci_gatt_init();
    aci_gap_init_IDB05A1(GAP_PERIPHERAL_ROLE_IDB05A1, 0, strlen(local_name_str),
                         &service_handle, &dev_name_char_handle, &appearance_char_handle);
    aci_gatt_update_char_value(service_handle, dev_name_char_handle, 0,
                               strlen(local_name_str), (uint8_t *)local_name_str);
    aci_hal_set_tx_power_level(1, 4);

    /* UWB 서비스 + 거리 characteristic */
    ret = aci_gatt_add_serv(UUID_TYPE_128, uwb_service_uuid, PRIMARY_SERVICE,
                            7, &g_service_handle);
    if (ret != BLE_STATUS_SUCCESS) { printf("BLE add_serv FAIL 0x%02X\r\n", ret); return; }

    ret = aci_gatt_add_char(g_service_handle, UUID_TYPE_128, uwb_dist_char_uuid, 4,
                            CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE,
                            GATT_NOTIFY_ATTRIBUTE_WRITE, 16, 0, &g_dist_char_handle);
    if (ret != BLE_STATUS_SUCCESS) { printf("BLE add_char FAIL 0x%02X\r\n", ret); return; }

    start_advertising();
    printf("BLE ready: advertising as '%s'\r\n", local_name_str);
}

/* ------------------------------------------------------------------ */
void uwb_ble_process(void)
{
    /* BlueNRG IRQ(PE6) 인터럽트가 큐를 채우고, 여기서 이벤트를 디스패치 */
    hci_user_evt_proc();
}

bool uwb_ble_is_connected(void) { return g_connected != 0; }

void uwb_ble_update_distance(float distance_m)
{
    if (!g_connected) return;
    /* float32 (LE) 4바이트 그대로 notify */
    aci_gatt_update_char_value(g_service_handle, g_dist_char_handle, 0,
                               4, (uint8_t *)&distance_m);
}

#endif /* UWB_ENABLE_BLE */
