#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QDate>
#include <QDebug>
#include <QProgressBar>
#include <QFrame>
#include <QLabel>
#include <climits>  // 用 INT_MAX
#include <QRandomGenerator>
#include <QMessageBox>
#include <QProcess>
#include <memory>
#include <QGridLayout>
#include <QRandomGenerator>
#include "logger.h"
#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include "updatemanager.h"
#include "boardcontrol.h"
#include "pipeline_clock.h"

// ============================================================================
// QSS 片段:tab 按钮的激活/非激活样式
// 之前三处重复,抽取成静态字符串常量
// ============================================================================
namespace {

static const char* kTabBtnActiveQss =
    "QPushButton {"
    "border-left: 1px solid #e0e0e0;"
    "border-right: 1px solid #e0e0e0;"
    "border-top: none;"
    "border-bottom: 1px solid #e0e0e0;"
    "border-radius: 0px;"
    "background-color: rgb(242, 242, 242);"
    "color: green;"
    "}"
    "QPushButton:pressed {"
    "background-color: #e0e0e0;"
    "}";

static const char* kTabBtnIdleQss =
    "QPushButton {"
    "border-left: 1px solid #e0e0e0;"
    "border-right: 1px solid #e0e0e0;"
    "border-top: none;"
    "border-bottom: 1px solid #e0e0e0;"
    "border-radius: 0px;"
    "background-color: rgb(242, 242, 242);"
    "color: black;"
    "}"
    "QPushButton:pressed {"
    "background-color: #e0e0e0;"
    "}";

}  // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    try{
        LOG_INFO("AI Optical Sorter Software Starting");
        ui->setupUi(this);
        ui->homePage->show();
        ui->materialSelection->hide();
        ui->systemManagement->hide();
        QGridLayout *grid = qobject_cast<QGridLayout*>(ui->mainWindow->layout());
        grid->addWidget(ui->homePage,0,0);
        grid->addWidget(ui->materialSelection,0,0);
        grid->addWidget(ui->systemManagement,0,0);

        // 获取当前系统日期
        QDate date = QDate::currentDate();
        QString dateString = date.toString("yyyy-MM-dd");
        ui->dateTime->setText(QT_TR_NOOP(dateString));

        // 语言切换按钮
        QSettings settings("config.ini", QSettings::IniFormat);
        m_currentLanguage = settings.value("config/language").toString();
        updateLanguageButton();

        // 颜色，品类（2 行），标签，形态，品相
        createButtonsFromini("colorButtons", 1, 1, colorButtons);
        createButtonsFromini("categoryButtons", 3, 2, categoryButtons);
        createButtonsFromini("labelButtons", 6, 1, labelButtons);
        createButtonsFromini("shapeButtons", 8, 1, shapeButtons);
        createButtonsFromini("appearanceButtons", 10, 1, appearanceButtons);
        loadSelectedButtonsFromIni();

        // 物料选择界面的排版
        ui->gridLayout->addWidget(ui->Hline1, 2, 0, 1, 9);
        ui->gridLayout->addWidget(ui->Hline2, 5, 0, 1, 9);
        ui->gridLayout->addWidget(ui->Hline3, 7, 0, 1, 9);
        ui->gridLayout->addWidget(ui->Hline4, 9, 0, 1, 9);
        ui->gridLayout->addWidget(ui->Hline5, 11, 0, 1, 9);

        // 主界面相机画面
        scene = new QGraphicsScene(this);
        pixmapItem = new QGraphicsPixmapItem();
        scene->addItem(pixmapItem);

        ui->cameraview->setScene(scene);
        ui->cameraview->setRenderHint(QPainter::Antialiasing, false);
        ui->cameraview->setRenderHint(QPainter::SmoothPixmapTransform, true);

        // 线程池
        threadPool = new QThread;
        threadPool_yolo = new QThread;
        threadPool_robotA = new QThread;

        // OSS线程
        ossThread = new uploadpictoOSS;
        ossThread->moveToThread(threadPool);

        // boardcontrol
        boardControl* ctrl = new boardControl;
        ctrl->moveToThread(threadPool);
        connect(threadPool, &QThread::started,
                ctrl, &boardControl::initSerial);

        connect(this, &MainWindow::singleControl,
                ctrl, &boardControl::singleBoardControl);

        connect(this, &MainWindow::batchControl,
                ctrl, &boardControl::batchBoardControl);

        connect(this, &MainWindow::requestEncoder,
                ctrl, &boardControl::requestEncoderSpeed);

        connect(ctrl, &boardControl::encoderSpeedReceived,
                this, &MainWindow::onEncoderSpeed);

        // 保存本地文件线程
        savelocalpicThread = new saveLocalpic;
        savelocalpicThread->moveToThread(threadPool);

        // 摄像头线程
        camThread = new camerathread;
        connect(savelocalpicThread, &saveLocalpic::forOSSPathSig,
                this, &MainWindow::uploadOSSPath);
        connect(camThread, &camerathread::errorMegSig,
                this, &MainWindow::cameraerrorMegSig,Qt::QueuedConnection);
        if (camThread->openCamera("192.168.1.30")) {
            LOG_INFO("Camera thread started");
            camThread->start();
        }

        yolorecogThread = new yolorecognition;
        yolorecogThread->moveToThread(threadPool_yolo);
        connect(camThread, &camerathread::frameReadySig,
                yolorecogThread, &yolorecognition::recognition);
        connect(yolorecogThread, &yolorecognition::resultImgSig,
                this, &MainWindow::updateFrame);

        // NOTE: pointSig/objPointSig 的新路由将在 PR4 由 TrackerWorker/Dispatcher 接管,
        // PR1 阶段仅拆线,不再连接旧的 calDistance / ConveyorTracker 链路。

        m_robot = new robotControl;
        m_robot->moveToThread(threadPool_robotA);
        connect(this, &MainWindow::testinitRobot,
                m_robot, &robotControl::initRobot);
        connect(this, &MainWindow::testMoveRobot,
                m_robot, &robotControl::testRobotControl);

        m_tcpserverA = new tcpforrobot;
        m_tcpserverA->moveToThread(threadPool_robotA);
        connect(this, &MainWindow::tcpRobotSigA,
                m_tcpserverA, &tcpforrobot::startServer);
        connect(this, &MainWindow::tcpPosSigA,
                m_tcpserverA, &tcpforrobot::sendData);
        connect(m_tcpserverA, &tcpforrobot::clientConnected,
                this, &MainWindow::chan1_chan);

        m_running = false;

        // 线程池中的线程启动
        threadPool->start();
        threadPool_yolo->start();
        threadPool_robotA->start();
    }catch(std::exception& e)
    {
        logMsg = "MainFuc: " + QString::fromStdString(e.what());
        LOG_ERROR(logMsg);
    }
}

