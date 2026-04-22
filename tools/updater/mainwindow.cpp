#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "updater.h"

#include <QProgressBar>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    resize(400, 120);
    setWindowTitle("Updating...");

    QWidget* central = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(central);

    m_statusLabel = new QLabel("Initializing...", this);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);

    layout->addWidget(m_statusLabel);
    layout->addWidget(m_progressBar);

    setCentralWidget(central);

    m_updater = new Updater(this);

    if (!m_updater->initFromArgs())
        close();

    connect(m_updater, &Updater::progressChanged,
            m_progressBar, &QProgressBar::setValue);

    connect(m_updater, &Updater::statusChanged,
            m_statusLabel, &QLabel::setText);

    connect(m_updater, &Updater::finished, this, [this](bool){
        close();
    });

    // 不阻塞 UI，延迟启动
    QTimer::singleShot(0, m_updater, &Updater::start);
}

MainWindow::~MainWindow()
{
    delete ui;
}

