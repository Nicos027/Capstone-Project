#include "wifiprovisioning.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProcess>
#include <QGuiApplication>
#include <QScreen>
#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <algorithm>

WifiProvisioningDialog::WifiProvisioningDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("VoltWatch - Wi-Fi Setup");
    setModal(true);
    computeUiScale();
    buildUi();
}

WifiProvisioningDialog::~WifiProvisioningDialog()
{
    if (nmcliProcess_) {
        nmcliProcess_->disconnect();
        if (nmcliProcess_->state() != QProcess::NotRunning) {
            nmcliProcess_->kill();
            nmcliProcess_->waitForFinished(1000);
        }
        delete nmcliProcess_;
        nmcliProcess_ = nullptr;
    }
}

void WifiProvisioningDialog::computeUiScale()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    int sw = screen ? screen->geometry().width()  : 800;
    int sh = screen ? screen->geometry().height() : 480;

    double rw = sw / 800.0;
    double rh = sh / 480.0;
    double k  = std::min(rw, rh);
    if (k < 1.0)  k = 1.0;
    if (k > 2.5)  k = 2.5;

    auto px = [k](int basePx) {
        return std::max(1, static_cast<int>(basePx * k + 0.5));
    };

    s_.titleFont  = px(22);
    s_.labelFont  = px(13);
    s_.inputFont  = px(15);
    s_.buttonFont = px(15);
    s_.statusFont = px(12);
    s_.btnW       = px(140);
    s_.btnH       = px(44);
    s_.inputH     = px(38);
}