MainWindow::~MainWindow()
{
    if (camThread) {
        camThread->stop();
    }

    auto stopPool = [](QThread*& pool) {
        if (!pool) return;
        pool->quit();
        pool->wait();
        delete pool;
        pool = nullptr;
    };
    stopPool(threadPool);
    stopPool(threadPool_yolo);
    stopPool(threadPool_robotA);

    delete ui;
}

void MainWindow::createButtonsFromini(const QString& groupName, int startRow, int maxRows, QList<QPushButton*>& targetList)
{
    QSettings settings("config.ini", QSettings::IniFormat);
    settings.setIniCodec("UTF-8");

    settings.beginGroup(groupName);

    QStringList keys = settings.childKeys();

    int col = 2;              // 按钮从第2列开始
    int row = startRow;       // 每种属性的按钮开始的行数不同
    const int maxCol = 8;     // 一行最多按钮数

    for (const QString& key : keys)
    {
        QString text = settings.value(key).toString();

        QPushButton* btn = new QPushButton;
        btn->setObjectName(key);

        // 按钮中英文切换
        if (m_currentLanguage == "chinese")
        {
            btn->setText(tr(text.toUtf8().constData()));
        }else if (m_currentLanguage == "english")
        {
            btn->setText(key);
        }

        ui->gridLayout->addWidget(btn, row, col);

        targetList.append(btn);
        allButtons.append(btn);

        btn->setProperty("state", false);
        btn->setStyleSheet("QPushButton {\n    background-color: white;"
                           "\n    border: 1px solid gray;"
                           "\n    border-radius: 10px;"
                           "\n    color: black;"
                           "\n    font-size: 14px;"
                           "\n}\nQPushButton:pressed "
                           "{\n    background-color: #e0e0e0;"
                           "\n    border: 1px solid #555555;}");
        btn->setMinimumSize(80, 35);
        btn->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        // 给每个按钮绑定相同逻辑
        connect(btn, &QPushButton::clicked,this, &MainWindow::onAnyButtonClicked);// 改变按钮样式，生成对应样式的label

        col++;

        // 超过列数就换行
        if (col > maxCol)
        {
            col = 2;
            row++;

            // 超过该属性允许的最大行数，直接停止
            if (row >= startRow + maxRows)
                break;
        }
    }
    settings.endGroup();
}

