// #include "i2s_audio.h"
#include "m5cores3_audio.h"

// #ifdef USE_ESP32

#include "esphome/core/log.h"
#include <esp_heap_caps.h>
#include <esp_system.h>

namespace esphome {
namespace m5cores3_audio {

static const char *const TAG = "m5cores3.audio";

void M5CoreS3AudioComponent::setup() {
  ESP_LOGI(TAG, "setup");
  
  // Log memory information
  ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
  ESP_LOGI(TAG, "Free PSRAM: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  ESP_LOGI(TAG, "Minimum free heap: %d bytes", esp_get_minimum_free_heap_size());
  
  // Initialize M5 with proper configuration
  auto cfg = M5.config();
  cfg.clear_display = true;
  cfg.output_power = true;
  cfg.internal_imu = true;
  cfg.internal_rtc = true;
  cfg.internal_spk = true;
  cfg.internal_mic = true;
  cfg.external_spk = false;
  cfg.external_spk_detail.omit_spk_class = true;
  M5.begin(cfg);
  
  // Set up error handling
  esp_register_shutdown_handler([]() {
    ESP_LOGI(TAG, "System shutdown handler called");
    M5.Speaker.end();
    M5.Mic.end();
  });
  
  ESP_LOGI(TAG, "M5CoreS3 Audio Component initialized successfully");
}

void M5CoreS3AudioComponent::loop() {
  // Monitor memory usage periodically
  static uint32_t last_memory_check = 0;
  uint32_t now = millis();
  
  if (now - last_memory_check > 30000) { // Check every 30 seconds
    last_memory_check = now;
    
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    
    if (free_heap < 50000) { // Less than 50KB free
      ESP_LOGW(TAG, "Low memory warning: %d bytes free", free_heap);
    }
    
    if (min_free_heap < 30000) { // Minimum ever was less than 30KB
      ESP_LOGW(TAG, "Critical memory warning: minimum free was %d bytes", min_free_heap);
    }
    
    ESP_LOGD(TAG, "Memory status - Free: %d, Min: %d", free_heap, min_free_heap);
  }
}

void M5CoreS3AudioComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "M5CoreS3 Audio Component:");
  ESP_LOGCONFIG(TAG, "  Free heap: %d bytes", esp_get_free_heap_size());
  ESP_LOGCONFIG(TAG, "  Free PSRAM: %d bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  ESP_LOGCONFIG(TAG, "  Port: %d", this->port_);
}

}  // namespace m5cores3_audio
}  // namespace esphome

// #endif  // USE_ESP32
