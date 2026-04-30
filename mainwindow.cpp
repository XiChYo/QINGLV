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
        threadPool_robot = new QThread;

        // OSS线程
        m_ossThread = new uploadpictoOSS;
        m_ossThread->moveToThread(threadPool);

        // boardcontrol
        boardControl* ctrl = new boardControl;
        ctrl->moveToThread(threadPool);
        connect(threadPool, &QThread::started,
                ctrl, &boardControl::initSerial);

        connect(this, &MainWindow::batchControl,
                ctrl, &boardControl::batchBoardControl);

        connect(this, &MainWindow::requestEncoder,
                ctrl, &boardControl::requestEncoderSpeed);

        connect(ctrl, &boardControl::encoderSpeedReceived,
                this, &MainWindow::onEncoderSpeed);

        // 保存本地文件线程
        m_savelocalpicThread = new saveLocalpic;
        m_savelocalpicThread->moveToThread(threadPool);

        // 摄像头线程
        m_camThread = new camerathread;
        connect(m_savelocalpicThread, &saveLocalpic::forOSSPathSig,
                this, &MainWindow::uploadOSSPath);
        connect(m_camThread, &camerathread::errorMegSig,
                this, &MainWindow::cameraerrorMegSig,Qt::QueuedConnection);
        if (m_camThread->openCamera("192.168.0.20")) {
            LOG_INFO("Camera thread started");
            m_camThread->start();
        }

        m_yolorecogThread = new yolorecognition;
        m_yolorecogThread->moveToThread(threadPool_yolo);

        connect(m_camThread, &camerathread::frameReadySig,
                m_yolorecogThread, &yolorecognition::recognition);

        connect(m_yolorecogThread, &yolorecognition::resultImgSig,
                this, &MainWindow::updateFrame);
        connect(m_yolorecogThread, &yolorecognition::resultImgSig,
                m_savelocalpicThread, &saveLocalpic::saveresultpicture);

        connect(m_yolorecogThread, &yolorecognition::pointSig,
                this, &MainWindow::getAndsendA);

        connect(m_yolorecogThread, &yolorecognition::rawImgSig,
                m_savelocalpicThread, &saveLocalpic::savelocalpicture);


        m_calDistance = new calDistance;
        m_calDistance->moveToThread(threadPool_yolo);
//        connect(m_yolorecogThread, &yolorecognition::ObjPointSig,
//                m_calDistance,&calDistance::distance);

        m_tracker = new ConveyorTracker;
        connect(m_calDistance,&calDistance::readyPoint,
                m_tracker, &ConveyorTracker::addTrackerTask);

        connect(m_tracker, &ConveyorTracker::trackerTaskFinishedSig,
                this, &MainWindow::TrackerTask);

        m_robot = new robotControl;
        m_robot->moveToThread(threadPool_robot);

        m_tcpserver = new tcpforrobot;
        m_tcpserver->moveToThread(threadPool_robot);
        connect(this, &MainWindow::tcpRobotSig,
                m_tcpserver, &tcpforrobot::startServer);
//        connect(m_calDistance,&calDistance::readyPoint,
//                m_tcpserver, &tcpforrobot::sendData);
        connect(m_tcpserver, &tcpforrobot::clientConnected,
                this, &MainWindow::chan1_chan);

        m_running = true;
        m_trackerthread = std::thread([this]()
        {
                while(m_running)
        {
                m_tracker->updateSpeed(speed);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    });

        // 线程池中的线程启动
        threadPool->start();
        threadPool_yolo->start();
        threadPool_robot->start();
    }catch(std::exception& e)
    {
        logMsg = "MainFuc: " + QString::fromStdString(e.what());
        LOG_ERROR(logMsg);
    }
}

