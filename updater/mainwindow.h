#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QProgressBar;
class QLabel;
class Updater;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow();

private:
    Ui::MainWindow *ui;
    QProgressBar* m_progressBar;
    QLabel* m_statusLabel;
    Updater* m_updater;
};
#endif // MAINWINDOW_H
