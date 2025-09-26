#include "esp_now_manager.hpp"

static ESPNowManager* esp_now_manager_instance = nullptr;

ESPNowManager::ESPNowManager()
    : initialized_(false), discovery_active_(false), sequence_counter_(0),
      receive_queue_(nullptr), send_queue_(nullptr), peers_mutex_(nullptr),
      receive_task_handle_(nullptr), send_task_handle_(nullptr),
      discovery_task_handle_(nullptr) {
    memset(&statistics_, 0, sizeof(statistics_));
    memset(local_mac_, 0, sizeof(local_mac_));
}

ESPNowManager::~ESPNowManager() {
    deinitialize();
}

ESPNowManager& ESPNowManager::get_instance() {
    if (!esp_now_manager_instance) {
        esp_now_manager_instance = new ESPNowManager();
    }
    return *esp_now_manager_instance;
}

esp_err_t ESPNowManager::initialize(uint8_t channel) {
    if (initialized_) {
        ESP_LOGW(ESP_NOW_MANAGER_TAG, "Manager already initialized");
        return ESP_OK;
    }

    ESP_LOGI(ESP_NOW_MANAGER_TAG, "Initializing ESP-NOW Manager for 5GHz (Channel %d)", channel);

    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to initialize netif: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to set WiFi storage: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to set WiFi channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to initialize ESP-NOW: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_now_register_send_cb(esp_now_send_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to register send callback: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_now_register_recv_cb(esp_now_recv_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to register recv callback: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_get_mac(WIFI_IF_STA, local_mac_);
    if (ret != ESP_OK) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to get MAC address: %s", esp_err_to_name(ret));
        return ret;
    }

    receive_queue_ = xQueueCreate(20, sizeof(esp_now_message_t) + 6);
    send_queue_ = xQueueCreate(20, sizeof(esp_now_message_t) + 6);
    peers_mutex_ = xSemaphoreCreateMutex();

    if (!receive_queue_ || !send_queue_ || !peers_mutex_) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to create queues or mutex");
        return ESP_ERR_NO_MEM;
    }

    xTaskCreate(receive_task, "esp_now_recv", 4096, this, 5, &receive_task_handle_);
    xTaskCreate(send_task, "esp_now_send", 4096, this, 5, &send_task_handle_);

    statistics_.session_start_time_us = get_timestamp_us();
    initialized_ = true;

    ESP_LOGI(ESP_NOW_MANAGER_TAG, "ESP-NOW Manager initialized successfully");
    ESP_LOGI(ESP_NOW_MANAGER_TAG, "Local MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             local_mac_[0], local_mac_[1], local_mac_[2],
             local_mac_[3], local_mac_[4], local_mac_[5]);

    return ESP_OK;
}

esp_err_t ESPNowManager::deinitialize() {
    if (!initialized_) {
        return ESP_OK;
    }

    stop_discovery();

    if (receive_task_handle_) {
        vTaskDelete(receive_task_handle_);
        receive_task_handle_ = nullptr;
    }

    if (send_task_handle_) {
        vTaskDelete(send_task_handle_);
        send_task_handle_ = nullptr;
    }

    if (receive_queue_) {
        vQueueDelete(receive_queue_);
        receive_queue_ = nullptr;
    }

    if (send_queue_) {
        vQueueDelete(send_queue_);
        send_queue_ = nullptr;
    }

    if (peers_mutex_) {
        vSemaphoreDelete(peers_mutex_);
        peers_mutex_ = nullptr;
    }

    esp_now_deinit();
    esp_wifi_stop();
    esp_wifi_deinit();

    peers_.clear();
    initialized_ = false;

    ESP_LOGI(ESP_NOW_MANAGER_TAG, "ESP-NOW Manager deinitialized");
    return ESP_OK;
}

void ESPNowManager::esp_now_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
    ESPNowManager& manager = get_instance();
    if (manager.send_callback_) {
        manager.send_callback_(mac_addr, status);
    }

    if (status == ESP_NOW_SEND_SUCCESS) {
        manager.statistics_.total_packets_sent++;
        manager.update_peer_stats(mac_addr, false, false);
    } else {
        manager.statistics_.total_packets_lost++;
        manager.update_peer_stats(mac_addr, false, true);
    }
}

