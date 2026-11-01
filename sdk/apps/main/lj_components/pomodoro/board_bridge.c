#include "board_bridge.h"
#include "qm_config.h"
#if CONFIG_QM_CLI_SUPPORT
#include "qm_cli.h"
#endif
#if __has_include("board_bridge_local.h")
#include "board_bridge_local.h"
#include "pomodoro.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string.h>
#define TAG "board_bridge"
#define MAGIC 0x4d434231u
static const uint8_t voice_mac[6] = BOARD_BRIDGE_VOICE_MAC;
static const uint8_t action_mac[6] = BOARD_BRIDGE_ACTION_MAC;
static const uint8_t pmk[16] = BOARD_BRIDGE_PMK;
static const uint8_t lmk[16] = BOARD_BRIDGE_LMK;
static const char *commands[] = {"start", "pause", "resume", "stop"};
typedef struct {
    uint32_t magic, session, seq;
    uint8_t version, type, command, reserved; /* 1=command, 2=execution ACK */
    int32_t result;
} packet_t;
_Static_assert(sizeof(packet_t) == 20, "Unexpected packet layout");
typedef struct { bool local; packet_t packet; } event_t;
static QueueHandle_t queue;
static bool ready, voice;
static uint32_t last_tx_seq, last_ack_seq;
static int last_ack_result;
static uint8_t peer_mac[6];
static void receive(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!ready || !info || !data || len != sizeof(packet_t) ||
        memcmp(info->src_addr, peer_mac, 6) != 0 ||
        memcmp(info->des_addr, voice ? voice_mac : action_mac, 6) != 0) return;
    event_t e = {0}; memcpy(&e.packet, data, sizeof(e.packet));
    if (e.packet.magic != MAGIC || e.packet.version != 1 || e.packet.command >= 4 ||
        (voice ? e.packet.type != 2 : e.packet.type != 1)) return;
    if (xQueueSend(queue, &e, 0) != pdTRUE) ESP_EARLY_LOGW(TAG, "receive queue full");
}
static void worker(void *unused)
{
    (void)unused;
    uint32_t session = esp_random(), seq = 0, last_session = 0, last_seq = 0;
    bool pending = false, seen = false;
    packet_t tx = {0}, last_ack = {0};
    unsigned attempts = 0;
    int64_t deadline = 0;
    for (;;) {
        event_t e;
        if (xQueueReceive(queue, &e, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (e.local && voice) {
                /* Do not reorder a second operation ahead of an unacknowledged one. */
                if (pending) ESP_LOGW(TAG, "new intent supersedes unconfirmed command");
                tx = e.packet; tx.magic = MAGIC; tx.version = 1; tx.type = 1;
                tx.session = session; tx.seq = ++seq;
                __atomic_store_n(&last_tx_seq, tx.seq, __ATOMIC_RELAXED);
                ESP_LOGI(TAG, "forward %s queued seq=%lu", commands[tx.command], (unsigned long)tx.seq);
                pending = true; attempts = 0; deadline = 0;
            } else if (!e.local && voice && pending && e.packet.session == tx.session &&
                       e.packet.seq == tx.seq && e.packet.command == tx.command) {
                ESP_LOGI(TAG, "remote %s ACK result=%d seq=%lu", commands[tx.command],
                         (int)e.packet.result, (unsigned long)tx.seq);
                __atomic_store_n(&last_ack_seq, tx.seq, __ATOMIC_RELAXED);
                __atomic_store_n(&last_ack_result, e.packet.result, __ATOMIC_RELAXED);
                pending = false;
            } else if (!e.local && !voice) {
                packet_t ack = e.packet; ack.type = 2;
                if (seen && e.packet.session == last_session && e.packet.seq == last_seq) {
                    ack = last_ack; /* Retransmission: return original result, never execute twice. */
                } else if (seen && e.packet.session == last_session && e.packet.seq < last_seq) {
                    ack.result = ESP_ERR_INVALID_STATE;
                } else {
                    ack.result = pomodoro_execute_command(commands[e.packet.command]);
                    seen = true; last_session = e.packet.session; last_seq = e.packet.seq; last_ack = ack;
                    ESP_LOGI(TAG, "executed remote %s result=%d seq=%lu", commands[ack.command],
                             (int)ack.result, (unsigned long)ack.seq);
                }
                esp_err_t ret = esp_now_send(peer_mac, (uint8_t *)&ack, sizeof(ack));
                if (ret != ESP_OK) ESP_LOGW(TAG, "ACK send failed %s", esp_err_to_name(ret));
            }
        }
        if (pending && esp_timer_get_time() >= deadline) {
            if (attempts >= 3) {
                ESP_LOGW(TAG, "remote %s timeout; execution outcome unknown", commands[tx.command]);
                pending = false;
            } else {
                esp_err_t ret = esp_now_send(peer_mac, (uint8_t *)&tx, sizeof(tx));
                ++attempts; deadline = esp_timer_get_time() + 2000000;
                if (ret != ESP_OK) ESP_LOGW(TAG, "send failed %s", esp_err_to_name(ret));
            }
        }
    }
}
esp_err_t board_bridge_init(void)
{
    if (ready) return ESP_OK;
    uint8_t mac[6]; esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (ret != ESP_OK) return ret;
    if (!memcmp(mac, voice_mac, 6)) voice = true;
    else if (!memcmp(mac, action_mac, 6)) voice = false;
    else { ESP_LOGW(TAG, "not a paired board; bridge disabled"); return ESP_ERR_NOT_SUPPORTED; }
    memcpy(peer_mac, voice ? action_mac : voice_mac, 6);
    queue = xQueueCreate(12, sizeof(event_t));
    if (!queue) return ESP_ERR_NO_MEM;
    ret = esp_now_init(); if (ret != ESP_OK) goto fail;
    ret = esp_now_set_pmk(pmk); if (ret != ESP_OK) goto deinit;
    esp_now_peer_info_t peer = {0}; memcpy(peer.peer_addr, peer_mac, 6);
    memcpy(peer.lmk, lmk, 16); peer.channel = 0; peer.ifidx = WIFI_IF_STA; peer.encrypt = true;
    ret = esp_now_add_peer(&peer); if (ret != ESP_OK) goto deinit;
    ret = esp_now_register_recv_cb(receive); if (ret != ESP_OK) goto deinit;
    if (xTaskCreate(worker, "board_bridge", 4096, NULL, 4, NULL) != pdPASS) {
        ret = ESP_ERR_NO_MEM; goto deinit;
    }
    ready = true;
    ESP_LOGI(TAG, "ready role=%s encrypted; Wi-Fi channel must match", voice ? "voice" : "action");
    return ESP_OK;
deinit:
    esp_now_deinit();
fail:
    vQueueDelete(queue); queue = NULL; return ret;
}
esp_err_t board_bridge_forward(const char *command)
{
    if (!command) return ESP_ERR_INVALID_ARG;
    if (!ready || !voice) return ESP_ERR_INVALID_STATE;
    for (unsigned i = 0; i < 4; ++i) if (!strcmp(command, commands[i])) {
        event_t e = {.local = true}; e.packet.command = i;
        return xQueueSend(queue, &e, 0) == pdTRUE ? ESP_OK : ESP_ERR_NO_MEM;
    }
    return ESP_ERR_INVALID_ARG;
}
#if CONFIG_QM_CLI_SUPPORT
static void bridge_cli(char *buf, int len, int argc, char **argv)
{
    (void)buf; (void)len;
    if (argc == 2 && !strcmp(argv[1], "status")) {
        uint8_t channel = 0; wifi_second_chan_t second;
        (void)esp_wifi_get_channel(&channel, &second);
        qm_cli_printf("link ready=%d role=%s channel=%u tx=%lu ack=%lu result=%d\r\n",
            ready, ready ? (voice ? "voice" : "action") : "uninitialized", channel,
            (unsigned long)__atomic_load_n(&last_tx_seq, __ATOMIC_RELAXED),
            (unsigned long)__atomic_load_n(&last_ack_seq, __ATOMIC_RELAXED),
            __atomic_load_n(&last_ack_result, __ATOMIC_RELAXED));
        return;
    }
    if (argc == 2) {
        esp_err_t ret = board_bridge_forward(argv[1]);
        if (ret == ESP_OK) {
            int local = pomodoro_execute_command(argv[1]);
            qm_cli_printf("link command queued; local_result=%d; wait for remote ACK\r\n", local);
        } else qm_cli_printf("link not queued: %s; connect paired voice board to Wi-Fi first\r\n", esp_err_to_name(ret));
    } else qm_cli_printf("link status|start|pause|resume|stop\r\n");
}
static const struct qm_cli_command bridge_cli_command = {
    .name = "link", .help = "paired-board link status|start|pause|resume|stop", .function = bridge_cli,
};
#endif
int board_bridge_cli_init(void)
{
#if CONFIG_QM_CLI_SUPPORT
    return qm_cli_register_command(&bridge_cli_command);
#else
    return 0;
#endif
}

#else
int board_bridge_cli_init(void) { return 0; }
esp_err_t board_bridge_init(void) { return ESP_ERR_NOT_SUPPORTED; }
esp_err_t board_bridge_forward(const char *cmd) { (void)cmd; return ESP_ERR_NOT_SUPPORTED; }
#endif
