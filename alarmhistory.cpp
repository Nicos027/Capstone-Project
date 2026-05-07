#include "alarmhistory.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>

AlarmHistory::AlarmHistory(QObject *parent) : QObject(parent)
{
}

AlarmHistory::~AlarmHistory()
{
    if (db_.isOpen()) {
        db_.close();
    }
}

bool AlarmHistory::initialize()
{
    // Place the database under the user's standard data directory:
    //   /home/<user>/.local/share/voltwatch/alarm_history.db
    // This follows XDG conventions and survives reinstalls of the
    // VoltWatch binary itself, since the data directory is independent
    // of the project folder.
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataDir.isEmpty()) {
        // Fallback for unusual environments
        dataDir = QDir::homePath() + "/.local/share/voltwatch";
    }

    QDir dir(dataDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            qWarning() << "Failed to create data directory:" << dataDir;
            return false;
        }
    }

    dbPath_ = dataDir + "/alarm_history.db";

    // Use a named connection so we don't conflict with any default
    // connection that might exist elsewhere in the app.
    db_ = QSqlDatabase::addDatabase("QSQLITE", "voltwatch_alarms");
    db_.setDatabaseName(dbPath_);

    if (!db_.open()) {
        qWarning() << "Failed to open alarm history database:"
                   << db_.lastError().text();
        return false;
    }

    // Create the table if it doesn't exist yet.
    QSqlQuery query(db_);
    bool ok = query.exec(
        "CREATE TABLE IF NOT EXISTS alarms ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  timestamp TEXT NOT NULL,"
        "  alarm_type TEXT NOT NULL,"
        "  vrms REAL NOT NULL,"
        "  irms REAL NOT NULL"
        ")"
    );
    if (!ok) {
        qWarning() << "Failed to create alarms table:"
                   << query.lastError().text();
        return false;
    }

    // Create an index on timestamp for fast newest-first queries.
    query.exec("CREATE INDEX IF NOT EXISTS idx_alarms_timestamp "
               "ON alarms (timestamp DESC)");

    initialized_ = true;
    qDebug() << "Alarm history database ready at" << dbPath_;
    return true;
}

void AlarmHistory::recordAlarm(const QString& alarmType, double vrms, double irms)
{
    if (!initialized_) {
        qWarning() << "AlarmHistory::recordAlarm called before initialize()";
        return;
    }

    QSqlQuery query(db_);
    query.prepare(
        "INSERT INTO alarms (timestamp, alarm_type, vrms, irms) "
        "VALUES (:timestamp, :alarm_type, :vrms, :irms)"
    );
    query.bindValue(":timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));
    query.bindValue(":alarm_type", alarmType);
    query.bindValue(":vrms", vrms);
    query.bindValue(":irms", irms);

    if (!query.exec()) {
        qWarning() << "Failed to insert alarm record:"
                   << query.lastError().text();
        return;
    }

    enforceRowLimit();
}

void AlarmHistory::enforceRowLimit()
{
    // Keep only the newest MAX_RECORDS rows. Delete anything older.
    // This runs after every insert; in steady state it deletes 0 or 1 row.
    QSqlQuery query(db_);
    query.prepare(QString(
        "DELETE FROM alarms WHERE id NOT IN ("
        "  SELECT id FROM alarms ORDER BY id DESC LIMIT %1"
        ")").arg(MAX_RECORDS)
    );
    if (!query.exec()) {
        qWarning() << "Failed to enforce row limit:"
                   << query.lastError().text();
    }
}

QVector<AlarmRecord> AlarmHistory::recentAlarms() const
{
    QVector<AlarmRecord> records;
    if (!initialized_) {
        return records;
    }

    QSqlQuery query(db_);
    query.prepare(
        "SELECT id, timestamp, alarm_type, vrms, irms "
        "FROM alarms "
        "ORDER BY id DESC "
        "LIMIT :limit"
    );
    query.bindValue(":limit", MAX_RECORDS);

    if (!query.exec()) {
        qWarning() << "Failed to read alarms:" << query.lastError().text();
        return records;
    }

    while (query.next()) {
        AlarmRecord r;
        r.id        = query.value(0).toLongLong();
        r.timestamp = QDateTime::fromString(query.value(1).toString(), Qt::ISODate);
        r.alarmType = query.value(2).toString();
        r.vrms      = query.value(3).toDouble();
        r.irms      = query.value(4).toDouble();
        records.append(r);
    }

    return records;
}
