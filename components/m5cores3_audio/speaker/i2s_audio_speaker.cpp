#include "i2s_audio_speaker.h"

// #ifdef USE_ESP32

// #include <driver/i2s.h>
#include <M5Unified.h>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome {
namespace m5cores3_audio {

static const size_t BUFFER_COUNT = 20;

static const char *const TAG = "m5cores3.speaker";

void I2SAudioSpeaker::setup() {
  ESP_LOGI(TAG, "setup");
  auto cfg = M5.Speaker.config();
  cfg.task_priority = 15;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 128;
  M5.Speaker.config(cfg);
  M5.Speaker.begin();
  M5.Speaker.setVolume(200);

  this->buffer_queue_ = xQueueCreate(BUFFER_COUNT, sizeof(DataEvent));
  this->event_queue_ = xQueueCreate(BUFFER_COUNT, sizeof(TaskEvent));
  
  if (!this->buffer_queue_ || !this->event_queue_) {
    ESP_LOGE(TAG, "Failed to create queues");
    this->mark_failed();
    return;
  }
}

void I2SAudioSpeaker::start() { 
  if (this->state_ != speaker::STATE_STOPPED) {
    return;
  }
  this->state_ = speaker::STATE_STARTING; 
}

void I2SAudioSpeaker::start_() {
  if (!this->parent_->try_lock()) {
    return;  // Waiting for another i2s component to return lock
  }
  
  this->state_ = speaker::STATE_RUNNING;
  
  // Create player task with proper error handling
  BaseType_t result = xTaskCreate(I2SAudioSpeaker::player_task, "speaker_task", 8192, (void *) this, 1, &this->player_task_handle_);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Failed to create player task");
    this->parent_->unlock();
    this->state_ = speaker::STATE_STOPPED;
    return;
  }
}

void I2SAudioSpeaker::player_task(void *params) {
  I2SAudioSpeaker *this_speaker = (I2SAudioSpeaker *) params;

  TaskEvent event;
  event.type = TaskEventType::STARTING;
  xQueueSend(this_speaker->event_queue_, &event, portMAX_DELAY);

  // Stop microphone to avoid I2S port conflict
  M5.Mic.end();
  
  // Small delay to ensure mic is fully stopped
  delay(10);
  
  // Start speaker with proper configuration
  auto cfg = M5.Speaker.config();
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 128;
  cfg.task_priority = 15;
  cfg.sample_rate = 16000;
  M5.Speaker.config(cfg);
  
  if (!M5.Speaker.begin()) {
    ESP_LOGE(TAG, "Failed to start speaker");
    event.type = TaskEventType::WARNING;
    event.err = ESP_FAIL;
    xQueueSend(this_speaker->event_queue_, &event, 0);
    event.type = TaskEventType::STOPPED;
    xQueueSend(this_speaker->event_queue_, &event, 0);
    vTaskDelete(NULL);
    return;
  }
  
  ESP_LOGI(TAG, "spk start play");

  DataEvent data_event;
  event.type = TaskEventType::STARTED;
  xQueueSend(this_speaker->event_queue_, &event, portMAX_DELAY);

  int16_t buffer[BUFFER_SIZE / 2];

  while (true) {
    if (xQueueReceive(this_speaker->buffer_queue_, &data_event, 100 / portTICK_PERIOD_MS) != pdTRUE) {
      break;  // End of audio from main thread
    }
    if (data_event.stop) {
      // Stop signal from main thread
      xQueueReset(this_speaker->buffer_queue_);  // Flush queue
      break;
    }

    // Validate data event
    if (data_event.len == 0 || data_event.len > BUFFER_SIZE) {
      ESP_LOGW(TAG, "Invalid data event length: %d", data_event.len);
      continue;
    }

    try {
      // Copy data safely
      memcpy(buffer, data_event.data, data_event.len);
      size_t remaining = data_event.len / 2;
      size_t current = 0;

      while (remaining > 0) {
        // Play buffer with error checking
        if (!M5.Speaker.playRaw(&buffer[current], 1, 16000)) {
          ESP_LOGW(TAG, "Failed to play audio sample");
          break;
        }
        
        // Wait for playback to complete with timeout
        uint32_t timeout = 0;
        while (M5.Speaker.isPlaying() && timeout < 100) {
          delay(1);
          timeout++;
        }
        
        if (timeout >= 100) {
          ESP_LOGW(TAG, "Playback timeout");
          break;
        }

        remaining--;
        current++;
      }

      event.type = TaskEventType::PLAYING;
      xQueueSend(this_speaker->event_queue_, &event, portMAX_DELAY);
      
    } catch (const std::exception& e) {
      ESP_LOGE(TAG, "Exception in player task: %s", e.what());
      break;
    }
  }

  // Stop speaker
  M5.Speaker.end();
  ESP_LOGI(TAG, "spk play end");

  event.type = TaskEventType::STOPPING;
  xQueueSend(this_speaker->event_queue_, &event, portMAX_DELAY);

  event.type = TaskEventType::STOPPED;
  xQueueSend(this_speaker->event_queue_, &event, portMAX_DELAY);

  vTaskDelete(NULL);
}