void ESPNowManager::esp_now_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    ESPNowManager& manager = get_instance();

    if (len < sizeof(esp_now_message_t)) {
        ESP_LOGW(ESP_NOW_MANAGER_TAG, "Received message too short: %d bytes", len);
        return;
    }

    esp_now_message_t* msg = (esp_now_message_t*)data;

    uint32_t calculated_crc = esp_crc32_le(0, data, len - sizeof(uint32_t));
    if (calculated_crc != msg->crc32) {
        ESP_LOGW(ESP_NOW_MANAGER_TAG, "CRC mismatch in received message");
        return;
    }

    manager.statistics_.total_packets_received++;
    manager.statistics_.total_bytes_received += len;
    manager.update_peer_stats(recv_info->src_addr, true, false);

    typedef struct {
        uint8_t mac_addr[6];
        esp_now_message_t msg;
    } queued_message_t;

    queued_message_t queued_msg;
    memcpy(queued_msg.mac_addr, recv_info->src_addr, 6);
    memcpy(&queued_msg.msg, msg, sizeof(esp_now_message_t));

    if (xQueueSend(manager.receive_queue_, &queued_msg, 0) != pdPASS) {
        ESP_LOGW(ESP_NOW_MANAGER_TAG, "Receive queue full, dropping message");
    }
}

void ESPNowManager::receive_task(void *parameter) {
    ESPNowManager* manager = (ESPNowManager*)parameter;

    typedef struct {
        uint8_t mac_addr[6];
        esp_now_message_t msg;
    } queued_message_t;

    queued_message_t received_msg;

    while (true) {
        if (xQueueReceive(manager->receive_queue_, &received_msg, portMAX_DELAY) == pdPASS) {

            if (received_msg.msg.msg_type == ESP_NOW_MSG_TYPE_DISCOVERY_REQUEST) {
                ESP_LOGD(ESP_NOW_MANAGER_TAG, "Received discovery request from %02x:%02x:%02x:%02x:%02x:%02x",
                         received_msg.mac_addr[0], received_msg.mac_addr[1], received_msg.mac_addr[2],
                         received_msg.mac_addr[3], received_msg.mac_addr[4], received_msg.mac_addr[5]);

                manager->add_peer_internal(received_msg.mac_addr);

                uint8_t response_data[6];
                memcpy(response_data, manager->local_mac_, 6);
                manager->send_message(received_msg.mac_addr, ESP_NOW_MSG_TYPE_DISCOVERY_RESPONSE,
                                     response_data, 6);

                if (manager->peer_discovered_callback_) {
                    esp_now_peer_info_t* peer = manager->find_peer(received_msg.mac_addr);
                    if (peer) {
                        manager->peer_discovered_callback_(peer);
                    }
                }
            }
            else if (received_msg.msg.msg_type == ESP_NOW_MSG_TYPE_DISCOVERY_RESPONSE) {
                ESP_LOGD(ESP_NOW_MANAGER_TAG, "Received discovery response from %02x:%02x:%02x:%02x:%02x:%02x",
                         received_msg.mac_addr[0], received_msg.mac_addr[1], received_msg.mac_addr[2],
                         received_msg.mac_addr[3], received_msg.mac_addr[4], received_msg.mac_addr[5]);

                manager->add_peer_internal(received_msg.mac_addr);
                manager->statistics_.discovery_responses_received++;

                if (manager->peer_discovered_callback_) {
                    esp_now_peer_info_t* peer = manager->find_peer(received_msg.mac_addr);
                    if (peer) {
                        manager->peer_discovered_callback_(peer);
                    }
                }
            }
            else if (received_msg.msg.msg_type == ESP_NOW_MSG_TYPE_PING) {
                ESP_LOGD(ESP_NOW_MANAGER_TAG, "Received ping, sending pong");
                manager->send_message(received_msg.mac_addr, ESP_NOW_MSG_TYPE_PONG,
                                     (uint8_t*)&received_msg.msg.sequence_number, sizeof(uint32_t));
            }

            if (manager->receive_callback_) {
                manager->receive_callback_(received_msg.mac_addr, &received_msg.msg);
            }
        }
    }
}

void ESPNowManager::send_task(void *parameter) {
    ESPNowManager* manager = (ESPNowManager*)parameter;

    typedef struct {
        uint8_t mac_addr[6];
        esp_now_message_t msg;
    } queued_message_t;

    queued_message_t send_msg;

    while (true) {
        if (xQueueReceive(manager->send_queue_, &send_msg, portMAX_DELAY) == pdPASS) {
            esp_err_t result = esp_now_send(send_msg.mac_addr, (uint8_t*)&send_msg.msg,
                                           sizeof(esp_now_message_t));
            if (result != ESP_OK) {
                ESP_LOGW(ESP_NOW_MANAGER_TAG, "Failed to send message: %s", esp_err_to_name(result));
            } else {
                manager->statistics_.total_bytes_sent += sizeof(esp_now_message_t);
            }
        }
    }
}

