#include <Arduino.h>
#include <math.h>

#define MIC_MODE_I2S 1
#define MIC_MODE_ANALOG 2

#ifndef MIC_INPUT_MODE
#define MIC_INPUT_MODE MIC_MODE_I2S
#endif

#ifndef MIC_SAMPLE_RATE
#define MIC_SAMPLE_RATE 16000
#endif

#ifndef MIC_I2S_BCLK
#define MIC_I2S_BCLK 5
#endif

#ifndef MIC_I2S_WS
#define MIC_I2S_WS 4
#endif

#ifndef MIC_I2S_DATA
#define MIC_I2S_DATA 6
#endif

#ifndef MIC_I2S_LEFT_CHANNEL
#define MIC_I2S_LEFT_CHANNEL 1
#endif

#ifndef MIC_ADC_PIN
#define MIC_ADC_PIN 1
#endif

#ifndef MIC_WARMUP_BLOCKS
#define MIC_WARMUP_BLOCKS 10
#endif

#if MIC_INPUT_MODE == MIC_MODE_I2S
#include "driver/i2s.h"

static const i2s_port_t kI2sPort = I2S_NUM_0;
static const size_t kI2sSampleCount = 512;
static int32_t i2sSamples[kI2sSampleCount];

static void setupI2sMic() {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_RX);
  config.sample_rate = MIC_SAMPLE_RATE;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  config.channel_format = MIC_I2S_LEFT_CHANNEL ? I2S_CHANNEL_FMT_ONLY_LEFT : I2S_CHANNEL_FMT_ONLY_RIGHT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 8;
  config.dma_buf_len = 256;
  config.use_apll = false;
  config.tx_desc_auto_clear = false;
  config.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = MIC_I2S_BCLK;
  pins.ws_io_num = MIC_I2S_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = MIC_I2S_DATA;

  ESP_ERROR_CHECK(i2s_driver_install(kI2sPort, &config, 0, nullptr));
  ESP_ERROR_CHECK(i2s_set_pin(kI2sPort, &pins));
  ESP_ERROR_CHECK(i2s_zero_dma_buffer(kI2sPort));
}

static bool readAudioBlock(int32_t *samples, size_t maxSamples, size_t *sampleCount) {
  size_t bytesRead = 0;
  esp_err_t result = i2s_read(kI2sPort, samples, maxSamples * sizeof(samples[0]), &bytesRead, pdMS_TO_TICKS(1000));
  *sampleCount = bytesRead / sizeof(samples[0]);
  return result == ESP_OK && *sampleCount > 0;
}

static int32_t normalizeSample(int32_t sample) {
  return sample >> 8;
}
#else
static const size_t kAnalogSampleCount = 512;
static int32_t analogSamples[kAnalogSampleCount];

static void setupAnalogMic() {
  analogReadResolution(12);
  analogSetPinAttenuation(MIC_ADC_PIN, ADC_11db);
}

static bool readAudioBlock(int32_t *samples, size_t maxSamples, size_t *sampleCount) {
  const uint32_t intervalUs = 1000000UL / MIC_SAMPLE_RATE;
  uint32_t nextSampleAt = micros();

  for (size_t i = 0; i < maxSamples; ++i) {
    while (static_cast<int32_t>(micros() - nextSampleAt) < 0) {
      delayMicroseconds(5);
    }
    samples[i] = analogRead(MIC_ADC_PIN);
    nextSampleAt += intervalUs;
  }

  *sampleCount = maxSamples;
  return true;
}

static int32_t normalizeSample(int32_t sample) {
  return sample;
}
#endif

struct AudioStats {
  int32_t mean;
  float rms;
  int32_t peak;
  float dbfs;
};