void I2SAudioSpeaker::stop() {
  if (this->state_ == speaker::STATE_STOPPED) {
    return;
  }
  
  this->state_ = speaker::STATE_STOPPING;
  
  // Send stop signal to player task
  if (this->buffer_queue_) {
    DataEvent event;
    event.stop = true;
    xQueueSend(this->buffer_queue_, &event, 0);
  }
}

void I2SAudioSpeaker::watch_() {
  TaskEvent event;
  if (xQueueReceive(this->event_queue_, &event, 0) == pdTRUE) {
    switch (event.type) {
      case TaskEventType::STARTING:
        ESP_LOGD(TAG, "Starting");
        break;
      case TaskEventType::STARTED:
        ESP_LOGD(TAG, "Started");
        break;
      case TaskEventType::PLAYING:
        ESP_LOGD(TAG, "Playing");
        break;
      case TaskEventType::STOPPING:
        ESP_LOGD(TAG, "Stopping");
        break;
      case TaskEventType::STOPPED:
        ESP_LOGD(TAG, "Stopped");
        this->state_ = speaker::STATE_STOPPED;
        this->parent_->unlock();
        if (this->player_task_handle_) {
          this->player_task_handle_ = nullptr;
        }
        break;
      case TaskEventType::WARNING:
        ESP_LOGW(TAG, "Task warning: %s", esp_err_to_name(event.err));
        break;
    }
  }
}

void I2SAudioSpeaker::loop() {
  switch (this->state_) {
    case speaker::STATE_STARTING:
      this->start_();
      break;
    case speaker::STATE_RUNNING:
    case speaker::STATE_STOPPING:
      this->watch_();
      break;
    case speaker::STATE_STOPPED:
      break;
  }
}

size_t I2SAudioSpeaker::play(const uint8_t *data, size_t length) {
  if (!data || length == 0) {
    return 0;
  }

  if (this->state_ != speaker::STATE_RUNNING && this->state_ != speaker::STATE_STARTING) {
    this->start();
  }
  
  if (!this->buffer_queue_) {
    return 0;
  }

  size_t remaining = length;
  size_t index = 0;
  
  while (remaining > 0) {
    DataEvent event;
    event.stop = false;
    size_t to_send_length = std::min(remaining, BUFFER_SIZE);
    event.len = to_send_length;
    
    // Safe memory copy
    memcpy(event.data, data + index, to_send_length);
    
    if (xQueueSend(this->buffer_queue_, &event, 10 / portTICK_PERIOD_MS) != pdTRUE) {
      // Queue is full, return how much we've sent so far
      return index;
    }
    
    remaining -= to_send_length;
    index += to_send_length;
  }
  
  return index;
}

bool I2SAudioSpeaker::has_buffered_data() const {
  if (!this->buffer_queue_) {
    return false;
  }
  return uxQueueMessagesWaiting(this->buffer_queue_) > 0;
}

}  // namespace m5cores3_audio
}  // namespace esphome

// #endif  // USE_ESP32
