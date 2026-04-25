#include "mqttpublisher.h"
#include "config.h"

#include <mosquitto.h>
#include <curl/curl.h>
#include <QDebug>
#include <QDateTime>
#include <QByteArray>

MqttPublisher::MqttPublisher(QObject *parent) : QObject(parent)
{
    mosquitto_lib_init();
}

MqttPublisher::~MqttPublisher()
{
    disconnectFromBroker();
    mosquitto_lib_cleanup();
}

bool MqttPublisher::connectToBroker(const QString& host,
                                     int port,
                                     const QString& clientId,
                                     const QString& username,
                                     const QString& password,
                                     const QString& topicPrefix)
{
    if (mosq_) {
        disconnectFromBroker();
    }

    topicPrefix_ = topicPrefix;

    QByteArray cid = clientId.toUtf8();
    mosq_ = mosquitto_new(cid.constData(), true, nullptr);
    if (!mosq_) {
        qWarning() << "Failed to create mosquitto client";
        return false;
    }

    // Enable TLS for HiveMQ Cloud (port 8883).
    // Passing nullptr for ca_file uses the system's default CA certificates,
    // which is what HiveMQ Cloud uses (Let's Encrypt).
    int rc = mosquitto_tls_set(mosq_, nullptr, "/etc/ssl/certs", nullptr, nullptr, nullptr);
    if (rc != MOSQ_ERR_SUCCESS) {
        qWarning() << "Failed to configure TLS:" << mosquitto_strerror(rc);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        return false;
    }

    // Set username and password for broker authentication
    QByteArray userBa = username.toUtf8();
    QByteArray passBa = password.toUtf8();
    rc = mosquitto_username_pw_set(mosq_, userBa.constData(), passBa.constData());
    if (rc != MOSQ_ERR_SUCCESS) {
        qWarning() << "Failed to set credentials:" << mosquitto_strerror(rc);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        return false;
    }

    QByteArray hostBa = host.toUtf8();
    rc = mosquitto_connect(mosq_, hostBa.constData(), port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        qWarning() << "Failed to connect to MQTT broker:"
                   << mosquitto_strerror(rc);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        return false;
    }

    rc = mosquitto_loop_start(mosq_);
    if (rc != MOSQ_ERR_SUCCESS) {
        qWarning() << "Failed to start mosquitto loop:"
                   << mosquitto_strerror(rc);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        return false;
    }

    connected_ = true;
    qDebug() << "MQTT connected to" << host << ":" << port
             << "as" << username << "prefix" << topicPrefix;
    return true;
}

void MqttPublisher::disconnectFromBroker()
{
    if (mosq_) {
        mosquitto_loop_stop(mosq_, true);
        mosquitto_disconnect(mosq_);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
    connected_ = false;
}

void MqttPublisher::publishReading(double vrms, double irms, const QString& alarm)
{
    if (!connected_ || !mosq_) return;

    QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QString payload = QString("{\"timestamp\":\"%1\",\"device\":\"%2\","
                              "\"vrms\":%3,\"irms\":%4,\"alarm\":\"%5\"}")
                        .arg(timestamp)
                        .arg(topicPrefix_.section('/', -1))  // e.g. "pi_01"
                        .arg(vrms, 0, 'f', 2)
                        .arg(irms, 0, 'f', 2)
                        .arg(alarm);

    QString topic = topicPrefix_ + "/telemetry";
    QByteArray topicBa = topic.toUtf8();
    QByteArray payloadBa = payload.toUtf8();

    int rc = mosquitto_publish(mosq_, nullptr, topicBa.constData(),
                                payloadBa.size(), payloadBa.constData(),
                                0,      // QoS 0 for telemetry
                                false); // not retained
    if (rc != MOSQ_ERR_SUCCESS) {
        qWarning() << "MQTT publish failed:" << mosquitto_strerror(rc);
    }
}

void MqttPublisher::publishAlarm(const QString& alarmType, double vrms, double irms)
{
    if (!connected_ || !mosq_) return;

    QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QString payload = QString("{\"timestamp\":\"%1\",\"device\":\"%2\","
                              "\"event_type\":\"%3\","
                              "\"vrms\":%4,\"irms\":%5}")
                        .arg(timestamp)
                        .arg(topicPrefix_.section('/', -1))
                        .arg(alarmType)
                        .arg(vrms, 0, 'f', 2)
                        .arg(irms, 0, 'f', 2);

    QString topic = topicPrefix_ + "/alarms";
    QByteArray topicBa = topic.toUtf8();
    QByteArray payloadBa = payload.toUtf8();

    int rc = mosquitto_publish(mosq_, nullptr, topicBa.constData(),
                                payloadBa.size(), payloadBa.constData(),
                                1,      // QoS 1 for alarms
                                true);  // retained
    if (rc != MOSQ_ERR_SUCCESS) {
        qWarning() << "MQTT alarm publish failed:" << mosquitto_strerror(rc);
    } else {
        qDebug() << "Published alarm:" << alarmType;
    }

    // Also send push notification via ntfy.sh so the owner's phone
    // buzzes even if the MQTT dashboard app is closed.
    sendNtfyPush(alarmType, vrms, irms);
}

void MqttPublisher::sendNtfyPush(const QString& alarmType, double vrms, double irms)
{
    using namespace VoltWatchConfig;

    QString url = NTFY_SERVER + "/" + NTFY_TOPIC;
    QString title = QString("VoltWatch %1 - %2")
                        .arg(DEVICE_ID.toUpper())
                        .arg(alarmType);
    QString message = QString("V_rms: %1 V   |   I_rms: %2 A")
                          .arg(vrms, 0, 'f', 2)
                          .arg(irms, 0, 'f', 2);

    CURL *curl = curl_easy_init();
    if (!curl) {
        qWarning() << "curl init failed for ntfy";
        return;
    }

    QByteArray urlBa = url.toUtf8();
    QByteArray titleBa = title.toUtf8();
    QByteArray msgBa = message.toUtf8();
    QByteArray tagsBa = QByteArray("Tags: warning,zap");
    QByteArray priorityBa = QByteArray("Priority: urgent");

    struct curl_slist *headers = nullptr;
    QByteArray titleHeader = "Title: " + titleBa;
    headers = curl_slist_append(headers, titleHeader.constData());
    headers = curl_slist_append(headers, priorityBa.constData());
    headers = curl_slist_append(headers, tagsBa.constData());

    curl_easy_setopt(curl, CURLOPT_URL, urlBa.constData());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, msgBa.constData());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)msgBa.size());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        qWarning() << "ntfy push failed:" << curl_easy_strerror(res);
    } else {
        qDebug() << "ntfy push sent to" << url;
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}
