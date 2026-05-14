#include "mainwindow.h"
#include "wifiprovisioning.h"
#include "config.h"

#include <QApplication>
#include <QTcpSocket>
#include <QThread>
#include <QDebug>

// Boot-time check: can we reach the HiveMQ broker?
//
// Returns true if a TCP connection succeeds within timeoutMs.
static bool canReachBroker(int timeoutMs)
{
    using namespace VoltWatchConfig;

    QTcpSocket socket;
    socket.connectToHost(MQTT_BROKER, MQTT_PORT);
    bool connected = socket.waitForConnected(timeoutMs);
    if (connected) {
        socket.disconnectFromHost();
    }
    return connected;
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Give NetworkManager a moment to auto-connect to known networks
    // on boot. Without this, we'd race the boot sequence and falsely
    // conclude there's no internet just because Wi-Fi hadn't associated
    // yet. 5 seconds is generous; auto-connect is usually 2-3s.
    QThread::sleep(5);

    // First-attempt connectivity check.
    bool reachable = canReachBroker(5000);

    // If we can't reach the broker, drop into the Wi-Fi provisioning
    // splash. This is the entry point for any new location the Pi
    // hasn't been to before (e.g., traveling between Guam and the
    // mainland US), as well as the very first boot.
    while (!reachable) {
        WifiProvisioningDialog dlg;
        dlg.showFullScreen();
        int result = dlg.exec();

        if (result != QDialog::Accepted) {
            // User cancelled. PQA still measures V/I, displays readings and waveform. Alarms log locally only.
            // MQTT disabled and no push notifications.
            qWarning() << "Wi-Fi provisioning cancelled by user; Running in OFFLINE mode (no phone notifications)";
            break;
        }

        // Dialog reported success. Re-verify we can actually reach the
        // broker now (nmcli's exit code only confirms Wi-Fi attached,
        // not that the network has working internet egress).
        reachable = canReachBroker(5000);
        if (!reachable) {
            qWarning() << "Wi-Fi connected but broker still unreachable; "
                          "looping back to provisioning splash.";
            // Loop continues — splash will show again. The user can
            // try a different network or cancel.
        }
    }

    // We have working connectivity. Launch the main GUI as before.
    MainWindow w;
    w.showFullScreen();
    return a.exec();
}
