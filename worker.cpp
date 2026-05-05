#include "worker.h"
#include "signal_processing.hpp"
#include "config.h"
#include "ads131m02.hpp"

#include <QThread>
#include <QtMath>
#include <QDebug>
#include <climits>
#include <lgpio.h>

// ADC hardware constants (do NOT edit per device — see config.h for calibration)
static constexpr double Vref        = 1.2;
static constexpr double FS_Counts   = 8388608.0;   // 2^23 (24-bit signed)
static constexpr int    Sample_Rate = 4000;

static double rawToAdcVolts(int32_t raw, int gain = 1) {
    return (static_cast<double>(raw) / FS_Counts) * (Vref / gain);
}

static double adcToLineVolts(double adcVolts) {
    return adcVolts * VoltWatchConfig::Volts_Per_Adc_Volt;
}

static double adcToLineAmps(double adcVolts) {
    return adcVolts * VoltWatchConfig::Amps_Per_Adc_Volt;
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

    const size_t cycleSamples = static_cast<size_t>(Sample_Rate / 60.0);
    RollingBuffer voltageBuffer(5 * cycleSamples);
    RollingBuffer currentBuffer(5 * cycleSamples);

    QString lastAlarmState = "NORMAL";
    bool relayLatched = false;
    QString latchedFault = "NORMAL";

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

    int gpiochip = lgGpiochipOpen(0);
    if (gpiochip < 0) {
        emit errorMessage("Failed to open gpiochip for relay");
        adc.closeDevice();
        emit finished();
        return;
    }
    if (lgGpioClaimOutput(gpiochip, 0, RELAY_GPIO, 1) < 0) {
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

        if (voltageBuffer.size() >= cycleSamples && ++emitDivider >= 60) {
            emitDivider = 0;

            auto vWin = voltageBuffer.latest(cycleSamples);
            auto iWin = currentBuffer.latest(cycleSamples);

            double vrms = computeRMS(vWin);
            double irms = computeACRMS(iWin);
            double realPower     = computeMeanProduct(vWin, iWin);
            double apparentPower = computeApparentPower(vrms, irms);
            double powerFactor   = computePowerFactor(realPower, apparentPower);

            QString alarm = "NORMAL";

            if (relayLatched) {
                alarm = latchedFault;
                lgGpioWrite(gpiochip, RELAY_GPIO, 1); // keep relay open
            } else if (vrms > HIGH_VOLT_LIMIT) {
                alarm = "OVERVOLTAGE";
                relayLatched = true;
                latchedFault = "OVERVOLTAGE";
                lgGpioWrite(gpiochip, RELAY_GPIO, 1); // open relay
            } else if (vrms < LOW_VOLT_LIMIT) {
                alarm = "UNDERVOLTAGE";
                lgGpioWrite(gpiochip, RELAY_GPIO, 0); // keep relay closed
            } else {
                alarm = "NORMAL";
                lgGpioWrite(gpiochip, RELAY_GPIO, 0); // keep relay closed
            }

            auto vWaveSamples = voltageBuffer.latest(5 * cycleSamples);
            auto iWaveSamples = currentBuffer.latest(5 * cycleSamples);
            QVector<double> vWave(vWaveSamples.begin(), vWaveSamples.end());
            QVector<double> iWave(iWaveSamples.begin(), iWaveSamples.end());

            emit newReadings(vrms, irms, realPower, apparentPower, powerFactor, alarm);
            emit newWaveform(vWave, iWave);

            if (alarm != "NORMAL" && alarm != lastAlarmState) {
                emit alarmTriggered(alarm, vrms, irms);
            }
            lastAlarmState = alarm;
        }
    }

    if (!relayLatched) {
        lgGpioWrite(gpiochip, RELAY_GPIO, 0);  // de-energize relay (load powered)
    }

    lgGpiochipClose(gpiochip);
    adc.closeDevice();

    emit finished();
}