MainWindow::~MainWindow()
{
    delete ui;
    m_camThread->stop();  // 关闭相机线程

    if (threadPool) {
        threadPool->quit();   // 通知线程事件循环退出
        threadPool->wait();   // 阻塞等待线程真正结束
        delete threadPool;    // 释放线程对象
        threadPool = nullptr;
    }
    m_running = false;
    if(m_trackerthread.joinable())
    {
        m_trackerthread.join();
    }
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
    // 显示首页，关闭其它页面
    ui->homePage->show();
    ui->materialSelection->hide();
    ui->systemManagement->hide();

    ui->homepage->setStyleSheet("QPushButton {"
                                "border-left: 1px solid #e0e0e0;"   /* 左边框 */
                                "border-right: 1px solid #e0e0e0;"  /* 右边框 */
                                "border-top: none;"              /* 去掉上边框 */
                                "border-bottom: 1px solid #e0e0e0;"           /* 去掉下边框 */
                                "border-radius: 0px;"            /* 去掉四角弧度 */
                                "background-color: rgb(242, 242, 242);"
                                "color: green;"
                                "}"
                                "QPushButton:pressed {"
                                "background-color: #e0e0e0;"
                                "}");
    ui->materialselection->setStyleSheet("QPushButton {"
                                         "border-left: 1px solid #e0e0e0;"   /* 左边框 */
                                         "border-right: 1px solid #e0e0e0;"  /* 右边框 */
                                         "border-top: none;"              /* 去掉上边框 */
                                         "border-bottom: 1px solid #e0e0e0;"           /* 去掉下边框 */
                                         "border-radius: 0px;"            /* 去掉四角弧度 */
                                         "background-color: rgb(242, 242, 242);"
                                         "color: black;"
                                        "}"
                                         "QPushButton:pressed {"
                                         "background-color: #e0e0e0;"
                                         "}");
    ui->systemmanagement->setStyleSheet("QPushButton {"
                                        "border-left: 1px solid #e0e0e0;"   /* 左边框 */
                                        "border-right: 1px solid #e0e0e0;"  /* 右边框 */
                                        "border-top: none;"              /* 去掉上边框 */
                                        "border-bottom: 1px solid #e0e0e0;"           /* 去掉下边框 */
                                        "border-radius: 0px;"            /* 去掉四角弧度 */
                                        "background-color: rgb(242, 242, 242);"
                                        "color: black;"
                                        "}"
                                        "QPushButton:pressed {"
                                        "background-color: #e0e0e0;"
                                        "}");
}

void MainWindow::on_materialselection_clicked()
{
    // 显示物料选择
    ui->homePage->hide();
    ui->materialSelection->show();
    ui->systemManagement->hide();

    ui->homepage->setStyleSheet("QPushButton {"
                                "border-left: 1px solid #e0e0e0;"   /* 左边框 */
                                "border-right: 1px solid #e0e0e0;"  /* 右边框 */
                                "border-top: none;"              /* 去掉上边框 */
                                "border-bottom: 1px solid #e0e0e0;"           /* 去掉下边框 */
                                "border-radius: 0px;"            /* 去掉四角弧度 */
                                "background-color: rgb(242, 242, 242);"
                                "color: black;"
                                "}"
                                "QPushButton:pressed {"
                                "background-color: #e0e0e0;"
                                "}");
    ui->materialselection->setStyleSheet("QPushButton {"
                                         "border-left: 1px solid #e0e0e0;"   /* 左边框 */
                                         "border-right: 1px solid #e0e0e0;"  /* 右边框 */
                                         "border-top: none;"              /* 去掉上边框 */
                                         "border-bottom: 1px solid #e0e0e0;"           /* 去掉下边框 */
                                         "border-radius: 0px;"            /* 去掉四角弧度 */
                                         "background-color: rgb(242, 242, 242);"
                                         "color: green;"
                                         "}"
                                         "QPushButton:pressed {"
                                         "background-color: #e0e0e0;"
                                         "}");
    ui->systemmanagement->setStyleSheet("QPushButton {"
                                        "border-left: 1px solid #e0e0e0;"   /* 左边框 */
                                        "border-right: 1px solid #e0e0e0;"  /* 右边框 */
                                        "border-top: none;"              /* 去掉上边框 */
                                        "border-bottom: 1px solid #e0e0e0;"           /* 去掉下边框 */
                                        "border-radius: 0px;"            /* 去掉四角弧度 */
                                        "background-color: rgb(242, 242, 242);"
                                        "color: black;"
                                        "}"
                                        "QPushButton:pressed {"
                                        "background-color: #e0e0e0;"
                                        "}");
}

