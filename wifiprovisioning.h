#ifndef WIFIPROVISIONING_H
#define WIFIPROVISIONING_H

#include <QDialog>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;
class QProcess;

class WifiProvisioningDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WifiProvisioningDialog(QWidget *parent = nullptr);
    ~WifiProvisioningDialog();

private slots:
    void onConnectClicked();
    void onCancelClicked();
    void onProcessFinished(int exitCode, int exitStatus);

private:
    void buildUi();
    void computeUiScale();
    void setStatus(const QString& text, const QString& colorHex);
    void setBusy(bool busy);

    // Adaptive UI sizing — same pattern as MainWindow.
    // Reference design point is 800x480 (Freenove 5"); larger screens
    // scale up proportionally with a sensible cap.
    struct UiScale {
        int titleFont;
        int labelFont;
        int inputFont;
        int buttonFont;
        int statusFont;
        int btnW;
        int btnH;
        int inputH;
    };
    UiScale s_;

    QLabel       *titleLabel_;
    QLabel       *ssidLabel_;
    QLineEdit    *ssidInput_;
    QLabel       *passwordLabel_;
    QLineEdit    *passwordInput_;
    QPushButton  *connectBtn_;
    QPushButton  *cancelBtn_;
    QLabel       *statusLabel_;
    QProcess     *nmcliProcess_ = nullptr;
};

#endif
