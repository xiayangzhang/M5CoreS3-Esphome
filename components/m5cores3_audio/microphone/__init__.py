import esphome.config_validation as cv
import esphome.codegen as cg

from esphome import pins
from esphome.const import CONF_CHANNEL, CONF_ID, CONF_NUMBER
from esphome.components import microphone, esp32
from esphome.components.adc import ESP32_VARIANT_ADC1_PIN_TO_CHANNEL, validate_adc_pin

from .. import (
    i2s_audio_ns,
    I2SAudioComponent,
    I2SAudioIn,
    CONF_I2S_AUDIO_ID,
    CONF_I2S_DIN_PIN,
)

# Audio format constants
CONF_SAMPLE_RATE = "sample_rate"
CONF_CHANNELS = "channels"

CODEOWNERS = ["@jesserockz"]
# DEPENDENCIES = ["i2s_audio"]
DEPENDENCIES = ["m5cores3_audio"]

CONF_ADC_PIN = "adc_pin"
CONF_ADC_TYPE = "adc_type"
CONF_PDM = "pdm"
CONF_BITS_PER_SAMPLE = "bits_per_sample"

I2SAudioMicrophone = i2s_audio_ns.class_(
    "I2SAudioMicrophone", I2SAudioIn, microphone.Microphone, cg.Component
)

i2s_channel_fmt_t = cg.global_ns.enum("i2s_channel_fmt_t")
CHANNELS = {
    "left": i2s_channel_fmt_t.I2S_CHANNEL_FMT_ONLY_LEFT,
    "right": i2s_channel_fmt_t.I2S_CHANNEL_FMT_ONLY_RIGHT,
}
i2s_bits_per_sample_t = cg.global_ns.enum("i2s_bits_per_sample_t")
BITS_PER_SAMPLE = {
    16: i2s_bits_per_sample_t.I2S_BITS_PER_SAMPLE_16BIT,
    32: i2s_bits_per_sample_t.I2S_BITS_PER_SAMPLE_32BIT,
}

INTERNAL_ADC_VARIANTS = [esp32.const.VARIANT_ESP32]
PDM_VARIANTS = [esp32.const.VARIANT_ESP32, esp32.const.VARIANT_ESP32S3]

_validate_bits = cv.float_with_unit("bits", "bit")


def validate_esp32_variant(config):
    variant = esp32.get_esp32_variant()
    if config[CONF_ADC_TYPE] == "external":
        if config[CONF_PDM]:
            if variant not in PDM_VARIANTS:
                raise cv.Invalid(f"{variant} does not support PDM")
        return config
    if config[CONF_ADC_TYPE] == "internal":
        if variant not in INTERNAL_ADC_VARIANTS:
            raise cv.Invalid(f"{variant} does not have an internal ADC")
        return config
    raise NotImplementedError


BASE_SCHEMA = microphone.MICROPHONE_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(I2SAudioMicrophone),
        cv.GenerateID(CONF_I2S_AUDIO_ID): cv.use_id(I2SAudioComponent),
        cv.Optional(CONF_CHANNEL, default="right"): cv.enum(CHANNELS),
        cv.Optional(CONF_BITS_PER_SAMPLE, default="32bit"): cv.All(
            _validate_bits, cv.enum(BITS_PER_SAMPLE)
        ),
        # Audio format configuration for compatibility with ESPHome 2025.8.0+
        cv.Optional(CONF_SAMPLE_RATE, default=16000): cv.positive_int,
        cv.Optional(CONF_CHANNELS, default=1): cv.one_of(1, 2),
    }
).extend(cv.COMPONENT_SCHEMA)

def _validate_audio_format(config):
    """Add required audio format fields for ESPHome 2025.8.0+ compatibility"""
    # Add max_channels field that is required by newer ESPHome versions
    if "max_channels" not in config:
        config["max_channels"] = config.get(CONF_CHANNELS, 1)
    return config

CONFIG_SCHEMA = cv.All(
    cv.typed_schema(
        {
            "internal": BASE_SCHEMA.extend(
                {
                    cv.Required(CONF_ADC_PIN): validate_adc_pin,
                }
            ),
            "external": BASE_SCHEMA.extend(
                {
                    cv.Required(CONF_I2S_DIN_PIN): pins.internal_gpio_input_pin_number,
                    cv.Required(CONF_PDM): cv.boolean,
                }
            ),
        },
        key=CONF_ADC_TYPE,
    ),
    validate_esp32_variant,
    _validate_audio_format,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    await cg.register_parented(var, config[CONF_I2S_AUDIO_ID])
    
    # Set audio format parameters for ESPHome 2025.8.0+ compatibility  
    cg.add(var.set_sample_rate(config[CONF_SAMPLE_RATE]))
    cg.add(var.set_channels(config[CONF_CHANNELS]))
    
    # Convert bits_per_sample from enum to integer value
    bits_per_sample_val = 32 if config[CONF_BITS_PER_SAMPLE] == i2s_bits_per_sample_t.I2S_BITS_PER_SAMPLE_32BIT else 16
    cg.add(var.set_bits_per_sample_val(bits_per_sample_val))

    # if config[CONF_ADC_TYPE] == "internal":
    #     variant = esp32.get_esp32_variant()
    #     pin_num = config[CONF_ADC_PIN][CONF_NUMBER]
    #     channel = ESP32_VARIANT_ADC1_PIN_TO_CHANNEL[variant][pin_num]
    #     cg.add(var.set_adc_channel(channel))
    # else:
    #     cg.add(var.set_din_pin(config[CONF_I2S_DIN_PIN]))
    #     cg.add(var.set_pdm(config[CONF_PDM]))

    # cg.add(var.set_channel(config[CONF_CHANNEL]))
    # cg.add(var.set_bits_per_sample(config[CONF_BITS_PER_SAMPLE]))

    await microphone.register_microphone(var, config)
