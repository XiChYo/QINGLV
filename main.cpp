#include "mainwindow.h"

#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QSettings>
#include "pipeline_types.h"
#include "pipeline_clock.h"
#include "runtime_config.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    pipeline::initClock();

    qRegisterMetaType<DetectedObject>("DetectedObject");
    qRegisterMetaType<DetectedFrame>("DetectedFrame");
    qRegisterMetaType<TrackedObject>("TrackedObject");
    qRegisterMetaType<DispatchedGhost>("DispatchedGhost");
    qRegisterMetaType<SortTask>("SortTask");
    qRegisterMetaType<SpeedSample>("SpeedSample");
    qRegisterMetaType<ValvePulse>("ValvePulse");
    qRegisterMetaType<QVector<ValvePulse>>("QVector<ValvePulse>");
    qRegisterMetaType<RuntimeConfig>("RuntimeConfig");

    //读取 config.ini 中的语言设置
    QSettings settings("config.ini", QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    QString language = settings.value("config/language", "chinese").toString();

    //加载翻译器
    QTranslator translator;

    if (language == "english") {
        translator.load("guangxuan_zh_CN.qm");
    } else {
        translator.load("guangxuan_zh_EN.qm");
    }

    a.installTranslator(&translator);

    MainWindow w;
    w.showFullScreen();
    w.show();
    return a.exec();
}
