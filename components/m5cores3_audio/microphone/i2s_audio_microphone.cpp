#include "i2s_audio_microphone.h"

// #ifdef USE_ESP32

// #include <driver/i2s.h>
#include <M5Unified.h>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace m5cores3_audio {

static const size_t BUFFER_SIZE = 512;

static const char *const TAG = "m5cores3.microphone";

void I2SAudioMicrophone::setup() {
  ESP_LOGI(TAG, "setup");
}

void I2SAudioMicrophone::start() {
  if (this->is_failed())
    return;
  if (this->state_ == microphone::STATE_RUNNING)
    return;  // Already running
  this->state_ = microphone::STATE_STARTING;
}

void I2SAudioMicrophone::start_() {
  if (!this->parent_->try_lock()) {
    return;  // Waiting for another i2s to return lock
  }

  // Stop speaker to avoid I2S port conflict
  M5.Speaker.end();
  
  // Small delay to ensure speaker is fully stopped
  delay(10);
  
  // Start microphone
  auto cfg = M5.Mic.config();
  cfg.task_priority = 15;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.sample_rate = 16000;
  M5.Mic.config(cfg);
  
  if (!M5.Mic.begin()) {
    ESP_LOGE(TAG, "Failed to start microphone");
    this->parent_->unlock();
    this->state_ = microphone::STATE_STOPPED;
    return;
  }
  
  ESP_LOGI(TAG, "start mic");
  this->state_ = microphone::STATE_RUNNING;
  this->high_freq_.start();
}

void I2SAudioMicrophone::stop() {
  if (this->state_ == microphone::STATE_STOPPED || this->is_failed())
    return;
  if (this->state_ == microphone::STATE_STARTING) {
    this->state_ = microphone::STATE_STOPPED;
    return;
  }
  this->state_ = microphone::STATE_STOPPING;
}

void I2SAudioMicrophone::stop_() {
  // Stop microphone
  M5.Mic.end();
  ESP_LOGI(TAG, "mic end");
  
  // Small delay to ensure mic is fully stopped
  delay(10);
  
  // Restart speaker
  auto cfg = M5.Speaker.config();
  cfg.task_priority = 15;
  M5.Speaker.config(cfg);
  M5.Speaker.begin();
  
  this->parent_->unlock();
  this->state_ = microphone::STATE_STOPPED;
  this->high_freq_.stop();
}

size_t I2SAudioMicrophone::read_(uint8_t *buf, size_t len, TickType_t ticks_to_wait) {
  if (!buf || len == 0) {
    ESP_LOGW(TAG, "Invalid buffer parameters");
    return 0;
  }

  // Ensure we don't exceed buffer limits
  size_t samples_to_read = len / sizeof(int16_t);
  if (samples_to_read == 0) {
    ESP_LOGW(TAG, "Buffer too small for samples");
    return 0;
  }

  // Limit maximum samples to prevent memory issues
  if (samples_to_read > 1024) {
    samples_to_read = 1024;
    len = samples_to_read * sizeof(int16_t);
  }

  try {
    // Use static buffer to avoid frequent allocations
    static std::vector<int16_t> temp_buf;
    temp_buf.resize(samples_to_read);

    // Record into temporary buffer with error checking
    if (!M5.Mic.record(temp_buf.data(), samples_to_read, 16000)) {
      ESP_LOGW(TAG, "Failed to start recording");
      this->status_set_warning();
      return 0;
    }

    // Wait for recording to complete with timeout
    uint32_t timeout = 0;
    while (M5.Mic.isRecording() && timeout < 1000) {
      delay(1);
      timeout++;
    }

    if (timeout >= 1000) {
      ESP_LOGW(TAG, "Recording timeout");
      this->status_set_warning();
      return 0;
    }

    // Copy data safely
    memcpy(buf, temp_buf.data(), len);
    this->status_clear_warning();
    return len;
    
  } catch (const std::exception& e) {
    ESP_LOGE(TAG, "Exception in read_: %s", e.what());
    this->status_set_warning();
    return 0;
  }
}

void I2SAudioMicrophone::loop() {
  switch (this->state_) {
    case microphone::STATE_STARTING:
      this->start_();
      break;
    case microphone::STATE_STOPPING:
      this->stop_();
      break;
    case microphone::STATE_RUNNING:
    case microphone::STATE_STOPPED:
      break;
  }
}

}  // namespace m5cores3_audio
}  // namespace esphome

// #endif  // USE_ESP32
