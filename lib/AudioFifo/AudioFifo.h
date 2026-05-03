#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>

class AudioFifo {
public:
  static constexpr uint32_t kInternalSampleRateHz = 16000;
  static constexpr size_t kDefaultCapacitySamples = 4096;
  static constexpr size_t kDefaultWatermarkSamples = 1024;

  enum class State {
    Filling,
    Draining,
  };

  explicit AudioFifo(size_t capacitySamples = kDefaultCapacitySamples,
                     size_t watermarkSamples = 0)
      : capacitySamples_(capacitySamples ? capacitySamples : 1),
        watermarkSamples_(watermarkSamples),
        buffer_(new int16_t[capacitySamples_]),
        emptySinceMs_(millis()) {
    if (watermarkSamples_ > capacitySamples_) {
      watermarkSamples_ = capacitySamples_;
    }
    state_ = watermarkSamples_ == 0 ? State::Draining : State::Filling;
  }

  AudioFifo(const AudioFifo &) = delete;
  AudioFifo &operator=(const AudioFifo &) = delete;

  size_t queue(const int16_t *samples, size_t sampleCount, uint32_t sampleRateHz = kInternalSampleRateHz) {
    if (samples == nullptr || sampleCount == 0) {
      return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!queueEnabled_) {
      upsampleHasPrev_ = false;
      upsamplePrev_ = 0;
      return sampleCount;
    }

    if (sampleRateHz == 8000) {
      return queueUpsampled8kLocked(samples, sampleCount);
    }
    return queue16kLocked(samples, sampleCount);
  }

  size_t dequeueMono(int16_t *samples, size_t maxSamples) {
    if (samples == nullptr || maxSamples == 0) {
      return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!canDequeueLocked()) {
      if (usedSamples_ == 0) {
        underflowed_ = true;
      }
      return 0;
    }

    const size_t toRead = minSize(maxSamples, usedSamples_);
    readLocked(samples, toRead);
    noteEmptyLocked();
    return toRead;
  }

  size_t dequeueMonoImmediate(int16_t *samples, size_t maxSamples) {
    if (samples == nullptr || maxSamples == 0) {
      return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (usedSamples_ == 0) {
      underflowed_ = true;
      return 0;
    }

    const size_t toRead = minSize(maxSamples, usedSamples_);
    readLocked(samples, toRead);
    noteEmptyLocked();
    return toRead;
  }

  size_t dequeueStereo(int16_t *interleavedSamples, size_t maxFrames) {
    if (interleavedSamples == nullptr || maxFrames == 0) {
      return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!canDequeueLocked()) {
      if (usedSamples_ == 0) {
        underflowed_ = true;
      }
      return 0;
    }

    const size_t frames = minSize(maxFrames, usedSamples_);
    size_t first = minSize(frames, capacitySamples_ - readIndex_);
    duplicateMonoToStereo(interleavedSamples, &buffer_[readIndex_], first);

    const size_t second = frames - first;
    if (second > 0) {
      duplicateMonoToStereo(interleavedSamples + first * 2, buffer_.get(), second);
    }

    readIndex_ = (readIndex_ + frames) % capacitySamples_;
    usedSamples_ -= frames;
    noteEmptyLocked();
    return frames;
  }

  void clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    readIndex_ = 0;
    writeIndex_ = 0;
    usedSamples_ = 0;
    state_ = watermarkSamples_ == 0 ? State::Draining : State::Filling;
    emptySinceMs_ = millis();
    upsampleHasPrev_ = false;
    upsamplePrev_ = 0;
  }

  size_t availableToRead() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return usedSamples_;
  }

  size_t availableToWrite() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return capacitySamples_ - usedSamples_;
  }

  uint8_t getFillPercentagle() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<uint8_t>((usedSamples_ * 100U) / capacitySamples_);
  }

  uint8_t getFillPercentage() const {
    return getFillPercentagle();
  }

  void setQueueEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    queueEnabled_ = enabled;
    if (!queueEnabled_) {
      upsampleHasPrev_ = false;
      upsamplePrev_ = 0;
    }
  }

  bool queueEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queueEnabled_;
  }

  size_t capacity() const {
    return capacitySamples_;
  }

  size_t watermark() const {
    return watermarkSamples_;
  }

  State state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

  bool readyToDequeue() const {
    std::lock_guard<std::mutex> lock(mutex_);
    updateWatermarkLocked();
    return state_ == State::Draining && usedSamples_ > 0;
  }

  uint32_t lastFillLatencyMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastFillLatencyMs_;
  }

  bool overflowed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return overflowed_;
  }

  bool underflowed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return underflowed_;
  }

  void resetOverflowFlag() {
    std::lock_guard<std::mutex> lock(mutex_);
    overflowed_ = false;
  }

  void resetUnderflowFlag() {
    std::lock_guard<std::mutex> lock(mutex_);
    underflowed_ = false;
  }

  void resetFlowFlags() {
    std::lock_guard<std::mutex> lock(mutex_);
    overflowed_ = false;
    underflowed_ = false;
  }

