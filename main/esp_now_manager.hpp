#pragma once

#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_crc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <string.h>
#include <vector>
#include <functional>
#include <chrono>

#define ESP_NOW_MANAGER_TAG "ESP_NOW_MGR"
#define ESP_NOW_MAX_PEERS 20  // Increased for testing - will measure actual limits
#define ESP_NOW_BROADCAST_ADDR {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define ESP_NOW_CHANNEL_5GHZ 36
#define ESP_NOW_MAX_DATA_LEN 250

typedef enum {
    ESP_NOW_MSG_TYPE_DISCOVERY_REQUEST = 0x01,
    ESP_NOW_MSG_TYPE_DISCOVERY_RESPONSE = 0x02,
    ESP_NOW_MSG_TYPE_PING = 0x10,
    ESP_NOW_MSG_TYPE_PONG = 0x11,
    ESP_NOW_MSG_TYPE_DATA = 0x20,
    ESP_NOW_MSG_TYPE_TEST_START = 0x30,
    ESP_NOW_MSG_TYPE_TEST_STOP = 0x31,
    ESP_NOW_MSG_TYPE_TEST_DATA = 0x32,
} esp_now_msg_type_t;

typedef struct {
    uint8_t msg_type;
    uint32_t sequence_number;
    uint64_t timestamp_us;
    uint16_t payload_length;
    uint32_t crc32;
    uint8_t payload[ESP_NOW_MAX_DATA_LEN - 16];
} __attribute__((packed)) esp_now_message_t;

typedef struct {
    uint8_t mac_addr[6];
    int8_t rssi;
    uint64_t last_seen_us;
    uint32_t packets_sent;
    uint32_t packets_received;
    uint32_t packets_lost;
    bool is_active;
} esp_now_peer_info_t;

typedef struct {
    uint32_t total_packets_sent;
    uint32_t total_packets_received;
    uint32_t total_packets_lost;
    uint32_t discovery_requests_sent;
    uint32_t discovery_responses_received;
    uint64_t total_bytes_sent;
    uint64_t total_bytes_received;
    uint64_t session_start_time_us;
} esp_now_statistics_t;

typedef std::function<void(const uint8_t*, const esp_now_message_t*)> esp_now_receive_callback_t;
typedef std::function<void(const uint8_t*, esp_now_send_status_t)> esp_now_send_callback_t;
typedef std::function<void(const esp_now_peer_info_t*)> esp_now_peer_discovered_callback_t;

class ESPNowManager {
private:
    bool initialized_;
    bool discovery_active_;
    uint8_t local_mac_[6];
    uint32_t sequence_counter_;

    std::vector<esp_now_peer_info_t> peers_;
    esp_now_statistics_t statistics_;

    QueueHandle_t receive_queue_;
    QueueHandle_t send_queue_;
    SemaphoreHandle_t peers_mutex_;
    TaskHandle_t receive_task_handle_;
    TaskHandle_t send_task_handle_;
    TaskHandle_t discovery_task_handle_;

    esp_now_receive_callback_t receive_callback_;
    esp_now_send_callback_t send_callback_;
    esp_now_peer_discovered_callback_t peer_discovered_callback_;

    static void esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
    static void esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len);

    static void receive_task(void *parameter);
    static void send_task(void *parameter);
    static void discovery_task(void *parameter);

    esp_err_t add_peer_internal(const uint8_t *mac_addr);
    esp_now_peer_info_t* find_peer(const uint8_t *mac_addr);
    uint64_t get_timestamp_us();
    void update_peer_stats(const uint8_t *mac_addr, bool is_received, bool is_lost = false);

    // Core networking functions
    void update_peer_rssi(esp_now_peer_info_t* peer, int8_t rssi);

public:
    ESPNowManager();
    ~ESPNowManager();

    esp_err_t initialize(uint8_t channel = ESP_NOW_CHANNEL_5GHZ);
    esp_err_t deinitialize();

    esp_err_t start_discovery(uint32_t duration_ms = 10000);
    esp_err_t stop_discovery();
    esp_err_t send_discovery_request();

    esp_err_t add_peer(const uint8_t *mac_addr);
    esp_err_t remove_peer(const uint8_t *mac_addr);
    bool is_peer_registered(const uint8_t *mac_addr);
    std::vector<esp_now_peer_info_t> get_peers();
    size_t get_peer_count();

    esp_err_t send_message(const uint8_t *mac_addr, esp_now_msg_type_t msg_type,
                          const uint8_t *data, size_t len);
    esp_err_t send_broadcast(esp_now_msg_type_t msg_type, const uint8_t *data, size_t len);
    esp_err_t send_ping(const uint8_t *mac_addr);

    // Network testing utilities
    esp_err_t send_test_message(const uint8_t *mac_addr, const uint8_t *data, size_t len);
    std::vector<esp_now_peer_info_t> get_peers_by_rssi(int8_t min_rssi = -80);
    esp_now_peer_info_t* get_strongest_peer();

    const uint8_t* get_local_mac();
    esp_now_statistics_t get_statistics();
    void reset_statistics();

    void set_receive_callback(esp_now_receive_callback_t callback);
    void set_send_callback(esp_now_send_callback_t callback);
    void set_peer_discovered_callback(esp_now_peer_discovered_callback_t callback);

    static ESPNowManager& get_instance();
};