void MainWindow::on_systemmanagement_clicked()
{
    // 显示系统管理
    ui->homePage->hide();
    ui->materialSelection->hide();
    ui->systemManagement->show();

    ui->homepage->setStyleSheet("QPushButton {"
                                "border-left: 1px solid #e0e0e0;"   /* 左边框 */
                                "border-right: 1px solid #e0e0e0;"  /* 右边框 */
                                "border-top: none;"              /* 去掉上边框 */
                                "border-bottom: 1px solid #e0e0e0;"           /* 去掉下边框 */
                                "border-radius: 0px;"            /* 去掉四角弧度 */
                                "background-color: rgb(242, 242, 242);"
                                "color: black;"
                                "}"
                                "QPushButton:pressed {"
                                "background-color: #e0e0e0;"
                                "}");
    ui->materialselection->setStyleSheet("QPushButton {"
                                         "border-left: 1px solid #e0e0e0;"   /* 左边框 */
                                         "border-right: 1px solid #e0e0e0;"  /* 右边框 */
                                         "border-top: none;"              /* 去掉上边框 */
                                         "border-bottom: 1px solid #e0e0e0;"           /* 去掉下边框 */
                                         "border-radius: 0px;"            /* 去掉四角弧度 */
                                         "background-color: rgb(242, 242, 242);"
                                         "color: black;"
                                         "}"
                                         "QPushButton:pressed {"
                                         "background-color: #e0e0e0;"
                                         "}");
    ui->systemmanagement->setStyleSheet("QPushButton {"
                                        "border-left: 1px solid #e0e0e0;"   /* 左边框 */
                                        "border-right: 1px solid #e0e0e0;"  /* 右边框 */
                                        "border-top: none;"              /* 去掉上边框 */
                                        "border-bottom: 1px solid #e0e0e0;"           /* 去掉下边框 */
                                        "border-radius: 0px;"            /* 去掉四角弧度 */
                                        "background-color: rgb(242, 242, 242);"
                                        "color: green;"
                                        "}"
                                        "QPushButton:pressed {"
                                        "background-color: #e0e0e0;"
                                        "}");
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
//    // 重置所有按钮状态和样式
//    auto buttons = this->findChildren<QPushButton*>();
//    for (auto *btn : buttons) {
//        if (btn->objectName() == "reset" || btn->objectName() == "run" || btn->objectName() == "homepage" || btn->objectName() == "materialselection" || btn->objectName() == "systemmanagement" || btn->objectName() == "powerbutton" ) continue; // 跳过重置按钮
//        btn->setProperty("state", false);          // 状态归 0
//        btn->setStyleSheet(
//                    "QPushButton {"
//                    "background-color: white;"
//                    "border: 1px solid gray;"
//                    "border-radius: 10px;"
//                    "color: black;"
//                    "font-size: 14px;"
//                    "}"
//                    "QPushButton:pressed {"
//                    "background-color: #e0e0e0;"
//                    "border: 1px solid #555555;"
//                    "}"); // 样式恢复默认
//    }

//    QLayout *layout = ui->showlabel;

//    // 遍历 layout 中的所有子控件
//    while (QLayoutItem* item = layout->takeAt(0)) {
//        if (QWidget *widget = item->widget()) {
//            QLabel *label = qobject_cast<QLabel*>(widget);
//            if (label) {
//                label->deleteLater();  // 删除 label
//            }
//        }
//        delete item; // 删除 layoutItem
//    }
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
//    LOG_INFO("Program started");

//    QGridLayout *layout = qobject_cast<QGridLayout*>(ui->showchat->layout());
//    if (!layout) return;

//    //  清空 grid layout 中原来的控件
//    QLayoutItem *item;
//    while ((item = layout->takeAt(0)) != nullptr) {
//        if (item->widget())
//            delete item->widget();
//        delete item;
//    }
//    progressBars.clear(); // 清空之前的 progressbar 列表
//    int row = 0;  // grid layout 行号
//    int totalValue = 0; // 累加所有进度条 value

//    // 进度条的颜色列表
//    QStringList colors = {
//        "#ffc000", "#ffff00", "#92d050", "#ffc000",
//        "#26b9bd", "#00b0f0", "#0070c0", "#7030a0",
//        "#ff55ff", "#550000", "#ff0000", "#ff007f"
//    };
//    int colorIndex = 0;

//    // 记录按钮点击状态，让软件下次启动的时候可以记录状态
//    QStringList selectedNames;

//    for (QPushButton* btn : allButtons)
//    {
//        if (!btn)
//            continue;

//        if (btn->property("state").toBool())
//        {
//            selectedNames << btn->objectName();
//        }
//    }
//    LOG_INFO("Clicked:" + selectedNames.join(", "));

//    QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
//    QSettings settings(configPath, QSettings::IniFormat);
//    settings.beginGroup("SelectedButtons");
//    settings.setValue("names", selectedNames);
//    settings.endGroup();
//    settings.sync();

//    // 遍历 categoryButtons
//    for (QPushButton* btn : categoryButtons)
//    {
//        if (!btn->property("state").toBool())
//            continue;

//        // 左侧文本
//        QLabel* label = new QLabel(btn->text(), ui->showchat);
//        label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
//        label->setStyleSheet("color: rgb(255, 255, 255); font-size: 10pt;");

//        // 中间竖线
//        QFrame* vline = new QFrame(ui->showchat);
//        vline->setFrameShape(QFrame::VLine);
//        vline->setFrameShadow(QFrame::Sunken);
//        vline->setStyleSheet("background-color: rgb(255, 255, 255);");
//        vline->setMaximumWidth(1);

//        // 右侧进度条
//        QProgressBar* bar = new QProgressBar(ui->showchat);
//        bar->setMaximum(INT_MAX);

//        // 随机生成 value
//        int randomValue = QRandomGenerator::global()->bounded(10001); // 0~10000
//        bar->setValue(randomValue);
//        totalValue += randomValue;

//        bar->setAlignment(Qt::AlignRight);
//        bar->setFormat("%v");

//        // 设置 QSS
//        QString color = colors[colorIndex % colors.size()];
//        bar->setStyleSheet(QString(
//            "QProgressBar {"
//            "border: none;"
//            "background: transparent;"
//            "color: rgb(255, 255, 255);"
//            "font-size: 11pt;"
//            "}"
//            "QProgressBar::chunk {"
//            "border: none;"
//            "background-color: %1;"
//            "margin: 0px;"
//            "}"
//        ).arg(color));
//        colorIndex++;

//        // 保存 bar 的 objectName
//        bar->setObjectName(btn->objectName() + "_bar");

//        // 保存裸指针（Qt 管理生命周期）
//        progressBars.append(bar);

//        // 加入布局
//        layout->addWidget(label, row, 0);
//        layout->addWidget(vline, row, 1);
//        layout->addWidget(bar, row, 2);

//        row++;
//    }

//    for (QProgressBar *bar : progressBars) {
//        bar->setMaximum(totalValue);
//    }
}