void MainWindow::cameraerrorMegSig(const QString &msg)
{
    logMsg = "[Camera Error]: " + msg;
    LOG_ERROR(logMsg);
}
void MainWindow::on_homepage_clicked()
{
    ui->homePage->show();
    ui->materialSelection->hide();
    ui->systemManagement->hide();

    ui->homepage->setStyleSheet(kTabBtnActiveQss);
    ui->materialselection->setStyleSheet(kTabBtnIdleQss);
    ui->systemmanagement->setStyleSheet(kTabBtnIdleQss);
}

void MainWindow::on_materialselection_clicked()
{
    ui->homePage->hide();
    ui->materialSelection->show();
    ui->systemManagement->hide();

    ui->homepage->setStyleSheet(kTabBtnIdleQss);
    ui->materialselection->setStyleSheet(kTabBtnActiveQss);
    ui->systemmanagement->setStyleSheet(kTabBtnIdleQss);
}

void MainWindow::on_systemmanagement_clicked()
{
    ui->homePage->hide();
    ui->materialSelection->hide();
    ui->systemManagement->show();

    ui->homepage->setStyleSheet(kTabBtnIdleQss);
    ui->materialselection->setStyleSheet(kTabBtnIdleQss);
    ui->systemmanagement->setStyleSheet(kTabBtnActiveQss);
}

void MainWindow::onAnyButtonClicked()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) return;

    QString labelName = btn->objectName() + "_"; // 这个label的objectname不能随便改，后面中英文切换功能有用到
    bool state = btn->property("state").toBool(); // 获取按钮状态
    btn->setProperty("state", !state); // 每点击一次，反转当前按钮状态

    // 查找该 label 是否已存在
    QLabel *existLabel = ui->showlabel->parentWidget()->findChild<QLabel *>(labelName);

    if (!existLabel)
    {
        // 创建 label（父对象是 this，Qt 会自动管理生命周期）
        QLabel* label = new QLabel(btn->text(), this);
        label->setObjectName(labelName);

        // 设置大小
        label->setMaximumHeight(40);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(
            "background-color: #26b9bd;"
            "border: 1px solid gray;"
            "border-radius: 10px;"
            "color: white;"
            "font-size: 15px;"
        );

        ui->showlabel->addWidget(label);
        allTopLabels.append(label);

        // 改按钮样式：天蓝背景 + 白字
        btn->setStyleSheet(
            "QPushButton {"
            "background-color: #26b9bd;"
            "border: 1px solid gray;"
            "border-radius: 10px;"
            "color: white;"
            "font-size: 14px;"
            "}"
            "QPushButton:pressed {"
            "background-color: #e0e0e0;"
            "border: 1px solid #555555;"
            "}"
        );
    }
    else
    {
        // 删除 label
        ui->showlabel->removeWidget(existLabel);
        allTopLabels.removeOne(existLabel);
        existLabel->deleteLater();

        // 恢复按钮原来的样式
        btn->setStyleSheet(
                    "QPushButton {"
                    "background-color: white;"
                    "border: 1px solid gray;"
                    "border-radius: 10px;"
                    "color: black;"
                    "font-size: 14px;"
                    "}"
                    "QPushButton:pressed {"
                    "background-color: #e0e0e0;"
                    "border: 1px solid #555555;"
                    "}"
        );
    }
}

