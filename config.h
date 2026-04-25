// config.h
// =======================================================
// Per-device configuration
//
//   1. Scroll down and edit your device number
//   2. DEVICE_NUM    -> your Pi number (01, 02, or 03)
//   3. MQTT_PASSWORD -> the HiveMQ password in SOP
//
//
// Everything else is shared and should NOT be changed.
// =======================================================
#pragma once
#include <QString>
namespace VoltWatchConfig {
    // ========== EDIT THESE TWO LINES ==========
    //
    // 1. DEVICE_NUM: "01" for Nicos, "02" for Rossana, "03" for Jose
    //    Keep the quotes. Must be two digits with leading zero.
    static const QString DEVICE_NUM = "01";
    // 2. MQTT_PASSWORD: listed in SOP
    static const QString MQTT_PASSWORD = "PASTE-YOUR-PASSWORD-HERE";
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
    // e.g. "voltwatch-pqa-pi01-alerts"
    static const QString NTFY_TOPIC    = "voltwatch-pqa-" + DEVICE_ID + "-alerts";
    static const QString NTFY_SERVER   = "https://ntfy.sh";

    // Measurement limits (IEEE 1159, +/-10% of 120 V nominal)
    static const double NOMINAL_VOLTAGE = 120.0;
    static const double LOW_VOLT_LIMIT  = 108.0;
    static const double HIGH_VOLT_LIMIT = 132.0;
    static const double HIGH_CURRENT_LIMIT = 6.5;
    // Relay control
    static const int RELAY_GPIO = 16;
}
