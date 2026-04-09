//
//  main.cpp
//  Power Quality Analyzer
//

#include "ads131m02.hpp"          
#include "signal_processing.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <climits>
#include <cstdint>
#include <lgpio.h>

using namespace std;

constexpr double Vref = 1.2;
constexpr double FS_Counts = 8388608.0;   // 2^23
constexpr int Sample_Rate = 4000;

constexpr double Nominal_Vrms = 120.0;
constexpr double Low_Limit = 108.0;
constexpr double High_Limit = 132.0;

constexpr int DRDY_GPIO = 27;
constexpr int DRDY_Timeout_us = 200000;

// Replace with calibrated values later
constexpr double Volts_Per_Adc_Volt = 195.4;
constexpr double Amps_Per_Adc_Volt  = 60.0;

double rawToAdcVolts(int32_t raw, int gain = 1) {
    return (static_cast<double>(raw) / FS_Counts) * (Vref / gain);
}

double adcToLineVolts(double adcVolts) {
    return adcVolts * Volts_Per_Adc_Volt;
}

double adcToLineAmps(double adcVolts) {
    return adcVolts * Amps_Per_Adc_Volt;
}

// Wait for a NEW DRDY pulse: high first, then low
bool waitForDrdyTransition(int gpiochip, int pin, int timeout_us = 200000) {
    auto start = chrono::steady_clock::now();

    int level = lgGpioRead(gpiochip, pin);
    if (level < 0) {
        return false;
    }

    // If DRDY is already low, wait for it to release high first
    while (level == 0) {
        auto elapsed = chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now() - start
        ).count();

        if (elapsed > timeout_us) {
            return false;
        }

        this_thread::sleep_for(chrono::microseconds(2));
        level = lgGpioRead(gpiochip, pin);
        if (level < 0) return false;
    }

    // Now wait for the next asserted-low event
    while (level == 1) {
        auto elapsed = chrono::duration_cast<chrono::microseconds>(
            chrono::steady_clock::now() - start
        ).count();

        if (elapsed > timeout_us) {
            return false;
        }

        this_thread::sleep_for(chrono::microseconds(2));
        level = lgGpioRead(gpiochip, pin);
        if (level < 0) return false;
    }

    return true;
}

int main() {
    ADS131M02 adc("/dev/spidev0.0", 1000000);

    if (!adc.openDevice()) {
        cerr << "Failed to open ADC\n";
        return 1;
    }

    if (!adc.configure()) {
        cerr << "ADC configuration failed\n";
        return 1;
    }

    int gpiochip = lgGpiochipOpen(0);
    if (gpiochip < 0) {
        cerr << "Failed to open gpiochip\n";
        return 1;
    }

    if (lgGpioClaimInput(gpiochip, 0, DRDY_GPIO) < 0) {
        cerr << "Failed to claim GPIO27 as input\n";
        lgGpiochipClose(gpiochip);
        return 1;
    }

    const size_t cycleSamples = static_cast<size_t>(Sample_Rate / 60.0);

    RollingBuffer voltageBuffer(5 * cycleSamples);
    RollingBuffer currentBuffer(5 * cycleSamples);

    int32_t rawMin = INT32_MAX;
    int32_t rawMax = INT32_MIN;
    int printDivider = 0;

    while (true) {
        if (!waitForDrdyTransition(gpiochip, DRDY_GPIO, DRDY_Timeout_us)) {
    continue;
        }
        
        SampleFrame frame{};
        if (!adc.readSample(frame)) {
            cerr << "Read failed\n";
            continue;
        }

        double vAdc  = rawToAdcVolts(frame.ch0_raw, 1);
        double iAdc  = rawToAdcVolts(frame.ch1_raw, 1);

        double vLine = adcToLineVolts(vAdc);
        double iLine = adcToLineAmps(iAdc);

        voltageBuffer.push(vLine);
        currentBuffer.push(iLine);

        if (frame.ch0_raw < rawMin) rawMin = frame.ch0_raw;
        if (frame.ch0_raw > rawMax) rawMax = frame.ch0_raw;

        // Print debug every 50 samples so the console doesn't get hammered
        static int debugDivider = 0;

if (++debugDivider >= 200) {
    debugDivider = 0;

    cout << "raw=" << frame.ch0_raw
         << " rawMin=" << rawMin
         << " rawMax=" << rawMax
         << " vAdc=" << vAdc
         << " vLine=" << vLine
         << "\n";
    }

        if (voltageBuffer.size() >= cycleSamples) {
            auto vWin = voltageBuffer.latest(cycleSamples);
            auto iWin = currentBuffer.latest(cycleSamples);

            double vrms = computeRMS(vWin);
            double irms = computeRMS(iWin);

            string alarm = "NORMAL";
            if (vrms < Low_Limit) {
                alarm = "UNDERVOLTAGE";
            } else if (vrms > High_Limit) {
                alarm = "OVERVOLTAGE";
            }

            cout << "Vrms=" << vrms
                 << " Irms=" << irms
                 << " Alarm=" << alarm
                 << "\r" << flush;
        }
    }

    // unreachable in current loop, but fine to leave here
    lgGpiochipClose(gpiochip);
    adc.closeDevice();
    return 0;
}
