#include "mainwindow.h"

#include <QApplication>
#include <QTranslator>
#include <QLocale>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    //读取 config.ini 中的语言设置
    QSettings settings("config.ini", QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    QString language = settings.value("config/language", "chinese").toString();

    //加载翻译器
    QTranslator translator;

    if (language == "english") {
        translator.load(":/guangxuan_zh_CN.qm");
    } else {
        translator.load(":/guangxuan_zh_EN.qm");
    }

    a.installTranslator(&translator);

    MainWindow w;
//    w.showFullScreen();
    w.show();
    return a.exec();
}
