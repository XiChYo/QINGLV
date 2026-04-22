#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QList>
#include <QProgressBar>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include "uploadpictooss.h"
#include <QTranslator>
#include <QLabel>
#include "savelocalpic.h"
#include <atomic>
#include "robotcontrol.h"
#include "tcpforrobot.h"
#include "camera_worker.h"
#include "yolo_worker.h"
#include "runtime_config.h"
#include "boardcontrol.h"
#include "pipeline_types.h"
#include "tracker_worker.h"
#include "dispatcher.h"

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

    // 接 YoloWorker::detectedFrameReady(PR2 暂仅做一行日志,PR4 接 Tracker)
    void onDetectedFrame(const DetectedFrame& frame);

    // 接 boardControl::speedSample(UI 侧日志占位,真正消费者是 Tracker/Dispatcher)
    void onBoardSpeedSample(const SpeedSample& s);

    // Dispatcher -> MainWindow:Arm stub 派发日志
    void onArmStubDispatched(int trackId, int classId,
                             float aX, float aY, float bX, float bY);

    // Tracker -> MainWindow:PR4 做一行日志占位,PR5 可考虑接到 UI 计数器
    void onSortTask(const SortTask& task);

    // Dispatcher / Tracker -> MainWindow:告警
    void onPipelineWarning(const QString& msg);

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

    // ---- 新管道 worker(PR2) ----
    CameraWorker* m_cameraWorker = nullptr;
    YoloWorker*   m_yoloWorker   = nullptr;
    QThread*      m_cameraThread = nullptr;
    QThread*      m_yoloThread   = nullptr;

    // ---- BoardWorker(PR3,仍用原 boardControl 类扩展) ----
    boardControl* m_board         = nullptr;
    QThread*      m_boardThread   = nullptr;

    // ---- TrackerWorker + Dispatcher(PR4) ----
    TrackerWorker* m_tracker        = nullptr;
    Dispatcher*    m_dispatcher     = nullptr;
    QThread*       m_trackerThread  = nullptr;
    QThread*       m_dispatcherThread = nullptr;

    // ---- 历史 worker(保留) ----
    uploadpictoOSS* ossThread;
    saveLocalpic* savelocalpicThread;

    robotControl* m_robot;
    tcpforrobot *m_tcpserverA;

    QThread* threadPool;
    QThread* threadPool_robotA;

    // 启动时的配置快照(PR5 将切换为 Session 状态机按需加载)
    RuntimeConfig m_cfg;

    // ---- Session 状态机(PR5) ----
    enum class SessionState { Idle, Running, Stopping };
    SessionState m_sessionState = SessionState::Idle;

    // 根据 UI 勾选态合成 enabledClassIds,更新进 m_cfg
    void refreshEnabledClassIdsFromUi();
    // Run 按钮入口:合成 cfg → 依次 sessionStart 所有 worker
    bool startSession();
    // Stop/析构共用路径:按反向顺序 BlockingQueued sessionStop
    void stopSession();
    // AC13/AC14:运行中锁定 / Idle 时解锁参数类控件(当前仅 allButtons)。
    void setRuntimeControlsEnabled(bool enabled);

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
