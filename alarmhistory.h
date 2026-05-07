#ifndef ALARMHISTORY_H
#define ALARMHISTORY_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QVector>
#include <QSqlDatabase>

// Single record describing one historical alarm.
// Used both for inserting new alarms and for displaying them in the history view.
struct AlarmRecord {
    qint64    id;          // database row id (primary key)
    QDateTime timestamp;   // when the alarm fired (local time)
    QString   alarmType;   // "OVERVOLTAGE", "UNDERVOLTAGE_TRIP", "OVERCURRENT", etc.
    double    vrms;        // V_rms at trigger
    double    irms;        // I_rms at trigger
};

// Manages the persistent alarm history database.
//
// The database lives at ~/.local/share/voltwatch/alarm_history.db
// (per-user, follows XDG conventions). Limited to MAX_RECORDS rows;
// when a new alarm pushes us over the limit, the oldest row is deleted.
//
// The database is independent of MQTT, the network, and the cloud broker.
// Alarms are recorded the moment Worker emits alarmTriggered, regardless
// of whether the device is online or offline.
class AlarmHistory : public QObject
{
    Q_OBJECT
public:
    static constexpr int MAX_RECORDS = 100;

    explicit AlarmHistory(QObject *parent = nullptr);
    ~AlarmHistory();

    // Initialize the database file and schema. Call once at startup.
    // Returns false if the database can't be opened/created (rare; only
    // happens if the SD card is full or permissions are wrong).
    bool initialize();

    // Insert a new alarm record. If the table is at MAX_RECORDS, the
    // oldest record is deleted to make room.
    void recordAlarm(const QString& alarmType, double vrms, double irms);

    // Return the most recent records, newest first.
    // If the table has fewer than MAX_RECORDS, returns whatever is there.
    QVector<AlarmRecord> recentAlarms() const;

private:
    void enforceRowLimit();

    QSqlDatabase db_;
    bool         initialized_ = false;
    QString      dbPath_;
};

#endif
