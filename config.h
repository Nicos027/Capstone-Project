// config.h
// =======================================================
// Per-device configuration
//
//   1. Scroll down and edit your device number
//   2. DEVICE_NUM        -> your Pi number (01, 02, or 03)
//   3. MQTT_PASSWORD     -> the HiveMQ password in SOP
//   4. CT calibration    -> uncomment ONE Amps_Per_Adc_Volt line
//      to match your current sensor (20 A or 100 A CT)
//
// Everything else is shared and should NOT be changed.
// =======================================================
#pragma once
#include <QString>

namespace VoltWatchConfig {

    // ========== EDIT THESE LINES ==========
    //
    // 1. DEVICE_NUM: "01" for Nicos, "02" for Rossana, "03" for Jose
    //    Keep the quotes. Must be two digits with leading zero.
    static const QString DEVICE_NUM = "01";

    // 2. MQTT_PASSWORD: listed in SOP
    static const QString MQTT_PASSWORD = "PASTE-YOUR-PASSWORD-HERE";

    // 3. CT calibration constants. Uncomment EXACTLY ONE pair below.
    //    Voltage path is the same for everyone (same transformer + divider).
    //    Current path differs by which CT model is plugged in.
    //
    //    Tune these against a known load + reference meter before final demo.

    // ---- Production hardware (20 A CT, ACUCT-H040-20:333mV) ----
    static constexpr double Volts_Per_Adc_Volt = 184.0;
    static constexpr double Amps_Per_Adc_Volt  = 61.0;

    // ---- Development with 100 A CT (Rossana, until 20 A arrives) ----
    // static constexpr double Volts_Per_Adc_Volt = 184.0;
    // static constexpr double Amps_Per_Adc_Volt  = 305.0;
    //
    // ==========================================

    // ---- Shared settings below (do not edit) ----
    static const QString MQTT_BROKER   = "a579eade5a7f40ca8f8ad5365379f4d3.s1.eu.hivemq.cloud";
    static const int     MQTT_PORT     = 8883;  // TLS port
    // HiveMQ credential username, e.g. "VoltWatch_pi01"
    static const QString MQTT_USERNAME = "VoltWatch_pi" + DEVICE_NUM;
    // MQTT topic prefix, e.g. "voltwatch/pi_01"
    static const QString DEVICE_ID     = "pi_" + DEVICE_NUM;
    static const QString TOPIC_PREFIX  = "voltwatch/" + DEVICE_ID;
    static const QString CLIENT_ID     = "voltwatch-" + DEVICE_ID;

    // ntfy.sh push notification topic (for phone push notifications)
    // Auto-derived; each teammate's phone subscribes to their own topic.
    // e.g. "voltwatch-pqa-pi_01-alerts"
    static const QString NTFY_TOPIC    = "voltwatch-pqa-" + DEVICE_ID + "-alerts";
    static const QString NTFY_SERVER   = "https://ntfy.sh";

    // Voltage limits (IEEE 1159, +/-10% of 120 V nominal).
    // Alarms fire when V_rms is outside this band.
    // Overvoltage also opens the relay (latched until manual restart).
    // Undervoltage is informational only; relay stays closed.
    static const double NOMINAL_VOLTAGE = 120.0;
    static const double LOW_VOLT_LIMIT  = 108.0;
    static const double HIGH_VOLT_LIMIT = 132.0;
    static const double TRIP_LOW_VOLT_LIMIT = 102.0;   // relay trip threshold
          

    // Relay control (active-low: GPIO=1 -> relay OFF -> NC contacts closed -> load powered)
    static const int RELAY_GPIO = 16;
}
