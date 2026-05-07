#include "worker.h"
#include "signal_processing.hpp"
#include "config.h"
#include "ads131m02.hpp"

#include <QThread>
#include <QtMath>
#include <QDebug>
#include <QDateTime>
#include <climits>
#include <vector>
#include <lgpio.h>

// ADC hardware constants (do NOT edit per device — see config.h for calibration)
static constexpr double Vref        = 1.2;
static constexpr double FS_Counts   = 8388608.0;   // 2^23 (24-bit signed)
static constexpr int    Sample_Rate = 4000;

// Window settings
static constexpr size_t DISPLAY_CYCLES = 3;
static constexpr size_t CONTROL_CYCLES = 5;

static double rawToAdcVolts(int32_t raw, int gain = 1) {
    return (static_cast<double>(raw) / FS_Counts) * (Vref / gain);
}

static double adcToLineVolts(double adcVolts) {
    return adcVolts * VoltWatchConfig::Volts_Per_Adc_Volt;
}

static double adcToLineAmps(double adcVolts) {
    return adcVolts * VoltWatchConfig::Amps_Per_Adc_Volt;
}

// Find first rising zero crossing so the displayed waveform looks stable
static size_t findRisingZeroCrossing(const std::vector<double>& x) {
    if (x.size() < 2) return 0;

    for (size_t i = 1; i < x.size(); ++i) {
        if (x[i - 1] < 0.0 && x[i] >= 0.0) {
            return i;
        }
    }

    return 0;
}

// Reorder a waveform so plotting starts at the trigger index
static std::vector<double> makeTriggeredWindow(const std::vector<double>& x, size_t triggerIndex) {
    if (x.empty()) return {};

    std::vector<double> y;
    y.reserve(x.size());

    for (size_t k = 0; k < x.size(); ++k) {
        size_t idx = (triggerIndex + k) % x.size();
        y.push_back(x[idx]);
    }

    return y;
}

Worker::Worker(QObject *parent)
    : QObject(parent), running_(false) {}

Worker::~Worker() {
    stop();
}

void Worker::stop() {
    running_ = false;
}