void MainWindow::on_powerbutton_clicked()
{
    QApplication::quit();
//    m_savelocalpicThread -> testint = 1;
//    m_tracker.addTrackerTask(1.75);
//    // 弹出询问框
//    QMessageBox::StandardButton reply;
//    reply = QMessageBox::question(this, "关机确认", "是否要关机？",
//                                  QMessageBox::Yes | QMessageBox::No);
//    try{
//    if (reply == QMessageBox::Yes) {
//        LOG_INFO("Shutdown command executed");
//        // 用户选择 Yes，执行关机命令
//        // 测试的时候先注释掉，以免误点关机。。。
////        QProcess::execute("shutdown /s /t 0"); // Windows 立即关机
////        QProcess::startDetached("shutdown", QStringList() << "-h" << "now"); // Linux 立即关机
//    }}catch(std::exception& e)
//    {
//        logMsg = "powerbtn: " + QString::fromStdString(e.what());
//        LOG_ERROR(logMsg);
//    }
}

void MainWindow::updateFrame(const QImage &img)
{
    pixmapItem->setPixmap(QPixmap::fromImage(img));
    ui->cameraview->fitInView(pixmapItem, Qt::KeepAspectRatio);
//    emit yoloImg(img);
}

void MainWindow::uploadOSSPath(const QString& filePath, const int ImgClass)
{
    logMsg = "Upload image to OSS: " + filePath + "ImgClass" + ImgClass;
    LOG_INFO(logMsg);

//    uploadOssSorF = m_ossThread->uploadImage(filePath, ImgClass);
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

//        bool success = m_ossThread->uploadImage(filePath, ImgClass);

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

void MainWindow::on_speedInfo_triggered()
{
    emit requestEncoder();
}
void MainWindow::onEncoderSpeed(const QByteArray& frame)
{
    // frame 是原始串口数据
//    qDebug() << "onEncoderSpeed Encoder raw:" << frame.toHex(' ').toUpper();

    if (frame.size() < 8)
        return;

    // 假设：第 5、6 字节是速度，高字节在前
    quint16 rotation =
        (static_cast<quint8>(frame[6]) << 8) |
         static_cast<quint8>(frame[7]);
    speed = rotation * 0.502;
    QString speed_text = QString::number(int(speed)) + "m/min";
//    m_camThread->captureIntervalMs = 1000;

//    if (speed < 1)
//    {
//        m_camThread->captureIntervalMs = 500;  // ms
//    }else {
//        m_camThread->captureIntervalMs = (1.00 / (speed/60)) * 1000 / 3;  // ms
//    }

//    qDebug()<< "m_camThread->captureIntervalMs"<<m_camThread->captureIntervalMs;

    ui->speed->setText(speed_text);
}

void MainWindow::on_chan1_clicked()
{
    emit tcpRobotSig();
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


void MainWindow::TrackerTask(Task task)
{
    qDebug() << "----------------------";
    // 1️判空
    if (task.cmds.empty())
    {
        qDebug() << "Task" << task.id << "no commands";
        return;
    }

    // 2️遍历发送
    for (const auto& cmd : task.cmds)
    {
        uint8_t valve = cmd.valveId;
        uint8_t high  = (cmd.mask >> 8) & 0xFF;
        uint8_t low   = cmd.mask & 0xFF;

        QString cmdStr = QString("%1 %2 %3")
                .arg(valve, 2, 16, QChar('0'))
                .arg(high, 2, 16, QChar('0'))
                .arg(low, 2, 16, QChar('0'))
                .toUpper();

        qDebug() << "Task" << task.id << "Send:" << cmdStr;

        emit batchControl(cmdStr);   //发送
    }
}
void MainWindow::getAndsendA(int x)
{

    if (m_tcpserver->isConnected("192.168.0.30") || m_tcpserver->isConnected("192.168.0.20"))
    {
        qDebug()<<"X:"<<x;


        int index;

        float grapPos = - ((2048 - x) / 2048.0f) * 560 - 57.5;   // Xmax = 800



        float mTime = 3500;

        int baseTime;

        if(!isBBusy && m_tcpserver->isConnected("192.168.0.20")){
            if (grapPos < -300)
            {
                grapPos -= ui->less300->text().toInt();
            }
            baseTime = ui->mTimeLabel->text().toInt();

            if (grapPos >= -150)
            {
                index = 0;
            }
            else
            {
                index = (int)((-grapPos - 150) / ui->aEnd->text().toInt()) + 1;
            }
            mTime = baseTime - index * ui->aMoreTime->text().toInt();

            QString str = QString("\n%1,0,0\n").arg(grapPos);
            QByteArray grapPosdata = str.toUtf8();

            qDebug()<<"---------------use B---------------";
            qDebug()<<"grapPos:"<<grapPos;
            qDebug()<<"mTime:"<<mTime;
            isBBusy = true;

            QTimer::singleShot(mTime, this, [this, grapPosdata, mTime]() {
                qDebug()<<"send BBB:"<<grapPosdata;
                m_tcpserver->sendToIP("192.168.0.20", grapPosdata);
            });

            QTimer::singleShot(10000, this, [this]() {
                isBBusy = false;
                qDebug() << "----B released----";
            });
        }else if (!isABusy && m_tcpserver->isConnected("192.168.0.30"))
        {
            if (grapPos < -300)
            {
                grapPos -= 30;
            }
            baseTime = 3500;
            if (grapPos <= -350)
            {
                grapPos = -350;
            }

            if (grapPos <= 0 && grapPos >= -150)
            {
                mTime = ui->l0->text().toInt();
            }else if(grapPos < -150 && grapPos >= -180)
            {
                mTime = ui->l150->text().toInt();
            }else if(grapPos < -180 && grapPos >= -210)
            {
                mTime = ui->l180->text().toInt();
            }else if(grapPos < -210 && grapPos >= -240)
            {
                mTime = ui->l210->text().toInt();
            }else if(grapPos < -240 && grapPos >= -270)
            {
                mTime = ui->l240->text().toInt();
            }else if(grapPos < -270 && grapPos >= -300)
            {
                mTime = ui->l270->text().toInt();
            }else if(grapPos < -300 && grapPos >= -330)
            {
                mTime = ui->l300->text().toInt();
            }else if(grapPos < -330 && grapPos >= -380)
            {
                mTime = ui->l330->text().toInt();
            }


            QString str = QString("\n%1,0,0\n").arg(grapPos);
            QByteArray grapPosdata = str.toUtf8();
            qDebug()<<"---------------use A---------------";
            qDebug()<<"grapPos:"<<grapPos;
            qDebug()<<"mTime:"<<mTime;
            isABusy = true;

            QTimer::singleShot(mTime, this, [this, grapPosdata, mTime]() {
                qDebug()<<"send AAA:"<<grapPosdata;
                m_tcpserver->sendToIP("192.168.0.30", grapPosdata);
            });

            QTimer::singleShot(5200, this, [this]() {
                isABusy = false;
                qDebug() << "----A released----";
            });
        }
    }
}

void MainWindow::on_addmTime_clicked()
{
    QString text = QString::number(ui->mTimeLabel->text().toInt() + 100);
    ui->mTimeLabel->setText(text);
}

void MainWindow::on_lessmTime_clicked()
{
    QString text = QString::number(ui->mTimeLabel->text().toInt() - 100);
    ui->mTimeLabel->setText(text);
}


void MainWindow::on_u0_clicked()
{
    QString text = QString::number(ui->l0->text().toInt() + 10);
    ui->l0->setText(text);
}

void MainWindow::on_d0_clicked()
{
    QString text = QString::number(ui->l0->text().toInt() - 10);
    ui->l0->setText(text);
}

void MainWindow::on_u150_clicked()
{
    QString text = QString::number(ui->l150->text().toInt() + 10);
    ui->l150->setText(text);
}

void MainWindow::on_d150_clicked()
{
    QString text = QString::number(ui->l150->text().toInt() - 10);
    ui->l150->setText(text);
}

void MainWindow::on_u180_clicked()
{
    QString text = QString::number(ui->l180->text().toInt() + 10);
    ui->l180->setText(text);
}

void MainWindow::on_d180_clicked()
{
    QString text = QString::number(ui->l180->text().toInt() - 10);
    ui->l180->setText(text);
}

void MainWindow::on_u210_clicked()
{
    QString text = QString::number(ui->l210->text().toInt() + 10);
    ui->l210->setText(text);
}

void MainWindow::on_d210_clicked()
{
    QString text = QString::number(ui->l210->text().toInt() - 10);
    ui->l210->setText(text);
}

void MainWindow::on_u240_clicked()
{
    QString text = QString::number(ui->l240->text().toInt() + 10);
    ui->l240->setText(text);
}

void MainWindow::on_d240_clicked()
{
    QString text = QString::number(ui->l240->text().toInt() - 10);
    ui->l240->setText(text);
}

void MainWindow::on_u270_clicked()
{
    QString text = QString::number(ui->l270->text().toInt() + 10);
    ui->l270->setText(text);
}

void MainWindow::on_d270_clicked()
{
    QString text = QString::number(ui->l270->text().toInt() - 10);
    ui->l270->setText(text);
}

void MainWindow::on_u300_clicked()
{
    QString text = QString::number(ui->l300->text().toInt() + 10);
    ui->l300->setText(text);
}

void MainWindow::on_d300_clicked()
{
    QString text = QString::number(ui->l300->text().toInt() - 10);
    ui->l300->setText(text);
}

void MainWindow::on_u330_clicked()
{
    QString text = QString::number(ui->l330->text().toInt() + 10);
    ui->l330->setText(text);
}

void MainWindow::on_d330_clicked()
{
    QString text = QString::number(ui->l330->text().toInt() - 10);
    ui->l330->setText(text);
}


void MainWindow::on_auEnd_clicked()
{
    QString text = QString::number(ui->aEnd->text().toInt() + 5);
    ui->aEnd->setText(text);
}

void MainWindow::on_adEnd_clicked()
{
    QString text = QString::number(ui->aEnd->text().toInt() - 5);
    ui->aEnd->setText(text);
}

void MainWindow::on_auMoreTime_clicked()
{
    QString text = QString::number(ui->aMoreTime->text().toInt() + 5);
    ui->aMoreTime->setText(text);
}

void MainWindow::on_adMoreTime_clicked()
{
    QString text = QString::number(ui->aMoreTime->text().toInt() - 5);
    ui->aMoreTime->setText(text);
}

void MainWindow::on_aless300_clicked()
{
    QString text = QString::number(ui->less300->text().toInt() + 5);
    ui->less300->setText(text);
}

void MainWindow::on_dless300_clicked()
{
    QString text = QString::number(ui->less300->text().toInt() - 5);
    ui->less300->setText(text);
}

void MainWindow::on_b1_clicked()
{
    emit batchControl("01 01 FF");
}

void MainWindow::on_b2_clicked()
{
    emit batchControl("02 01 FF");
}

void MainWindow::on_b3_clicked()
{
    emit batchControl("03 01 FF");
}

void MainWindow::on_b4_clicked()
{
    emit batchControl("04 01 FF");
}

void MainWindow::on_b5_clicked()
{
    emit batchControl("05 01 FF");
}

void MainWindow::on_b6_clicked()
{
    emit batchControl("06 01 FF");
}

void MainWindow::on_b7_clicked()
{
    emit batchControl("07 01 FF");
}

void MainWindow::on_b8_clicked()
{
    emit batchControl("08 01 FF");
}
