#include "battery.h"

#include "keira/ksystem.h"
#include "keira/mutex.h"

BatteryCalibrationService::BatteryCalibrationService() : Service("batteryCal") {
}

void BatteryCalibrationService::run() {
    while (true) {
        // Independent of rendering, panel visibility and display sleep. One
        // existing 32-reading ADC median per second; no NVS until eligible.
        const float voltage = lilka::battery.readRawVoltage();
        if (calibration.update(voltage, millis())) {
            // SDK takes a fresh median and rejects low/tagged readings again,
            // including USB reconnected between this poll and the save.
            NVS_LOCK;
            const bool saved = lilka::battery.calibrateFullLevel();
            NVS_UNLOCK;
            if (saved) {
                lilka::serial.log("Battery full-charge reference calibrated after unplugging");
            } else {
                lilka::serial.err("Battery automatic calibration skipped or save failed");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