void MainWindow::on_reset_clicked()
{
    // Placeholder:重置动作将由 PR5 UI 整理阶段重新设计
}

// 本次软件启动时，根据上次选择的按钮状态
void MainWindow::loadSelectedButtonsFromIni()
{
    QSettings settings("config.ini", QSettings::IniFormat);
    settings.beginGroup("SelectedButtons");
    QStringList names = settings.value("names").toStringList();
    settings.endGroup();
    QSet<QString> nameSet = QSet<QString>::fromList(names);

    for (QPushButton* btn : allButtons)
    {
        if (!btn)
            continue;

        if (nameSet.contains(btn->objectName()))
        {
            // 确保初始是 false
            btn->setProperty("state", false);
            btn->click();
        }
    }
}
void MainWindow::on_run_clicked()
{
    // Placeholder:Run 动作将由 PR5 UI 整理阶段接入 Session 状态机
}

void MainWindow::on_powerbutton_clicked()
{
    QApplication::quit();
}

void MainWindow::updateFrame(const QImage &img)
{
    pixmapItem->setPixmap(QPixmap::fromImage(img));
    ui->cameraview->fitInView(pixmapItem, Qt::KeepAspectRatio);
}

void MainWindow::uploadOSSPath(const QString& filePath, const int ImgClass)
{
    logMsg = "Upload image to OSS: " + filePath + "ImgClass" + ImgClass;
    LOG_INFO(logMsg);
}

void MainWindow::retryUploadFailedImages()
{
    LOG_INFO("History failed-upload Image reupload.");

    QFile file("failupLoadImage.txt");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    QStringList failedLines;   // 这次仍上传失败的记录

    while (!in.atEnd())
    {
        QString line = in.readLine().trimmed();
        if (line.isEmpty())
            continue;

        QStringList parts = line.split(",");
        if (parts.size() != 2)
        {
            failedLines << line;
            continue;
        }

        QString filePath = parts.at(0);
        bool ok = false;
        int ImgClass = parts.at(1).toInt(&ok);

        if (!ok || !QFile::exists(filePath))
        {
            failedLines << line;
            continue;
        }

//        bool success = ossThread->uploadImage(filePath, ImgClass);

//        if (!success)
//        {
//            // 本次仍然失败，保留
//            failedLines << line;
//        }
    }

    file.close();

    // 覆盖写回失败记录
    QFile outFile("failupLoadImage.txt");
    if (outFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        QTextStream out(&outFile);
        for (const QString& l : failedLines)
        {
            out << l << "\n";
        }
        outFile.close();
    }
}


void MainWindow::on_languageButton_clicked()
{
    try{
    // 切换语言状态
    if (m_currentLanguage == "chinese") // 当前是中文，更改成英文版本
    {
        LOG_INFO("Change to english version");
        m_currentLanguage = "english";
        ui->languageButton->setText(QT_TR_NOOP("英文 / English"));

        m_translator.load("guangxuan_zh_CN.qm");
        qApp->installTranslator(&m_translator);
        ui->retranslateUi(this);

        for (QPushButton* btn : allButtons)
        {
            if (!btn) continue;

            btn->setText(btn->objectName());
        }

    }
    else // 当前是英文，更改成中文版本
    {
        LOG_INFO("Change to chinese version");
        m_currentLanguage = "chinese";
        ui->languageButton->setText(QT_TR_NOOP("简体中文 / Simplified Chinese"));

        m_translator.load("guangxuan_zh_EN.qm");
        qApp->installTranslator(&m_translator);
        ui->retranslateUi(this);

        // 英文切换中文后，物料选择中的按钮的text也需要更换
        applyTextFromIni("colorButtons",      colorButtons);
        applyTextFromIni("categoryButtons",   categoryButtons);
        applyTextFromIni("labelButtons",      labelButtons);
        applyTextFromIni("shapeButtons",      shapeButtons);
        applyTextFromIni("appearanceButtons", appearanceButtons);
    }

    for (QLabel* label : allTopLabels)
        {
            if (!label)
                continue;

            // label 的 objectName，比如：white_
            QString labelName = label->objectName();

            // 去掉末尾的 "_"
            if (!labelName.endsWith("_"))
                continue;

            QString btnObjectName = labelName.left(labelName.length() - 1);

            // 在 allButtons 中找到对应按钮
            for (QPushButton* btn : allButtons)
            {
                if (!btn)
                    continue;

                if (btn->objectName() == btnObjectName)
                {
                    // 同步文本
                    label->setText(btn->text());
                    break;
                }
            }
        }

    // 写回 config.ini
    QSettings settings("config.ini", QSettings::IniFormat);
    settings.setValue("config/language", m_currentLanguage);
    settings.sync();
    }catch(std::exception& e)
    {
        logMsg = "Language change: " + QString::fromStdString(e.what());
        LOG_ERROR(logMsg);
    }
}

