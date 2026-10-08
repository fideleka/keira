#pragma once

#include <cmath>
#include <stdint.h>

// Only enable on the 33k/10k optocoupler ADC-tag modification.
// Input is SDK readRawVoltage(): normal-divider reconstructed volts, NOT
// actual GPIO3 volts and NOT calibrated/estimated battery percentage.
namespace keira {
enum class ChargeStatus { Unknown, Battery, Charging, Charged };

class ChargeStatusFilter {
public:
    static ChargeStatus classify(float voltage) {
        // Guard bands around provisional boundaries suppress noisy transitions.
        if (!std::isfinite(voltage) || voltage < 0.5f || voltage > 4.6f) return ChargeStatus::Unknown;
        if (voltage < 1.40f) return ChargeStatus::Charged;
        if (voltage > 1.50f && voltage < 2.65f) return ChargeStatus::Charging;
        if (voltage > 2.80f) return ChargeStatus::Battery;
        return ChargeStatus::Unknown;
    }

    ChargeStatus update(float voltage) {
        ChargeStatus next = classify(voltage);
        if (next == ChargeStatus::Unknown) {
            reset();
            return next;
        }
        if (next != candidate) {
            candidate = next;
            count = 1;
        } else if (count < 3) {
            ++count;
        }
        // Hide stale battery percentages immediately during plug/unplug;
        // publish the new state after three consistent one-second samples.
        return count >= 3 ? candidate : ChargeStatus::Unknown;
    }

    void reset() {
        candidate = ChargeStatus::Unknown;
        count = 0;
    }

private:
    ChargeStatus candidate = ChargeStatus::Unknown;
    uint8_t count = 0;
};
} // namespace keira
