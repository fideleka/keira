#pragma once

#include "keira/service.h"
#include "auto_full_calibration.h"

class BatteryCalibrationService : public Service {
public:
    BatteryCalibrationService();

private:
    void run() override;
    keira::AutoFullCalibration calibration;
};