void ESPNowManager::discovery_task(void *parameter) {
    ESPNowManager* manager = (ESPNowManager*)parameter;

    while (manager->discovery_active_) {
        manager->send_discovery_request();
        vTaskDelay(pdMS_TO_TICKS(1000)); // Send discovery request every second
    }

    manager->discovery_task_handle_ = nullptr;
    vTaskDelete(nullptr);
}

esp_err_t ESPNowManager::start_discovery(uint32_t duration_ms) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (discovery_active_) {
        ESP_LOGW(ESP_NOW_MANAGER_TAG, "Discovery already active");
        return ESP_OK;
    }

    ESP_LOGI(ESP_NOW_MANAGER_TAG, "Starting device discovery for %lu ms", duration_ms);

    uint8_t broadcast_addr[] = ESP_NOW_BROADCAST_ADDR;
    esp_now_peer_info_t broadcast_peer = {};
    memcpy(broadcast_peer.peer_addr, broadcast_addr, 6);
    broadcast_peer.channel = ESP_NOW_CHANNEL_5GHZ;
    broadcast_peer.encrypt = false;

    esp_err_t ret = esp_now_add_peer(&broadcast_peer);
    if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to add broadcast peer: %s", esp_err_to_name(ret));
        return ret;
    }

    discovery_active_ = true;
    xTaskCreate(discovery_task, "esp_now_discovery", 2048, this, 4, &discovery_task_handle_);

    if (duration_ms > 0) {
        // Auto-stop discovery after duration
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        stop_discovery();
    }

    return ESP_OK;
}

esp_err_t ESPNowManager::stop_discovery() {
    if (!discovery_active_) {
        return ESP_OK;
    }

    ESP_LOGI(ESP_NOW_MANAGER_TAG, "Stopping device discovery");
    discovery_active_ = false;

    if (discovery_task_handle_) {
        vTaskDelete(discovery_task_handle_);
        discovery_task_handle_ = nullptr;
    }

    return ESP_OK;
}

esp_err_t ESPNowManager::send_discovery_request() {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t broadcast_addr[] = ESP_NOW_BROADCAST_ADDR;
    uint8_t discovery_data[6];
    memcpy(discovery_data, local_mac_, 6);

    statistics_.discovery_requests_sent++;
    return send_message(broadcast_addr, ESP_NOW_MSG_TYPE_DISCOVERY_REQUEST, discovery_data, 6);
}