void WifiProvisioningDialog::buildUi()
{
    setStyleSheet(
        "QDialog { background: #1a1a1a; }"
        "QWidget { color: #e8e8e8; font-family: Arial, sans-serif; }"
    );

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(12);

    // --- Title ---
    titleLabel_ = new QLabel("Connect to Wi-Fi", this);
    titleLabel_->setAlignment(Qt::AlignCenter);
    titleLabel_->setStyleSheet(
        QString("color: #C0DD97; font-size: %1px; font-weight: bold; "
                "letter-spacing: 1px;").arg(s_.titleFont)
    );
    mainLayout->addWidget(titleLabel_);

    // --- Subtitle ---
    QLabel *subtitle = new QLabel(
        "VoltWatch needs an internet connection.\n"
        "Enter your Wi-Fi network details below.", this);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(
        QString("color: #888; font-size: %1px;").arg(s_.labelFont)
    );
    mainLayout->addWidget(subtitle);

    mainLayout->addSpacing(8);

    // --- SSID field ---
    ssidLabel_ = new QLabel("Network name (SSID):", this);
    ssidLabel_->setStyleSheet(
        QString("color: #aaa; font-size: %1px; font-weight: bold;").arg(s_.labelFont)
    );
    mainLayout->addWidget(ssidLabel_);

    ssidInput_ = new QLineEdit(this);
    ssidInput_->setFixedHeight(s_.inputH);
    ssidInput_->setStyleSheet(QString(
        "QLineEdit { background: #262626; color: #e8e8e8; "
        "border: 1px solid #404040; border-radius: 4px; "
        "padding: 4px 8px; font-size: %1px; }"
        "QLineEdit:focus { border: 1px solid #97C459; }"
    ).arg(s_.inputFont));
    mainLayout->addWidget(ssidInput_);

    // --- Password field ---
    passwordLabel_ = new QLabel("Password:", this);
    passwordLabel_->setStyleSheet(
        QString("color: #aaa; font-size: %1px; font-weight: bold;").arg(s_.labelFont)
    );
    mainLayout->addWidget(passwordLabel_);

    passwordInput_ = new QLineEdit(this);
    passwordInput_->setFixedHeight(s_.inputH);
    passwordInput_->setEchoMode(QLineEdit::Password);
    passwordInput_->setStyleSheet(QString(
        "QLineEdit { background: #262626; color: #e8e8e8; "
        "border: 1px solid #404040; border-radius: 4px; "
        "padding: 4px 8px; font-size: %1px; }"
        "QLineEdit:focus { border: 1px solid #97C459; }"
    ).arg(s_.inputFont));
    mainLayout->addWidget(passwordInput_);

    mainLayout->addSpacing(8);

    // --- Status label ---
    statusLabel_ = new QLabel(" ", this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setWordWrap(true);
    statusLabel_->setMinimumHeight(s_.statusFont * 3);
    statusLabel_->setStyleSheet(
        QString("color: #888; font-size: %1px;").arg(s_.statusFont)
    );
    mainLayout->addWidget(statusLabel_);

    mainLayout->addStretch();

    // --- Buttons ---
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(12);
    btnLayout->addStretch();

    cancelBtn_ = new QPushButton("Cancel", this);
    cancelBtn_->setFixedSize(s_.btnW, s_.btnH);
    cancelBtn_->setStyleSheet(QString(
        "QPushButton { background: #262626; color: #aaa; "
        "border: 2px solid #555; border-radius: 8px; "
        "font-size: %1px; font-weight: bold; }"
        "QPushButton:hover { background: #333; color: #e8e8e8; }"
        "QPushButton:disabled { background: #1a1a1a; color: #555; }"
    ).arg(s_.buttonFont));
    btnLayout->addWidget(cancelBtn_);

    connectBtn_ = new QPushButton("Connect", this);
    connectBtn_->setFixedSize(s_.btnW, s_.btnH);
    connectBtn_->setStyleSheet(QString(
        "QPushButton { background: #3B6D11; color: #EAF3DE; "
        "border: 2px solid #97C459; border-radius: 8px; "
        "font-size: %1px; font-weight: bold; letter-spacing: 1px; }"
        "QPushButton:hover { background: #4a8817; }"
        "QPushButton:disabled { background: #1a1a1a; color: #555; "
        "border: 2px solid #333; }"
    ).arg(s_.buttonFont));
    connectBtn_->setDefault(true);
    btnLayout->addWidget(connectBtn_);

    mainLayout->addLayout(btnLayout);

    connect(connectBtn_, &QPushButton::clicked,
            this, &WifiProvisioningDialog::onConnectClicked);
    connect(cancelBtn_, &QPushButton::clicked,
            this, &WifiProvisioningDialog::onCancelClicked);

    // Pressing Enter in either field triggers Connect
    connect(ssidInput_,     &QLineEdit::returnPressed,
            this, &WifiProvisioningDialog::onConnectClicked);
    connect(passwordInput_, &QLineEdit::returnPressed,
            this, &WifiProvisioningDialog::onConnectClicked);
}

void WifiProvisioningDialog::setStatus(const QString& text, const QString& colorHex)
{
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(
        QString("color: %1; font-size: %2px; font-weight: bold;")
            .arg(colorHex).arg(s_.statusFont)
    );
}

void WifiProvisioningDialog::setBusy(bool busy)
{
    ssidInput_->setEnabled(!busy);
    passwordInput_->setEnabled(!busy);
    connectBtn_->setEnabled(!busy);
    cancelBtn_->setEnabled(!busy);
    if (busy) {
        connectBtn_->setText("Connecting...");
    } else {
        connectBtn_->setText("Connect");
    }
}

void WifiProvisioningDialog::onConnectClicked()
{
    QString ssid = ssidInput_->text().trimmed();
    QString password = passwordInput_->text();

    if (ssid.isEmpty()) {
        setStatus("Please enter a network name.", "#F09595");
        return;
    }
    if (password.length() < 8) {
        setStatus("Wi-Fi password must be at least 8 characters.", "#F09595");
        return;
    }

    setStatus("Connecting to " + ssid + "...", "#EF9F27");
    setBusy(true);

    // Create the QProcess fresh for each attempt so failed-state from
    // a previous run can't bleed into this one.
    if (nmcliProcess_) {
        nmcliProcess_->disconnect();
        nmcliProcess_->deleteLater();
        nmcliProcess_ = nullptr;
    }
    nmcliProcess_ = new QProcess(this);
    nmcliProcess_->setProcessChannelMode(QProcess::MergedChannels);

    connect(nmcliProcess_,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            &WifiProvisioningDialog::onProcessFinished);

    QStringList args;
    args << "device" << "wifi" << "connect" << ssid
         << "password" << password;

    nmcliProcess_->start("nmcli", args);
}

void WifiProvisioningDialog::onCancelClicked()
{
    // Cancel rejects the dialog — the boot logic in main.cpp will exit
    // the application so the user is back at the desktop.
    reject();
}

void WifiProvisioningDialog::onProcessFinished(int exitCode, int exitStatus)
{
    Q_UNUSED(exitStatus);
    setBusy(false);

    QString output;
    if (nmcliProcess_) {
        output = QString::fromUtf8(nmcliProcess_->readAll()).trimmed();
        qDebug() << "nmcli exit code:" << exitCode << " output:" << output;
    }

    if (exitCode == 0) {
        // Successful connection. NetworkManager has saved the profile,
        // so future boots in range of this network will auto-connect.
        setStatus("Connected! Starting VoltWatch...", "#C0DD97");
        // Brief pause so the user sees the success message
        QTimer::singleShot(900, this, [this]() { accept(); });
    } else {
        // Translate the most common nmcli failure messages into something
        // a non-technical user can act on. Fall back to raw output if
        // we don't recognize the message.
        QString friendly;
        QString lower = output.toLower();
        if (lower.contains("no network with ssid")) {
            friendly = "Network not found. Check the SSID and try again.";
        } else if (lower.contains("secrets were required") ||
                   lower.contains("802-11-wireless-security") ||
                   lower.contains("invalid password") ||
                   lower.contains("authentication") ||
                   lower.contains("psk")) {
            friendly = "Wrong password. Please try again.";
        } else if (lower.contains("timeout")) {
            friendly = "Connection timed out. Check the network and try again.";
        } else if (output.isEmpty()) {
            friendly = QString("Connection failed (code %1).").arg(exitCode);
        } else {
            friendly = "Connection failed: " + output;
        }
        setStatus(friendly, "#F09595");
        passwordInput_->selectAll();
        passwordInput_->setFocus();
    }
}
