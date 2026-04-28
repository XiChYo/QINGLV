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
#include "caldistance.h"
#include "ConveyorTracker.h"
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

    void on_speedInfo_triggered();

    void on_chan1_clicked();


    void TrackerTask(Task task);

    void getAndsendA(int x);


    void on_addmTime_clicked();

    void on_lessmTime_clicked();


    void on_u0_clicked();

    void on_d0_clicked();

    void on_u150_clicked();

    void on_d150_clicked();

    void on_u180_clicked();

    void on_d180_clicked();

    void on_u210_clicked();

    void on_d210_clicked();

    void on_u240_clicked();

    void on_d240_clicked();

    void on_u270_clicked();

    void on_d270_clicked();

    void on_u300_clicked();

    void on_d300_clicked();

    void on_u330_clicked();

    void on_d330_clicked();

    void chan1_chan(QString ip);

    void on_auEnd_clicked();

    void on_adEnd_clicked();

    void on_auMoreTime_clicked();

    void on_adMoreTime_clicked();

    void on_aless300_clicked();

    void on_dless300_clicked();

    void on_b1_clicked();

    void on_b2_clicked();

    void on_b3_clicked();

    void on_b4_clicked();

    void on_b5_clicked();

    void on_b6_clicked();

    void on_b7_clicked();

    void on_b8_clicked();

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

    QGraphicsView* view; // ui输出画面
    QGraphicsScene* scene;
    QGraphicsPixmapItem* pixmapItem;

    camerathread* m_camThread; // 相机线程

    uploadpictoOSS* m_ossThread; // oss线程

    saveLocalpic* m_savelocalpicThread; // 保存照片线程

    yolorecognition* m_yolorecogThread; // 算法识别线程

    calDistance* m_calDistance; // 距离计算线程

    ConveyorTracker* m_tracker; // 喷气任务线程

    robotControl* m_robot; // 机器人线程

    tcpforrobot *m_tcpserver; // 机器人通信线程

    QThread* threadPool;
    QThread* threadPool_yolo;
    QThread* threadPool_robot;

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

    std::thread m_trackerthread;

    bool m_running;
    float speed;

    bool isABusy = false;
    bool isBBusy = false;

    bool isAconnected = false;
    bool isBconnected = false;

signals:
    void batchControl(QString order);
    void requestEncoder();
    void yoloImg(const QImage& image);

    void tcpRobotSig();
};
#endif // MAINWINDOW_H