private:
  static size_t minSize(size_t a, size_t b) {
    return a < b ? a : b;
  }

  static void duplicateMonoToStereo(int16_t *dst, const int16_t *src, size_t frames) {
    for (size_t i = 0; i < frames; ++i) {
      const int16_t sample = src[i];
      dst[i * 2] = sample;
      dst[i * 2 + 1] = sample;
    }
  }

  size_t queue16kLocked(const int16_t *samples, size_t sampleCount) {
    const size_t toWrite = minSize(sampleCount, capacitySamples_ - usedSamples_);
    if (toWrite < sampleCount) {
      overflowed_ = true;
    }
    if (toWrite == 0) {
      return 0;
    }

    const size_t first = minSize(toWrite, capacitySamples_ - writeIndex_);
    std::memcpy(&buffer_[writeIndex_], samples, first * sizeof(int16_t));

    const size_t second = toWrite - first;
    if (second > 0) {
      std::memcpy(buffer_.get(), samples + first, second * sizeof(int16_t));
    }

    writeIndex_ = (writeIndex_ + toWrite) % capacitySamples_;
    usedSamples_ += toWrite;
    upsampleHasPrev_ = false;
    upsamplePrev_ = 0;
    updateWatermarkLocked();
    return toWrite;
  }

  size_t queueUpsampled8kLocked(const int16_t *samples, size_t sampleCount) {
    const size_t inputToWrite = minSize(sampleCount, (capacitySamples_ - usedSamples_) / 2);
    if (inputToWrite < sampleCount) {
      overflowed_ = true;
    }
    int32_t prev = upsamplePrev_;
    bool hasPrev = upsampleHasPrev_;

    for (size_t i = 0; i < inputToWrite; ++i) {
      const int32_t sample = samples[i];
      buffer_[writeIndex_] = hasPrev ? static_cast<int16_t>((prev + sample) >> 1) : static_cast<int16_t>(sample);
      writeIndex_ = (writeIndex_ + 1) % capacitySamples_;
      buffer_[writeIndex_] = static_cast<int16_t>(sample);
      writeIndex_ = (writeIndex_ + 1) % capacitySamples_;
      prev = sample;
      hasPrev = true;
    }

    usedSamples_ += inputToWrite * 2;
    upsamplePrev_ = static_cast<int16_t>(prev);
    upsampleHasPrev_ = hasPrev;
    updateWatermarkLocked();
    return inputToWrite;
  }

  void readLocked(int16_t *samples, size_t sampleCount) {
    const size_t first = minSize(sampleCount, capacitySamples_ - readIndex_);
    std::memcpy(samples, &buffer_[readIndex_], first * sizeof(int16_t));

    const size_t second = sampleCount - first;
    if (second > 0) {
      std::memcpy(samples + first, buffer_.get(), second * sizeof(int16_t));
    }

    readIndex_ = (readIndex_ + sampleCount) % capacitySamples_;
    usedSamples_ -= sampleCount;
  }

  bool canDequeueLocked() {
    updateWatermarkLocked();
    return state_ == State::Draining && usedSamples_ > 0;
  }

  void updateWatermarkLocked() const {
    if (state_ == State::Filling && usedSamples_ >= watermarkSamples_) {
      state_ = State::Draining;
      lastFillLatencyMs_ = millis() - emptySinceMs_;
    }
  }

  void noteEmptyLocked() {
    if (usedSamples_ != 0) {
      return;
    }

    emptySinceMs_ = millis();
    state_ = watermarkSamples_ == 0 ? State::Draining : State::Filling;
  }

  const size_t capacitySamples_;
  size_t watermarkSamples_;
  std::unique_ptr<int16_t[]> buffer_;
  size_t readIndex_ = 0;
  size_t writeIndex_ = 0;
  size_t usedSamples_ = 0;
  bool upsampleHasPrev_ = false;
  int16_t upsamplePrev_ = 0;
  bool queueEnabled_ = true;
  bool overflowed_ = false;
  bool underflowed_ = false;
  mutable State state_ = State::Filling;
  uint32_t emptySinceMs_ = 0;
  mutable uint32_t lastFillLatencyMs_ = 0;
  mutable std::mutex mutex_;
};
