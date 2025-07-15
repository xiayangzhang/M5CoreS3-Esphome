#pragma once

// #ifdef USE_ESP32

// #include "../i2s_audio.h"
#include "../m5cores3_audio.h"

#include "esphome/components/microphone/microphone.h"
#include "esphome/core/component.h"

namespace esphome {
namespace m5cores3_audio {

class I2SAudioMicrophone : public I2SAudioIn, public microphone::Microphone, public Component {
 public:
  void setup() override;
  void start() override;
  void stop() override;

  void loop() override;

  void set_din_pin(int8_t pin) { this->din_pin_ = pin; }
  void set_pdm(bool pdm) { this->pdm_ = pdm; }


#if SOC_I2S_SUPPORTS_ADC
  void set_adc_channel(adc1_channel_t channel) {
    this->adc_channel_ = channel;
    this->adc_ = true;
  }
#endif

  void set_channel(i2s_channel_fmt_t channel) { this->channel_ = channel; }
  void set_bits_per_sample(i2s_bits_per_sample_t bits_per_sample) { this->bits_per_sample_ = bits_per_sample; }
  
  // Audio format support methods for ESPHome 2025.8.0+ compatibility
  void set_sample_rate(uint32_t sample_rate) { this->sample_rate_ = sample_rate; }
  void set_channels(uint8_t channels) { this->channels_ = channels; }
  void set_bits_per_sample_val(uint8_t bits) { this->bits_per_sample_val_ = bits; }

 protected:
  void start_();
  void stop_();
  size_t read_(uint8_t *buf, size_t len, TickType_t ticks_to_wait);

  int8_t din_pin_{I2S_PIN_NO_CHANGE};
#if SOC_I2S_SUPPORTS_ADC
  adc1_channel_t adc_channel_{ADC1_CHANNEL_MAX};
  bool adc_{false};
#endif
  bool pdm_{false};
  i2s_channel_fmt_t channel_;
  i2s_bits_per_sample_t bits_per_sample_;
  
  // Audio format properties for ESPHome 2025.8.0+ compatibility
  uint32_t sample_rate_{16000};
  uint8_t channels_{1};
  uint8_t bits_per_sample_val_{16};

  HighFrequencyLoopRequester high_freq_;
};

}  // namespace i2s_audio
}  // namespace esphome

// #endif  // USE_ESP32
