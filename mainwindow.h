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

    QThread* threadPool;

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
};
#endif // MAINWINDOW_H
