#ifndef MQTTPUBLISHER_H
#define MQTTPUBLISHER_H

#include <QObject>
#include <QString>

struct mosquitto;

class MqttPublisher : public QObject
{
    Q_OBJECT
public:
    explicit MqttPublisher(QObject *parent = nullptr);
    ~MqttPublisher();

    // Connect to cloud broker using TLS + username/password authentication.
    // Pass the device's topic prefix (e.g. "voltwatch/pi_01") so published
    // messages land under the correct namespace.
    bool connectToBroker(const QString& host,
                         int port,
                         const QString& clientId,
                         const QString& username,
                         const QString& password,
                         const QString& topicPrefix);

    void disconnectFromBroker();
    bool isConnected() const { return connected_; }

public slots:
    void publishReading(double vrms, double irms, const QString& alarm);
    void publishAlarm(const QString& alarmType, double vrms, double irms);

private:
    struct mosquitto *mosq_ = nullptr;
    bool connected_ = false;
    QString topicPrefix_;

    // Send a push notification via ntfy.sh (HTTP POST)
    void sendNtfyPush(const QString& alarmType, double vrms, double irms);
};

#endif
