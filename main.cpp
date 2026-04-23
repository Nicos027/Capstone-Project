//
//  main.cpp
//  Power Quality Analyzer
//

#include "ads131m02.hpp"
#include "signal_processing.hpp"

#include <iostream>
#include <climits>
#include <cstdint>
#include <lgpio.h>

using namespace std;

constexpr double Vref = 1.2;
constexpr double FS_Counts = 8388608.0;   // 2^23
constexpr int Sample_Rate = 4000;

constexpr double Low_Limit = 108.0;
constexpr double High_Limit = 132.0;
constexpr double Current_High_Limit = 6.5;
constexpr int Relay_Gpio = 16;

// Keep these as calibration placeholders for now
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

int main() {
    // SPI device, SPI speed, DRDY GPIO, CS2/PWM GPIO, RST GPIO
    ADS131M02 adc("/dev/spidev0.0", 1000000, 27, 17, 18);

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
        cerr << "Failed to open gpiochip for relay\n";
        adc.closeDevice();
        return 1;
    }

    // Start HIGH = relay OFF (active-low relay)
    if (lgGpioClaimOutput(gpiochip, 0, Relay_Gpio, 1) < 0) {
        cerr << "Failed to claim relay GPIO\n";
        lgGpiochipClose(gpiochip);
        adc.closeDevice();
        return 1;
    }
     const size_t cycleSamples = static_cast<size_t>(Sample_Rate / 60.0);

    RollingBuffer voltageBuffer(5 * cycleSamples);
    RollingBuffer currentBuffer(5 * cycleSamples);

    int32_t rawMin = INT32_MAX;
    int32_t rawMax = INT32_MIN;

    bool relayLatched = false;

    while (true) {
        SampleFrame frame{};
        static int readFailCount = 0;

        if (!adc.readSample(frame)) {
            ++readFailCount;

            if (readFailCount % 50 == 0) {
                cerr << "readSample still failing, count=" << readFailCount << "\n";
            }
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

        static int debugDivider = 0;
        if (++debugDivider >= 200) {
            debugDivider = 0;
            cout << "status=0x" << hex << frame.status << dec
                 << " raw=" << frame.ch0_raw
                 << " rawMin=" << rawMin
                 << " rawMax=" << rawMax
                 << " vAdc=" << vAdc
                 << " vLine=" << vLine
                 << " readFails=" << readFailCount
                 << "\n";
        }
         if (voltageBuffer.size() >= cycleSamples) {
            auto vWin = voltageBuffer.latest(cycleSamples);
            auto iWin = currentBuffer.latest(cycleSamples);

            double vrms = computeRMS(vWin);
            double irms = computeACRMS(iWin);

            string alarm = "NORMAL";
            if (vrms < Low_Limit) {
                alarm = "UNDERVOLTAGE";
            } else if (vrms > High_Limit) {
                alarm = "OVERVOLTAGE";
                if (!relayLatched) {
                    relayLatched = true;
                    lgGpioWrite(gpiochip, Relay_Gpio, 0);   // energize relay
                }
            } else if (irms > Current_High_Limit) {
                if (!relayLatched) {
                    relayLatched = true;
                    lgGpioWrite(gpiochip, Relay_Gpio, 0);   // energize relay
                }
            }

            cout << "Vrms=" << vrms
                 << " Irms=" << irms
                 << " Alarm=" << alarm
                 << "\r" << flush;
        }
    }

    lgGpioWrite(gpiochip, Relay_Gpio, 1);
    lgGpiochipClose(gpiochip);
    adc.closeDevice();
    return 0;
}