static AudioStats calculateAudioStats(const int32_t *samples, size_t sampleCount) {
  AudioStats stats = {};

  int64_t sum = 0;
  for (size_t i = 0; i < sampleCount; ++i) {
    sum += normalizeSample(samples[i]);
  }

  stats.mean = static_cast<int32_t>(sum / static_cast<int64_t>(sampleCount));
  int64_t squareSum = 0;

  for (size_t i = 0; i < sampleCount; ++i) {
    const int32_t centered = normalizeSample(samples[i]) - stats.mean;
    const int32_t magnitude = abs(centered);
    if (magnitude > stats.peak) {
      stats.peak = magnitude;
    }
    squareSum += static_cast<int64_t>(centered) * centered;
  }

  stats.rms = sqrtf(static_cast<float>(squareSum) / static_cast<float>(sampleCount));
#if MIC_INPUT_MODE == MIC_MODE_I2S
  const float fullScale = 8388608.0f;
#else
  const float fullScale = 2048.0f;
#endif
  stats.dbfs = stats.rms > 1.0f ? 20.0f * log10f(stats.rms / fullScale) : -120.0f;
  return stats;
}

static void printVadStats(const int32_t *samples, size_t sampleCount) {
  if (sampleCount == 0) {
    Serial.println("vad: no samples");
    return;
  }

  static bool noiseReady = false;
  static float noiseFloorDbfs = -70.0f;
  static uint32_t voiceBlocks = 0;
  static uint32_t noiseBlocks = 0;
  static uint32_t warmupBlocks = 0;

  const AudioStats stats = calculateAudioStats(samples, sampleCount);

  if (warmupBlocks < MIC_WARMUP_BLOCKS) {
    ++warmupBlocks;
    Serial.printf(
        "vad: warmup=%lu/%d dbfs=%.1f peak=%ld\n",
        static_cast<unsigned long>(warmupBlocks),
        MIC_WARMUP_BLOCKS,
        stats.dbfs,
        static_cast<long>(stats.peak));
    return;
  }

  if (!noiseReady) {
    noiseFloorDbfs = stats.dbfs;
    noiseReady = true;
  }

  const float marginDb = stats.dbfs - noiseFloorDbfs;
  const bool flat = stats.peak < 20;
  const bool clippingRisk = stats.dbfs > -8.0f;
  const bool voice = !flat && !clippingRisk && marginDb >= 10.0f;

  if (!voice && !clippingRisk && stats.dbfs > -100.0f) {
    const float alpha = stats.dbfs < noiseFloorDbfs ? 0.10f : 0.02f;
    noiseFloorDbfs = (1.0f - alpha) * noiseFloorDbfs + alpha * stats.dbfs;
  }

  if (voice) {
    ++voiceBlocks;
  } else {
    ++noiseBlocks;
  }

  const char *state = flat ? "flat" : (clippingRisk ? "clipping-risk" : (voice ? "voice" : "noise"));

  Serial.printf(
      "vad: samples=%u mean=%ld rms=%.1f peak=%ld dbfs=%.1f noise=%.1f margin=%.1f state=%s voice=%lu noise_blocks=%lu\n",
      static_cast<unsigned>(sampleCount),
      static_cast<long>(stats.mean),
      stats.rms,
      static_cast<long>(stats.peak),
      stats.dbfs,
      noiseFloorDbfs,
      marginDb,
      state,
      static_cast<unsigned long>(voiceBlocks),
      static_cast<unsigned long>(noiseBlocks));
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("ESP32-S3 microphone reachability test");
  Serial.printf("sample_rate=%d Hz\n", MIC_SAMPLE_RATE);

#if MIC_INPUT_MODE == MIC_MODE_I2S
  Serial.printf("mode=I2S bclk=%d ws=%d data=%d channel=%s\n",
                MIC_I2S_BCLK,
                MIC_I2S_WS,
                MIC_I2S_DATA,
                MIC_I2S_LEFT_CHANNEL ? "left" : "right");
  setupI2sMic();
#else
  Serial.printf("mode=analog adc_pin=%d\n", MIC_ADC_PIN);
  setupAnalogMic();
#endif
}

void loop() {
#if MIC_INPUT_MODE == MIC_MODE_I2S
  size_t sampleCount = 0;
  if (readAudioBlock(i2sSamples, kI2sSampleCount, &sampleCount)) {
    printVadStats(i2sSamples, sampleCount);
  } else {
    Serial.println("audio: i2s read timeout");
  }
#else
  size_t sampleCount = 0;
  if (readAudioBlock(analogSamples, kAnalogSampleCount, &sampleCount)) {
    printVadStats(analogSamples, sampleCount);
  }
#endif

  delay(300);
}
