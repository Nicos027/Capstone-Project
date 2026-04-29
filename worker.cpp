#include "worker.h"
#include "signal_processing.hpp"
#include "config.h"

#include <QThread>
#include <QtMath>
#include <QDebug>
#include <climits>

#ifdef USE_REAL_ADC
#include "ads131m02.hpp"
#include <lgpio.h>
#endif

static constexpr double Vref = 1.2;
static constexpr double FS_Counts = 8388608.0;
static constexpr int    Sample_Rate = 4000;

static constexpr double Volts_Per_Adc_Volt = 184.0;
static constexpr double Amps_Per_Adc_Volt  = 61.0;

static double rawToAdcVolts(int32_t raw, int gain = 1) {
    return (static_cast<double>(raw) / FS_Counts) * (Vref / gain);
}

static double adcToLineVolts(double adcVolts) {
    return adcVolts * Volts_Per_Adc_Volt;
}

static double adcToLineAmps(double adcVolts) {
    return adcVolts * Amps_Per_Adc_Volt;
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

#ifdef USE_REAL_ADC
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

    // Verify ADC is actually responding — attempt up to 10 sample reads
    // within ~2 seconds. If none succeed, the chip is either not wired up
    // or not powered, and we should notify the user.
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

    // Open gpiochip and claim the relay pin (start HIGH = relay OFF, active-low)
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

            // Determine alarm state with voltage OR current thresholds.
            // Overcurrent and overvoltage both latch the relay for protection.
            QString alarm = "NORMAL";
            if (vrms < LOW_VOLT_LIMIT) {
                alarm = "UNDERVOLTAGE";
            } else if (vrms > HIGH_VOLT_LIMIT) {
                alarm = "OVERVOLTAGE";
                if (!relayLatched) {
                    relayLatched = true;
                    lgGpioWrite(gpiochip, RELAY_GPIO, 0);  // energize relay
                }
            } else if (irms > HIGH_CURRENT_LIMIT) {
                alarm = "OVERCURRENT";
                if (!relayLatched) {
                    relayLatched = true;
                    lgGpioWrite(gpiochip, RELAY_GPIO, 0);  // energize relay
                }
            }

            auto vWaveSamples = voltageBuffer.latest(5 * cycleSamples);
            auto iWaveSamples = currentBuffer.latest(5 * cycleSamples);

            QVector<double> vWave = QVector<double>(vWaveSamples.begin(), vWaveSamples.end());
            QVector<double> iWave = QVector<double>(iWaveSamples.begin(), iWaveSamples.end());

            emit newReadings(vrms, irms, alarm);
            emit newWaveform(vWave, iWave);

            if (alarm != "NORMAL" && alarm != lastAlarmState) {
                emit alarmTriggered(alarm, vrms, irms);
            }
            lastAlarmState = alarm;
        }
    }

    // On shutdown, leave the relay in its current state (if latched, keep it
    // latched so protection stays engaged until someone resets the system).
    // Only return to "off" if no fault was detected.
    if (!relayLatched) {
        lgGpioWrite(gpiochip, RELAY_GPIO, 1);
    }
    lgGpiochipClose(gpiochip);
    adc.closeDevice();

#else
    // Desktop fake-data path for development on PC (no hardware)
    const double twoPi = 6.283185307179586;
    const double vPeak = 120.0 * 1.41421356;
    const double iPeak = 4.0  * 1.41421356;
    double phase = 0.0;
    int emitDivider = 0;

    while (running_) {
        for (int i = 0; i < 67; ++i) {
            double v = vPeak * std::sin(phase);
            double iVal = iPeak * std::sin(phase - 0.15);
            voltageBuffer.push(v);
            currentBuffer.push(iVal);
            phase += twoPi / 67.0;
            if (phase > twoPi) phase -= twoPi;
        }

        QThread::msleep(16);

        if (voltageBuffer.size() >= cycleSamples && ++emitDivider >= 3) {
            emitDivider = 0;

            auto vWin = voltageBuffer.latest(cycleSamples);
auto iWin = currentBuffer.latest(cycleSamples);

double vrms = computeRMS(vWin);          // or computeACRMS(vWin) if needed
double irms = computeACRMS(iWin);

double realPower = computeMeanProduct(vWinCentered, iWinCentered);
double apparentPower = computeApparentPower(vrms, irms);
double powerFactor = computePowerFactor(realPower, apparentPower);

QString alarm = "NORMAL";

if (vrms < LOW_VOLT_LIMIT) {
    alarm = "UNDERVOLTAGE";
} else if (vrms > HIGH_VOLT_LIMIT) {
    alarm = "OVERVOLTAGE";
} else if (irms > HIGH_CURRENT_LIMIT) {
    alarm = "OVERCURRENT";
}

auto vWaveSamples = voltageBuffer.latest(5 * cycleSamples);
auto iWaveSamples = currentBuffer.latest(5 * cycleSamples);

QVector<double> vWave = QVector<double>(vWaveSamples.begin(), vWaveSamples.end());
QVector<double> iWave = QVector<double>(iWaveSamples.begin(), iWaveSamples.end());

emit newReadings(vrms, irms, realPower, apparentPower, powerFactor, alarm);
emit newWaveform(vWave, iWave);

if (alarm != "NORMAL" && alarm != lastAlarmState) {
    emit alarmTriggered(alarm, vrms, irms);
}

lastAlarmState = alarm;
        }
    }
#endif

    emit finished();
}