void Worker::run() {
    using namespace VoltWatchConfig;
    running_ = true;

    const size_t cycleSamples   = static_cast<size_t>(Sample_Rate / 60.0);
    const size_t displaySamples = DISPLAY_CYCLES * cycleSamples;
    const size_t controlSamples = CONTROL_CYCLES * cycleSamples;
    const size_t bufferSamples  = 5 * controlSamples;   // plenty of history for triggering + control

    RollingBuffer voltageBuffer(bufferSamples);
    RollingBuffer currentBuffer(bufferSamples);

    QString lastAlarmState = "NORMAL";
    bool relayLatched = false;
    QString latchedFault = "NORMAL";
    QDateTime relayLatchTime;

    int overVoltageCount = 0;
    int underVoltageTripCount = 0;
    int underVoltageWarnCount = 0;

    ADS131M02 adc("/dev/spidev0.0", 1000000, 27, 17, 18);

    if (!adc.openDevice()) {
        emit errorMessage("Failed to open ADC");
        emit finished();
        return;
    }
    if (!adc.configure()) {
        emit errorMessage("ADC configuration failed");
        adc.closeDevice();
        emit finished();
        return;
    }

    bool adcResponded = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        SampleFrame probe{};
        if (adc.readSample(probe)) {
            adcResponded = true;
            break;
        }
    }
    if (!adcResponded) {
        emit errorMessage("ADC is not responding. Check that the ADS131M02 board is connected and powered.");
        adc.closeDevice();
        emit finished();
        return;
    }

    // Relay wiring in your setup:
    // GPIO = 0 -> relay closed / load powered
    // GPIO = 1 -> relay open   / load disconnected
    int gpiochip = lgGpiochipOpen(0);
    if (gpiochip < 0) {
        emit errorMessage("Failed to open gpiochip for relay");
        adc.closeDevice();
        emit finished();
        return;
    }
    if (lgGpioClaimOutput(gpiochip, 0, RELAY_GPIO, 0) < 0) {
        emit errorMessage("Failed to claim relay GPIO");
        lgGpiochipClose(gpiochip);
        adc.closeDevice();
        emit finished();
        return;
    }

    int emitDivider = 0;

    while (running_) {
        SampleFrame frame{};
        if (!adc.readSample(frame)) continue;

        double vAdc  = rawToAdcVolts(frame.ch0_raw, 1);
        double iAdc  = rawToAdcVolts(frame.ch1_raw, 1);
        double vLine = adcToLineVolts(vAdc);
        double iLine = adcToLineAmps(iAdc);

        voltageBuffer.push(vLine);
        currentBuffer.push(iLine);

        // Wait until we have enough samples for the control window
        if (voltageBuffer.size() < controlSamples) {
            continue;
        }

        // Throttle UI/control updates
        if (++emitDivider < 60) {
            continue;
        }
        emitDivider = 0;

        // --- Control / alarm window (more stable, 5 cycles) ---
        auto vControlWin = voltageBuffer.latest(controlSamples);
        auto iControlWin = currentBuffer.latest(controlSamples);

        double vrms = computeRMS(vControlWin);
        double irms = computeACRMS(iControlWin);
        double realPower     = computeMeanProductAC(vControlWin, iControlWin);
        double apparentPower = computeApparentPower(vrms, irms);
        double powerFactor   = computePowerFactor(realPower, apparentPower);

        if (irms < 0.03) {
    irms = 0.0;
}

if (qAbs(realPower) < 2.0) {
    realPower = 0.0;
}

if (apparentPower < 2.0) {
    apparentPower = 0.0;
}

if (apparentPower == 0.0) {
    powerFactor = 0.0;
}

        QString alarm = "NORMAL";

        if (relayLatched && relayLatchTime.isValid()) {
    if (relayLatchTime.secsTo(QDateTime::currentDateTime()) >= 15) {
        relayLatched = false;
        latchedFault = "NORMAL";
        relayLatchTime = QDateTime();
        overVoltageCount = 0;
        underVoltageTripCount = 0;
        underVoltageWarnCount = 0;
    }
}

        if (relayLatched) {
            alarm = latchedFault;
            lgGpioWrite(gpiochip, RELAY_GPIO, 1); // keep relay open
        } else {
            if (vrms > HIGH_VOLT_LIMIT) {
                overVoltageCount++;
            } else {
                overVoltageCount = 0;
            }

            if (vrms < TRIP_LOW_VOLT_LIMIT) {
                underVoltageTripCount++;
            } else {
                underVoltageTripCount = 0;
            }

            if (vrms < LOW_VOLT_LIMIT && vrms >= TRIP_LOW_VOLT_LIMIT) {
                underVoltageWarnCount++;
            } else {
                underVoltageWarnCount = 0;
            }

            if (underVoltageTripCount >= 5) {
                alarm = "UNDERVOLTAGE_TRIP";
                relayLatched = true;
                latchedFault = "UNDERVOLTAGE_TRIP";
                relayLatchTime = QDateTime::currentDateTime();
                lgGpioWrite(gpiochip, RELAY_GPIO, 1); // open relay
            } else if (overVoltageCount >= 5) {
                alarm = "OVERVOLTAGE";
                relayLatched = true;
                latchedFault = "OVERVOLTAGE";
                relayLatchTime = QDateTime::currentDateTime();
                lgGpioWrite(gpiochip, RELAY_GPIO, 1); // open relay
            } else if (underVoltageWarnCount >= 2) {
                alarm = "UNDERVOLTAGE_WARN";
                lgGpioWrite(gpiochip, RELAY_GPIO, 0); // keep relay closed
            } else {
                alarm = "NORMAL";
                lgGpioWrite(gpiochip, RELAY_GPIO, 0); // keep relay closed
            }
        }

        // --- Display window (faster looking, 3 cycles, triggered) ---
        auto vDisplayWin = voltageBuffer.latest(displaySamples);
        auto iDisplayWin = currentBuffer.latest(displaySamples);

        size_t triggerIndex = findRisingZeroCrossing(vDisplayWin);

        auto vTriggered = makeTriggeredWindow(vDisplayWin, triggerIndex);
        auto iTriggered = makeTriggeredWindow(iDisplayWin, triggerIndex);

        QVector<double> vWave(vTriggered.begin(), vTriggered.end());
        QVector<double> iWave(iTriggered.begin(), iTriggered.end());

        emit newReadings(vrms, irms, realPower, apparentPower, powerFactor, alarm);
        emit newWaveform(vWave, iWave);

        if (alarm != "NORMAL" && alarm != lastAlarmState) {
            emit alarmTriggered(alarm, vrms, irms);
        }
        lastAlarmState = alarm;
    }

    if (!relayLatched) {
        lgGpioWrite(gpiochip, RELAY_GPIO, 0); // keep load powered
    }

    lgGpiochipClose(gpiochip);
    adc.closeDevice();

    emit finished();
}
