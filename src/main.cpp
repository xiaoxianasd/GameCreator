#include "ui/MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QSurfaceFormat>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("GameCreator"));
    QCoreApplication::setOrganizationName(QStringLiteral("GameCreator"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    gc::MainWindow w;
    w.show();

    // 调试：设置 GC_SCREENSHOT=路径 时，延时自截窗口并退出（用于 UI 走查）
    const QString shot = qEnvironmentVariable("GC_SCREENSHOT");
    if (!shot.isEmpty()) {
        const int delay = qEnvironmentVariableIntValue("GC_SCREENSHOT_DELAY");
        QTimer::singleShot(delay > 0 ? delay : 6000, &w, [&w, shot] {
            w.grab().save(shot);
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
