#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QList>
#include <QProgressBar>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include "camerathread.h"
#include "uploadpictooss.h"
#include <QTranslator>
#include <QLabel>
#include "savelocalpic.h"
#include <thread>
#include <atomic>
#include "yolorecognition.h"
#include "valvecmd.h"
#include "robotcontrol.h"
#include "tcpforrobot.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_homepage_clicked();

    void on_materialselection_clicked();

    void on_systemmanagement_clicked();

    void onAnyButtonClicked();

    void on_reset_clicked();

    void on_run_clicked();

    void on_powerbutton_clicked();

    void updateFrame(const QImage& img);

    void cameraerrorMegSig(const QString &msg);

    void uploadOSSPath(const QString& filePath, const int ImgClass);

    void on_languageButton_clicked();

    void on_checkforNew_clicked();

    void on_singleControl_triggered();
    void on_multiControl_triggered();
    void on_speedInfo_triggered();

    void on_chan1_clicked();

    void chan1_chan(QString ip);

private:
    Ui::MainWindow *ui;
    QList<QPushButton*> allButtons; // 保存所有在物料选择生成的按钮

    QList<QPushButton*> colorButtons;
    QList<QPushButton*> categoryButtons; // 保存物料选择中的品类按钮
    QList<QPushButton*> labelButtons;
    QList<QPushButton*> shapeButtons;
    QList<QPushButton*> appearanceButtons;

    QList<QLabel*> allTopLabels;

    QString m_currentLanguage;

    QList<QProgressBar*> progressBars; // 保存所有生成的 progressbar
    QStringList logList; // 保存日志信息

    QGraphicsView* view;
    QGraphicsScene* scene;
    QGraphicsPixmapItem* pixmapItem;
    camerathread* camThread;
    uploadpictoOSS* ossThread;
    saveLocalpic* savelocalpicThread;
    yolorecognition* yolorecogThread;

    robotControl* m_robot;

    tcpforrobot *m_tcpserverA;

    QThread* threadPool;
    QThread* threadPool_yolo;
    QThread* threadPool_robotA;

    QString logMsg;
    bool uploadOssSorF;
    QTranslator m_translator;

    void loadSelectedButtonsFromIni();
    void retryUploadFailedImages();  // 历史图片失败重传
    void createButtonsFromini(const QString& groupName,
                              int startRow,
                              int maxRows,
                              QList<QPushButton*>& targetList);
    void updateLanguageButton();
    void applyTextFromIni(const QString& groupName, const QList<QPushButton*>& buttons);


    void onEncoderSpeed(const QByteArray& frame);

    std::thread m_thread;
    bool m_running;
    float speed;

    bool isABusy = false;
    bool isBBusy = false;

    bool isAconnected = false;
    bool isBconnected = false;

signals:
    void singleControl(QString order);
    void batchControl(QString order);
    void requestEncoder();
    void yoloImg(const QImage& image);

    void testinitRobot();
    void testMoveRobot();
    void tcpPosSigA(QByteArray data, float time);
    void tcpPosSigB(QByteArray data, float time);
    void tcpRobotSigA();
    void isUseA();
};
#endif // MAINWINDOW_H
