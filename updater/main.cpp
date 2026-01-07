#include "mainwindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QStringList>
#include <QMessageBox>


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);


    QStringList args = QCoreApplication::arguments();

    QString text;
    text += "arguments count = " + QString::number(args.size()) + "\n\n";

    for (int i = 0; i < args.size(); ++i)
    {
        text += QString("args[%1] = %2\n").arg(i).arg(args[i]);
    }

    QMessageBox::information(nullptr, "Updater Arguments", text);

    MainWindow w;
    w.show();
    return a.exec();
}