void MainWindow::updateLanguageButton()
{
    if (m_currentLanguage == "chinese")
    {
        ui->languageButton->setText(QT_TR_NOOP("简体中文 / Simplified Chinese"));

    }
    else if (m_currentLanguage == "english")
    {
        ui->languageButton->setText(QT_TR_NOOP("英文 / English"));
    }
}

void MainWindow::applyTextFromIni(const QString& groupName, const QList<QPushButton*>& buttons)
{
    QSettings settings("config.ini", QSettings::IniFormat);
    settings.setIniCodec("UTF-8");  // 防止中文乱码

    settings.beginGroup(groupName);

    for (QPushButton* btn : buttons)
    {
        if (!btn)
            continue;

        QString key = btn->objectName();

        if (settings.contains(key))
        {
            QString text = settings.value(key).toString();
            btn->setText(text);
        }
        else
        {
            btn->setText(key);
        }
    }

    settings.endGroup();
}

void MainWindow::on_checkforNew_clicked()
{

    UpdateManager* updater = new UpdateManager(this);

    connect(updater, &UpdateManager::noUpdate, this, [](){
        // 什么都不做，或者提示“已是最新版本”
    });

    connect(updater, &UpdateManager::updateError, this, [](const QString& msg){
        // 打日志即可，工业软件一般不弹窗
    });

//    updater->checkForUpdate();
}

void MainWindow::on_singleControl_triggered()
{
    emit singleControl("01 01 01");
}
void MainWindow::on_multiControl_triggered()
{
    emit batchControl("01 00 15");
}
void MainWindow::on_speedInfo_triggered()
{
    emit requestEncoder();
}
void MainWindow::onEncoderSpeed(const QByteArray& frame)
{
    if (frame.size() < 8)
        return;

    // 协议:第 6、7 字节(0-based)为转速,高字节在前。
    // 经验系数 0.502:单位换算为 m/min。PR3 会将该解析下沉到 BoardWorker。
    quint16 rotation =
        (static_cast<quint8>(frame[6]) << 8) |
         static_cast<quint8>(frame[7]);
    speed = rotation * 0.502;
    QString speed_text = QString::number(int(speed)) + "m/min";
    ui->speed->setText(speed_text);
}

void MainWindow::on_chan1_clicked()
{
    emit tcpRobotSigA();
    ui->chan1->setEnabled(false);
    ui->chan1->setText("等待机械臂连接");

    ui->chan1->setStyleSheet(R"(
    QPushButton {
        background-color: #fbc02d;   /* 黄色 */
        color: #000000;
    }
    )");
}

void MainWindow::chan1_chan(QString ip)
{
    if (ip == "192.168.0.30")
    {
        isBconnected = true;
        ui->chan1->setText("机械臂B已连接");

        ui->chan1->setStyleSheet(R"(
        QPushButton {
            background-color: #43a047;
            color: #000000;
        }
        )");
    }
    if (ip == "192.168.0.20")
    {
        isAconnected = true;
        ui->chan1->setText("机械臂A已连接");

        ui->chan1->setStyleSheet(R"(
        QPushButton {
            background-color: #43a047;
            color: #000000;
        }
        )");
    }
    if (isAconnected && isBconnected)
    {
        ui->chan1->setText("机械臂AB均已连接");

        ui->chan1->setStyleSheet(R"(
        QPushButton {
            background-color: #43a047;
            color: #000000;
        }
        )");
    }

}
