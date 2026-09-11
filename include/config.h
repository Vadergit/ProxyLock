#pragma once

#include <Arduino.h>

namespace Config {

// Set this to false while experimenting if the PC must not receive any keys.
constexpr bool kUsbActionsEnabled = true;

constexpr float kPathLossExponent = 2.2F;

constexpr uint32_t kRssiSampleIntervalMs = 100;
constexpr uint32_t kNearHoldMs = 400;
constexpr uint32_t kFarHoldMs = 5000;
constexpr uint32_t kConnectionLostHoldMs = 5000;
constexpr float kRssiEmaAlpha = 0.45F;

}  // namespace Config