esp_err_t ESPNowManager::add_peer_internal(const uint8_t *mac_addr) {
    if (xSemaphoreTake(peers_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_now_peer_info_t* existing_peer = find_peer(mac_addr);
    if (existing_peer) {
        existing_peer->last_seen_us = get_timestamp_us();
        existing_peer->is_active = true;
        xSemaphoreGive(peers_mutex_);
        return ESP_OK;
    }

    // Removed artificial peer limit - let's test natural ESP-NOW capabilities
    if (peers_.size() >= ESP_NOW_MAX_PEERS) {
        ESP_LOGW(ESP_NOW_MANAGER_TAG, "Peer count (%zu) reaching test limit (%d) - monitoring performance",
                 peers_.size(), ESP_NOW_MAX_PEERS);
        // Continue adding peers to test actual limits
    }

    esp_now_peer_info_t new_peer = {};
    memcpy(new_peer.mac_addr, mac_addr, 6);
    new_peer.last_seen_us = get_timestamp_us();
    new_peer.is_active = true;

    esp_now_peer_info_t esp_peer = {};
    memcpy(esp_peer.peer_addr, mac_addr, 6);
    esp_peer.channel = ESP_NOW_CHANNEL_5GHZ;
    esp_peer.encrypt = false;

    esp_err_t ret = esp_now_add_peer(&esp_peer);
    if (ret != ESP_OK && ret != ESP_ERR_ESPNOW_EXIST) {
        xSemaphoreGive(peers_mutex_);
        ESP_LOGE(ESP_NOW_MANAGER_TAG, "Failed to add ESP-NOW peer: %s", esp_err_to_name(ret));
        return ret;
    }

    peers_.push_back(new_peer);
    xSemaphoreGive(peers_mutex_);

    ESP_LOGI(ESP_NOW_MANAGER_TAG, "Added peer: %02x:%02x:%02x:%02x:%02x:%02x",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

    return ESP_OK;
}

esp_err_t ESPNowManager::add_peer(const uint8_t *mac_addr) {
    return add_peer_internal(mac_addr);
}

esp_err_t ESPNowManager::remove_peer(const uint8_t *mac_addr) {
    if (xSemaphoreTake(peers_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (auto it = peers_.begin(); it != peers_.end(); ++it) {
        if (memcmp(it->mac_addr, mac_addr, 6) == 0) {
            esp_now_del_peer(mac_addr);
            peers_.erase(it);
            xSemaphoreGive(peers_mutex_);
            ESP_LOGI(ESP_NOW_MANAGER_TAG, "Removed peer: %02x:%02x:%02x:%02x:%02x:%02x",
                     mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
            return ESP_OK;
        }
    }

    xSemaphoreGive(peers_mutex_);
    return ESP_ERR_NOT_FOUND;
}

bool ESPNowManager::is_peer_registered(const uint8_t *mac_addr) {
    if (xSemaphoreTake(peers_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }

    bool found = (find_peer(mac_addr) != nullptr);
    xSemaphoreGive(peers_mutex_);
    return found;
}

esp_now_peer_info_t* ESPNowManager::find_peer(const uint8_t *mac_addr) {
    for (auto& peer : peers_) {
        if (memcmp(peer.mac_addr, mac_addr, 6) == 0) {
            return &peer;
        }
    }
    return nullptr;
}

std::vector<esp_now_peer_info_t> ESPNowManager::get_peers() {
    if (xSemaphoreTake(peers_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return std::vector<esp_now_peer_info_t>();
    }

    std::vector<esp_now_peer_info_t> result = peers_;
    xSemaphoreGive(peers_mutex_);
    return result;
}

size_t ESPNowManager::get_peer_count() {
    if (xSemaphoreTake(peers_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return 0;
    }

    size_t count = peers_.size();
    xSemaphoreGive(peers_mutex_);
    return count;
}

esp_err_t ESPNowManager::send_message(const uint8_t *mac_addr, esp_now_msg_type_t msg_type,
                                     const uint8_t *data, size_t len) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    if (len > sizeof(((esp_now_message_t*)0)->payload)) {
        return ESP_ERR_INVALID_SIZE;
    }

    typedef struct {
        uint8_t mac_addr[6];
        esp_now_message_t msg;
    } queued_message_t;

    queued_message_t send_msg;
    memcpy(send_msg.mac_addr, mac_addr, 6);

    send_msg.msg.msg_type = msg_type;
    send_msg.msg.sequence_number = sequence_counter_++;
    send_msg.msg.timestamp_us = get_timestamp_us();
    send_msg.msg.payload_length = len;

    if (data && len > 0) {
        memcpy(send_msg.msg.payload, data, len);
    }

    send_msg.msg.crc32 = esp_crc32_le(0, (uint8_t*)&send_msg.msg,
                                     sizeof(esp_now_message_t) - sizeof(uint32_t));

    if (xQueueSend(send_queue_, &send_msg, pdMS_TO_TICKS(1000)) != pdPASS) {
        ESP_LOGW(ESP_NOW_MANAGER_TAG, "Send queue full");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t ESPNowManager::send_broadcast(esp_now_msg_type_t msg_type, const uint8_t *data, size_t len) {
    uint8_t broadcast_addr[] = ESP_NOW_BROADCAST_ADDR;
    return send_message(broadcast_addr, msg_type, data, len);
}

esp_err_t ESPNowManager::send_ping(const uint8_t *mac_addr) {
    uint32_t ping_id = sequence_counter_;
    return send_message(mac_addr, ESP_NOW_MSG_TYPE_PING, (uint8_t*)&ping_id, sizeof(ping_id));
}

const uint8_t* ESPNowManager::get_local_mac() {
    return local_mac_;
}

esp_now_statistics_t ESPNowManager::get_statistics() {
    return statistics_;
}

void ESPNowManager::reset_statistics() {
    memset(&statistics_, 0, sizeof(statistics_));
    statistics_.session_start_time_us = get_timestamp_us();
}

uint64_t ESPNowManager::get_timestamp_us() {
    return esp_timer_get_time();
}

void ESPNowManager::update_peer_stats(const uint8_t *mac_addr, bool is_received, bool is_lost) {
    if (xSemaphoreTake(peers_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    esp_now_peer_info_t* peer = find_peer(mac_addr);
    if (peer) {
        if (is_received) {
            peer->packets_received++;
        } else if (is_lost) {
            peer->packets_lost++;
        } else {
            peer->packets_sent++;
        }
        peer->last_seen_us = get_timestamp_us();
    }

    xSemaphoreGive(peers_mutex_);
}

void ESPNowManager::set_receive_callback(esp_now_receive_callback_t callback) {
    receive_callback_ = callback;
}

void ESPNowManager::set_send_callback(esp_now_send_callback_t callback) {
    send_callback_ = callback;
}

void ESPNowManager::set_peer_discovered_callback(esp_now_peer_discovered_callback_t callback) {
    peer_discovered_callback_ = callback;
}