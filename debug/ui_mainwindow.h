/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 5.14.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGraphicsView>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QWidget *centralwidget;
    QVBoxLayout *verticalLayout;
    QWidget *widget_5;
    QHBoxLayout *horizontalLayout_2;
    QLabel *label_3;
    QLabel *label;
    QLabel *dateTime;
    QPushButton *powerbutton;
    QWidget *widget_6;
    QHBoxLayout *horizontalLayout_4;
    QLabel *label_4;
    QHBoxLayout *showlabel;
    QSpacerItem *horizontalSpacer_4;
    QWidget *mainWindow;
    QWidget *materialSelection;
    QVBoxLayout *verticalLayout_2;
    QHBoxLayout *horizontalLayout_7;
    QLabel *label_6;
    QLabel *label_7;
    QGridLayout *gridLayout;
    QLabel *label_8;
    QLabel *label_9;
    QLabel *label_10;
    QLabel *label_11;
    QFrame *Hline4;
    QFrame *line_4;
    QFrame *Hline3;
    QFrame *line_2;
    QLabel *label_21;
    QFrame *line_3;
    QFrame *line;
    QFrame *Hline5;
    QFrame *Hline2;
    QLabel *label_12;
    QFrame *line_5;
    QFrame *Hline1;
    QGridLayout *gridLayout_4;
    QSpacerItem *horizontalSpacer;
    QSpacerItem *horizontalSpacer_3;
    QSpacerItem *horizontalSpacer_2;
    QPushButton *reset;
    QPushButton *run;
    QSpacerItem *verticalSpacer_4;
    QWidget *systemManagement;
    QGridLayout *gridLayout_6;
    QSpacerItem *verticalSpacer;
    QLabel *label_15;
    QLabel *label_18;
    QLabel *label_20;
    QLabel *label_2;
    QLabel *label_19;
    QLabel *label_17;
    QSpacerItem *verticalSpacer_2;
    QLabel *label_13;
    QLabel *label_14;
    QLabel *label_16;
    QPushButton *languageButton;
    QGridLayout *gridLayout_5;
    QWidget *homePage;
    QHBoxLayout *horizontalLayout_8;
    QGraphicsView *cameraview;
    QWidget *showchat;
    QGridLayout *gridLayout_2;
    QWidget *buttonlist;
    QHBoxLayout *horizontalLayout_5;
    QPushButton *homepage;
    QPushButton *materialselection;
    QPushButton *systemmanagement;
    QStatusBar *statusbar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName(QString::fromUtf8("MainWindow"));
        MainWindow->resize(881, 605);
        QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(MainWindow->sizePolicy().hasHeightForWidth());
        MainWindow->setSizePolicy(sizePolicy);
        MainWindow->setStyleSheet(QString::fromUtf8(""));
        centralwidget = new QWidget(MainWindow);
        centralwidget->setObjectName(QString::fromUtf8("centralwidget"));
        QSizePolicy sizePolicy1(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sizePolicy1.setHorizontalStretch(1);
        sizePolicy1.setVerticalStretch(1);
        sizePolicy1.setHeightForWidth(centralwidget->sizePolicy().hasHeightForWidth());
        centralwidget->setSizePolicy(sizePolicy1);
        verticalLayout = new QVBoxLayout(centralwidget);
        verticalLayout->setSpacing(0);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        widget_5 = new QWidget(centralwidget);
        widget_5->setObjectName(QString::fromUtf8("widget_5"));
        sizePolicy.setHeightForWidth(widget_5->sizePolicy().hasHeightForWidth());
        widget_5->setSizePolicy(sizePolicy);
        widget_5->setMinimumSize(QSize(881, 51));
        widget_5->setMaximumSize(QSize(16777215, 51));
        widget_5->setStyleSheet(QString::fromUtf8("background-color: rgb(255, 255, 255);"));
        horizontalLayout_2 = new QHBoxLayout(widget_5);
        horizontalLayout_2->setSpacing(2);
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        horizontalLayout_2->setContentsMargins(0, 0, 0, 0);
        label_3 = new QLabel(widget_5);
        label_3->setObjectName(QString::fromUtf8("label_3"));
        QSizePolicy sizePolicy2(QSizePolicy::Minimum, QSizePolicy::Minimum);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(label_3->sizePolicy().hasHeightForWidth());
        label_3->setSizePolicy(sizePolicy2);
        label_3->setMinimumSize(QSize(131, 51));
        label_3->setMaximumSize(QSize(131, 51));
        label_3->setPixmap(QPixmap(QString::fromUtf8(":/new/prefix/logo.png")));
        label_3->setScaledContents(true);

        horizontalLayout_2->addWidget(label_3);

        label = new QLabel(widget_5);
        label->setObjectName(QString::fromUtf8("label"));
        sizePolicy.setHeightForWidth(label->sizePolicy().hasHeightForWidth());
        label->setSizePolicy(sizePolicy);
        label->setMinimumSize(QSize(491, 0));
        label->setMaximumSize(QSize(16777215, 51));
        QFont font;
        font.setFamily(QString::fromUtf8("\345\276\256\350\275\257\351\233\205\351\273\221"));
        font.setPointSize(16);
        font.setBold(true);
        font.setWeight(75);
        label->setFont(font);
        label->setStyleSheet(QString::fromUtf8("background-color: rgb(255, 255, 255);"));
        label->setAlignment(Qt::AlignCenter);

        horizontalLayout_2->addWidget(label);

        dateTime = new QLabel(widget_5);
        dateTime->setObjectName(QString::fromUtf8("dateTime"));
        sizePolicy.setHeightForWidth(dateTime->sizePolicy().hasHeightForWidth());
        dateTime->setSizePolicy(sizePolicy);
        dateTime->setMinimumSize(QSize(131, 51));
        dateTime->setMaximumSize(QSize(131, 51));
        QFont font1;
        font1.setPointSize(11);
        dateTime->setFont(font1);
        dateTime->setText(QString::fromUtf8(""));
        dateTime->setAlignment(Qt::AlignCenter);

        horizontalLayout_2->addWidget(dateTime);

        powerbutton = new QPushButton(widget_5);
        powerbutton->setObjectName(QString::fromUtf8("powerbutton"));
        sizePolicy.setHeightForWidth(powerbutton->sizePolicy().hasHeightForWidth());
        powerbutton->setSizePolicy(sizePolicy);
        powerbutton->setMinimumSize(QSize(101, 51));
        powerbutton->setMaximumSize(QSize(101, 60));
        powerbutton->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"    background-color: white;        /* \350\203\214\346\231\257\347\231\275\350\211\262 */\n"
"    border: 0px solid gray;  /* \346\265\205\347\201\260\350\211\262\350\276\271\346\241\206 */\n"
"    border-radius: 0px;            /* \345\233\233\344\270\252\350\247\222\345\234\206\350\247\222\345\215\212\345\276\204 10px\357\274\214\345\217\257\350\260\203 */\n"
"    color: black;                   /* \346\226\207\345\255\227\351\242\234\350\211\262 */\n"
"    font-size: 14px;                /* \345\255\227\344\275\223\345\244\247\345\260\217 */\n"
"}\n"
"QPushButton:pressed {\n"
"    background-color: #e0e0e0;     /* \346\214\211\344\270\213\350\203\214\346\231\257\346\233\264\346\267\261 */\n"
"    border: 0px solid #555555;     /* \346\214\211\344\270\213\350\276\271\346\241\206\351\242\234\350\211\262\345\217\230\346\232\227 */\n"
"}"));
        QIcon icon;
        icon.addFile(QString::fromUtf8(":/new/prefix/shutdown.png"), QSize(), QIcon::Normal, QIcon::Off);
        powerbutton->setIcon(icon);
        powerbutton->setIconSize(QSize(45, 45));

        horizontalLayout_2->addWidget(powerbutton);

        label->raise();
        powerbutton->raise();
        dateTime->raise();
        label_3->raise();

        verticalLayout->addWidget(widget_5);

        widget_6 = new QWidget(centralwidget);
        widget_6->setObjectName(QString::fromUtf8("widget_6"));
        sizePolicy.setHeightForWidth(widget_6->sizePolicy().hasHeightForWidth());
        widget_6->setSizePolicy(sizePolicy);
        widget_6->setMinimumSize(QSize(881, 61));
        widget_6->setMaximumSize(QSize(16777215, 61));
        widget_6->setStyleSheet(QString::fromUtf8("background-color: rgb(254, 246, 240);"));
        horizontalLayout_4 = new QHBoxLayout(widget_6);
        horizontalLayout_4->setSpacing(0);
        horizontalLayout_4->setObjectName(QString::fromUtf8("horizontalLayout_4"));
        horizontalLayout_4->setContentsMargins(0, 0, 0, 0);
        label_4 = new QLabel(widget_6);
        label_4->setObjectName(QString::fromUtf8("label_4"));
        sizePolicy.setHeightForWidth(label_4->sizePolicy().hasHeightForWidth());
        label_4->setSizePolicy(sizePolicy);
        label_4->setMinimumSize(QSize(140, 0));
        label_4->setMaximumSize(QSize(140, 16777215));
        QFont font2;
        font2.setFamily(QString::fromUtf8("\345\276\256\350\275\257\351\233\205\351\273\221"));
        font2.setPointSize(10);
        font2.setBold(true);
        font2.setWeight(75);
        label_4->setFont(font2);
        label_4->setStyleSheet(QString::fromUtf8("background-color: rgb(254, 246, 240);\n"
"color: rgb(138, 0, 0);"));

        horizontalLayout_4->addWidget(label_4);

        showlabel = new QHBoxLayout();
        showlabel->setSpacing(3);
        showlabel->setObjectName(QString::fromUtf8("showlabel"));

        horizontalLayout_4->addLayout(showlabel);

        horizontalSpacer_4 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer_4);


        verticalLayout->addWidget(widget_6);

        mainWindow = new QWidget(centralwidget);
        mainWindow->setObjectName(QString::fromUtf8("mainWindow"));
        sizePolicy.setHeightForWidth(mainWindow->sizePolicy().hasHeightForWidth());
        mainWindow->setSizePolicy(sizePolicy);
        mainWindow->setMinimumSize(QSize(881, 421));
        materialSelection = new QWidget(mainWindow);
        materialSelection->setObjectName(QString::fromUtf8("materialSelection"));
        materialSelection->setGeometry(QRect(0, 0, 881, 421));
        sizePolicy.setHeightForWidth(materialSelection->sizePolicy().hasHeightForWidth());
        materialSelection->setSizePolicy(sizePolicy);
        materialSelection->setMinimumSize(QSize(881, 421));
        materialSelection->setStyleSheet(QString::fromUtf8("background-color: rgb(255, 255, 255);"));
        verticalLayout_2 = new QVBoxLayout(materialSelection);
        verticalLayout_2->setSpacing(0);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        verticalLayout_2->setContentsMargins(0, 0, 0, 0);
        horizontalLayout_7 = new QHBoxLayout();
        horizontalLayout_7->setSpacing(0);
        horizontalLayout_7->setObjectName(QString::fromUtf8("horizontalLayout_7"));
        label_6 = new QLabel(materialSelection);
        label_6->setObjectName(QString::fromUtf8("label_6"));
        sizePolicy.setHeightForWidth(label_6->sizePolicy().hasHeightForWidth());
        label_6->setSizePolicy(sizePolicy);
        label_6->setMinimumSize(QSize(135, 31));
        label_6->setMaximumSize(QSize(135, 16777215));
        QFont font3;
        font3.setPointSize(11);
        font3.setBold(true);
        font3.setWeight(75);
        label_6->setFont(font3);
        label_6->setStyleSheet(QString::fromUtf8("background-color: rgb(242, 242, 242);"));
        label_6->setAlignment(Qt::AlignCenter);

        horizontalLayout_7->addWidget(label_6);

        label_7 = new QLabel(materialSelection);
        label_7->setObjectName(QString::fromUtf8("label_7"));
        sizePolicy.setHeightForWidth(label_7->sizePolicy().hasHeightForWidth());
        label_7->setSizePolicy(sizePolicy);
        label_7->setMinimumSize(QSize(792, 31));
        label_7->setMaximumSize(QSize(16777215, 16777215));
        label_7->setFont(font3);
        label_7->setStyleSheet(QString::fromUtf8("background-color: rgb(242, 242, 242);"));
        label_7->setAlignment(Qt::AlignCenter);

        horizontalLayout_7->addWidget(label_7);


        verticalLayout_2->addLayout(horizontalLayout_7);

        gridLayout = new QGridLayout();
        gridLayout->setSpacing(5);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        label_8 = new QLabel(materialSelection);
        label_8->setObjectName(QString::fromUtf8("label_8"));
        sizePolicy.setHeightForWidth(label_8->sizePolicy().hasHeightForWidth());
        label_8->setSizePolicy(sizePolicy);
        label_8->setMinimumSize(QSize(83, 0));
        label_8->setMaximumSize(QSize(135, 16777215));
        label_8->setFont(font1);
        label_8->setStyleSheet(QString::fromUtf8(""));
        label_8->setAlignment(Qt::AlignCenter);

        gridLayout->addWidget(label_8, 1, 0, 1, 1);

        label_9 = new QLabel(materialSelection);
        label_9->setObjectName(QString::fromUtf8("label_9"));
        sizePolicy.setHeightForWidth(label_9->sizePolicy().hasHeightForWidth());
        label_9->setSizePolicy(sizePolicy);
        label_9->setMinimumSize(QSize(83, 0));
        label_9->setMaximumSize(QSize(135, 16777215));
        label_9->setFont(font1);
        label_9->setStyleSheet(QString::fromUtf8(""));
        label_9->setAlignment(Qt::AlignCenter);

        gridLayout->addWidget(label_9, 3, 0, 1, 1);

        label_10 = new QLabel(materialSelection);
        label_10->setObjectName(QString::fromUtf8("label_10"));
        sizePolicy.setHeightForWidth(label_10->sizePolicy().hasHeightForWidth());
        label_10->setSizePolicy(sizePolicy);
        label_10->setMinimumSize(QSize(83, 0));
        label_10->setMaximumSize(QSize(135, 16777215));
        label_10->setFont(font1);
        label_10->setStyleSheet(QString::fromUtf8(""));
        label_10->setAlignment(Qt::AlignCenter);

        gridLayout->addWidget(label_10, 6, 0, 1, 1);

        label_11 = new QLabel(materialSelection);
        label_11->setObjectName(QString::fromUtf8("label_11"));
        sizePolicy.setHeightForWidth(label_11->sizePolicy().hasHeightForWidth());
        label_11->setSizePolicy(sizePolicy);
        label_11->setMinimumSize(QSize(83, 0));
        label_11->setMaximumSize(QSize(135, 16777215));
        label_11->setFont(font1);
        label_11->setStyleSheet(QString::fromUtf8(""));
        label_11->setAlignment(Qt::AlignCenter);

        gridLayout->addWidget(label_11, 8, 0, 1, 1);

        Hline4 = new QFrame(materialSelection);
        Hline4->setObjectName(QString::fromUtf8("Hline4"));
        QSizePolicy sizePolicy3(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sizePolicy3.setHorizontalStretch(0);
        sizePolicy3.setVerticalStretch(0);
        sizePolicy3.setHeightForWidth(Hline4->sizePolicy().hasHeightForWidth());
        Hline4->setSizePolicy(sizePolicy3);
        Hline4->setMinimumSize(QSize(0, 0));
        Hline4->setStyleSheet(QString::fromUtf8("background-color: rgb(217, 217, 217);"));
        Hline4->setFrameShape(QFrame::HLine);
        Hline4->setFrameShadow(QFrame::Sunken);

        gridLayout->addWidget(Hline4, 9, 0, 1, 3);

        line_4 = new QFrame(materialSelection);
        line_4->setObjectName(QString::fromUtf8("line_4"));
        line_4->setFrameShape(QFrame::VLine);
        line_4->setFrameShadow(QFrame::Sunken);

        gridLayout->addWidget(line_4, 8, 1, 1, 1);

        Hline3 = new QFrame(materialSelection);
        Hline3->setObjectName(QString::fromUtf8("Hline3"));
        sizePolicy3.setHeightForWidth(Hline3->sizePolicy().hasHeightForWidth());
        Hline3->setSizePolicy(sizePolicy3);
        Hline3->setMinimumSize(QSize(0, 0));
        Hline3->setStyleSheet(QString::fromUtf8("background-color: rgb(217, 217, 217);"));
        Hline3->setFrameShape(QFrame::HLine);
        Hline3->setFrameShadow(QFrame::Sunken);

        gridLayout->addWidget(Hline3, 7, 0, 1, 3);

        line_2 = new QFrame(materialSelection);
        line_2->setObjectName(QString::fromUtf8("line_2"));
        line_2->setFrameShape(QFrame::VLine);
        line_2->setFrameShadow(QFrame::Sunken);

        gridLayout->addWidget(line_2, 3, 1, 2, 1);

        label_21 = new QLabel(materialSelection);
        label_21->setObjectName(QString::fromUtf8("label_21"));
        label_21->setMinimumSize(QSize(83, 0));
        label_21->setMaximumSize(QSize(135, 16777215));
        label_21->setFont(font1);

        gridLayout->addWidget(label_21, 4, 0, 1, 1);

        line_3 = new QFrame(materialSelection);
        line_3->setObjectName(QString::fromUtf8("line_3"));
        line_3->setFrameShape(QFrame::VLine);
        line_3->setFrameShadow(QFrame::Sunken);

        gridLayout->addWidget(line_3, 6, 1, 1, 1);

        line = new QFrame(materialSelection);
        line->setObjectName(QString::fromUtf8("line"));
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Sunken);

        gridLayout->addWidget(line, 1, 1, 1, 1);

        Hline5 = new QFrame(materialSelection);
        Hline5->setObjectName(QString::fromUtf8("Hline5"));
        sizePolicy3.setHeightForWidth(Hline5->sizePolicy().hasHeightForWidth());
        Hline5->setSizePolicy(sizePolicy3);
        Hline5->setMinimumSize(QSize(0, 0));
        Hline5->setStyleSheet(QString::fromUtf8("background-color: rgb(217, 217, 217);"));
        Hline5->setFrameShape(QFrame::HLine);
        Hline5->setFrameShadow(QFrame::Sunken);

        gridLayout->addWidget(Hline5, 11, 0, 1, 3);

        Hline2 = new QFrame(materialSelection);
        Hline2->setObjectName(QString::fromUtf8("Hline2"));
        sizePolicy3.setHeightForWidth(Hline2->sizePolicy().hasHeightForWidth());
        Hline2->setSizePolicy(sizePolicy3);
        Hline2->setMinimumSize(QSize(0, 0));
        Hline2->setStyleSheet(QString::fromUtf8("background-color: rgb(217, 217, 217);"));
        Hline2->setFrameShape(QFrame::HLine);
        Hline2->setFrameShadow(QFrame::Sunken);

        gridLayout->addWidget(Hline2, 5, 0, 1, 3);

        label_12 = new QLabel(materialSelection);
        label_12->setObjectName(QString::fromUtf8("label_12"));
        sizePolicy.setHeightForWidth(label_12->sizePolicy().hasHeightForWidth());
        label_12->setSizePolicy(sizePolicy);
        label_12->setMinimumSize(QSize(83, 0));
        label_12->setMaximumSize(QSize(135, 16777215));
        label_12->setFont(font1);
        label_12->setStyleSheet(QString::fromUtf8(""));
        label_12->setAlignment(Qt::AlignCenter);

        gridLayout->addWidget(label_12, 10, 0, 1, 1);

        line_5 = new QFrame(materialSelection);
        line_5->setObjectName(QString::fromUtf8("line_5"));
        line_5->setFrameShape(QFrame::VLine);
        line_5->setFrameShadow(QFrame::Sunken);

        gridLayout->addWidget(line_5, 10, 1, 1, 1);

        Hline1 = new QFrame(materialSelection);
        Hline1->setObjectName(QString::fromUtf8("Hline1"));
        QSizePolicy sizePolicy4(QSizePolicy::Minimum, QSizePolicy::Fixed);
        sizePolicy4.setHorizontalStretch(99);
        sizePolicy4.setVerticalStretch(0);
        sizePolicy4.setHeightForWidth(Hline1->sizePolicy().hasHeightForWidth());
        Hline1->setSizePolicy(sizePolicy4);
        Hline1->setMinimumSize(QSize(0, 0));
        Hline1->setMaximumSize(QSize(16777215, 16777215));
        Hline1->setStyleSheet(QString::fromUtf8("background-color: rgb(217, 217, 217);"));
        Hline1->setFrameShadow(QFrame::Sunken);
        Hline1->setLineWidth(1);
        Hline1->setFrameShape(QFrame::HLine);

        gridLayout->addWidget(Hline1, 2, 0, 1, 3);


        verticalLayout_2->addLayout(gridLayout);

        gridLayout_4 = new QGridLayout();
        gridLayout_4->setObjectName(QString::fromUtf8("gridLayout_4"));
        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        gridLayout_4->addItem(horizontalSpacer, 0, 0, 1, 1);

        horizontalSpacer_3 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        gridLayout_4->addItem(horizontalSpacer_3, 0, 2, 1, 1);

        horizontalSpacer_2 = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        gridLayout_4->addItem(horizontalSpacer_2, 0, 4, 1, 1);

        reset = new QPushButton(materialSelection);
        reset->setObjectName(QString::fromUtf8("reset"));
        sizePolicy.setHeightForWidth(reset->sizePolicy().hasHeightForWidth());
        reset->setSizePolicy(sizePolicy);
        reset->setMinimumSize(QSize(368, 40));
        reset->setMaximumSize(QSize(16777215, 70));
        QFont font4;
        font4.setPointSize(14);
        reset->setFont(font4);
        reset->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"border: 1px solid black;  /* \346\265\205\347\201\260\350\211\262\350\276\271\346\241\206 */\n"
"border-radius: 10px;\n"
"background-color: rgb(128, 128, 128);\n"
"color: rgb(255, 255, 255);\n"
"}\n"
"QPushButton:pressed {\n"
"    background-color: #e0e0e0;     /* \346\214\211\344\270\213\350\203\214\346\231\257\346\233\264\346\267\261 */\n"
"    border: 1px solid #555555;     /* \346\214\211\344\270\213\350\276\271\346\241\206\351\242\234\350\211\262\345\217\230\346\232\227 */\n"
"}"));

        gridLayout_4->addWidget(reset, 0, 3, 1, 1);

        run = new QPushButton(materialSelection);
        run->setObjectName(QString::fromUtf8("run"));
        sizePolicy.setHeightForWidth(run->sizePolicy().hasHeightForWidth());
        run->setSizePolicy(sizePolicy);
        run->setMinimumSize(QSize(368, 40));
        run->setMaximumSize(QSize(16777215, 70));
        run->setFont(font4);
        run->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"border: 1px solid black;  /* \346\265\205\347\201\260\350\211\262\350\276\271\346\241\206 */\n"
"border-radius: 10px;\n"
"background-color: rgb(70, 195, 54);\n"
"}\n"
"QPushButton:pressed {\n"
"    background-color: #e0e0e0;     /* \346\214\211\344\270\213\350\203\214\346\231\257\346\233\264\346\267\261 */\n"
"    border: 1px solid #555555;     /* \346\214\211\344\270\213\350\276\271\346\241\206\351\242\234\350\211\262\345\217\230\346\232\227 */\n"
"}"));

        gridLayout_4->addWidget(run, 0, 1, 1, 1);

        verticalSpacer_4 = new QSpacerItem(20, 5, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout_4->addItem(verticalSpacer_4, 1, 1, 1, 1);


        verticalLayout_2->addLayout(gridLayout_4);

        verticalLayout_2->setStretch(1, 1);
        systemManagement = new QWidget(mainWindow);
        systemManagement->setObjectName(QString::fromUtf8("systemManagement"));
        systemManagement->setGeometry(QRect(0, 0, 881, 421));
        sizePolicy.setHeightForWidth(systemManagement->sizePolicy().hasHeightForWidth());
        systemManagement->setSizePolicy(sizePolicy);
        systemManagement->setMinimumSize(QSize(881, 421));
        systemManagement->setStyleSheet(QString::fromUtf8("background-color: rgb(255, 255, 255);"));
        gridLayout_6 = new QGridLayout(systemManagement);
        gridLayout_6->setObjectName(QString::fromUtf8("gridLayout_6"));
        gridLayout_6->setHorizontalSpacing(50);
        gridLayout_6->setVerticalSpacing(0);
        gridLayout_6->setContentsMargins(200, 0, 0, 0);
        verticalSpacer = new QSpacerItem(724, 67, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout_6->addItem(verticalSpacer, 0, 0, 1, 2);

        label_15 = new QLabel(systemManagement);
        label_15->setObjectName(QString::fromUtf8("label_15"));
        sizePolicy.setHeightForWidth(label_15->sizePolicy().hasHeightForWidth());
        label_15->setSizePolicy(sizePolicy);
        label_15->setMaximumSize(QSize(150, 16777215));
        QFont font5;
        font5.setPointSize(12);
        label_15->setFont(font5);
        label_15->setStyleSheet(QString::fromUtf8("border-right: 1px solid #e0e0e0;"));

        gridLayout_6->addWidget(label_15, 2, 0, 1, 1);

        label_18 = new QLabel(systemManagement);
        label_18->setObjectName(QString::fromUtf8("label_18"));
        sizePolicy.setHeightForWidth(label_18->sizePolicy().hasHeightForWidth());
        label_18->setSizePolicy(sizePolicy);
        label_18->setFont(font5);

        gridLayout_6->addWidget(label_18, 2, 1, 1, 1);

        label_20 = new QLabel(systemManagement);
        label_20->setObjectName(QString::fromUtf8("label_20"));
        sizePolicy.setHeightForWidth(label_20->sizePolicy().hasHeightForWidth());
        label_20->setSizePolicy(sizePolicy);
        label_20->setFont(font5);

        gridLayout_6->addWidget(label_20, 4, 1, 1, 1);

        label_2 = new QLabel(systemManagement);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        sizePolicy.setHeightForWidth(label_2->sizePolicy().hasHeightForWidth());
        label_2->setSizePolicy(sizePolicy);
        label_2->setMaximumSize(QSize(150, 16777215));
        label_2->setFont(font5);
        label_2->setStyleSheet(QString::fromUtf8("border-right: 1px solid #e0e0e0;"));

        gridLayout_6->addWidget(label_2, 5, 0, 1, 1);

        label_19 = new QLabel(systemManagement);
        label_19->setObjectName(QString::fromUtf8("label_19"));
        sizePolicy.setHeightForWidth(label_19->sizePolicy().hasHeightForWidth());
        label_19->setSizePolicy(sizePolicy);
        label_19->setFont(font5);

        gridLayout_6->addWidget(label_19, 3, 1, 1, 1);

        label_17 = new QLabel(systemManagement);
        label_17->setObjectName(QString::fromUtf8("label_17"));
        sizePolicy.setHeightForWidth(label_17->sizePolicy().hasHeightForWidth());
        label_17->setSizePolicy(sizePolicy);
        label_17->setMaximumSize(QSize(150, 16777215));
        label_17->setFont(font5);
        label_17->setStyleSheet(QString::fromUtf8("border-right: 1px solid #e0e0e0;"));

        gridLayout_6->addWidget(label_17, 4, 0, 1, 1);

        verticalSpacer_2 = new QSpacerItem(578, 67, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout_6->addItem(verticalSpacer_2, 8, 0, 1, 2);

        label_13 = new QLabel(systemManagement);
        label_13->setObjectName(QString::fromUtf8("label_13"));
        sizePolicy.setHeightForWidth(label_13->sizePolicy().hasHeightForWidth());
        label_13->setSizePolicy(sizePolicy);
        label_13->setMaximumSize(QSize(150, 16777215));
        label_13->setFont(font5);
        label_13->setStyleSheet(QString::fromUtf8("border-right: 1px solid #e0e0e0;"));

        gridLayout_6->addWidget(label_13, 1, 0, 1, 1);

        label_14 = new QLabel(systemManagement);
        label_14->setObjectName(QString::fromUtf8("label_14"));
        sizePolicy.setHeightForWidth(label_14->sizePolicy().hasHeightForWidth());
        label_14->setSizePolicy(sizePolicy);
        label_14->setFont(font5);

        gridLayout_6->addWidget(label_14, 1, 1, 1, 1);

        label_16 = new QLabel(systemManagement);
        label_16->setObjectName(QString::fromUtf8("label_16"));
        sizePolicy.setHeightForWidth(label_16->sizePolicy().hasHeightForWidth());
        label_16->setSizePolicy(sizePolicy);
        label_16->setMaximumSize(QSize(150, 16777215));
        label_16->setFont(font5);
        label_16->setStyleSheet(QString::fromUtf8("border-right: 1px solid #e0e0e0;"));

        gridLayout_6->addWidget(label_16, 3, 0, 1, 1);

        languageButton = new QPushButton(systemManagement);
        languageButton->setObjectName(QString::fromUtf8("languageButton"));
        sizePolicy.setHeightForWidth(languageButton->sizePolicy().hasHeightForWidth());
        languageButton->setSizePolicy(sizePolicy);
        languageButton->setMaximumSize(QSize(250, 16777215));
        languageButton->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"border: 1px solid black;  /* \346\265\205\347\201\260\350\211\262\350\276\271\346\241\206 */\n"
"border-radius: 10px;\n"
"	background-color: rgb(212, 212, 212);\n"
"}\n"
"QPushButton:pressed {\n"
"    background-color: #e0e0e0;     /* \346\214\211\344\270\213\350\203\214\346\231\257\346\233\264\346\267\261 */\n"
"    border: 1px solid #555555;     /* \346\214\211\344\270\213\350\276\271\346\241\206\351\242\234\350\211\262\345\217\230\346\232\227 */\n"
"}"));
        languageButton->setText(QString::fromUtf8("\347\256\200\344\275\223\344\270\255\346\226\207 / Simplified Chinese"));

        gridLayout_6->addWidget(languageButton, 5, 1, 1, 1);

        gridLayout_5 = new QGridLayout(mainWindow);
        gridLayout_5->setSpacing(0);
        gridLayout_5->setObjectName(QString::fromUtf8("gridLayout_5"));
        gridLayout_5->setContentsMargins(0, 0, 0, 0);
        homePage = new QWidget(mainWindow);
        homePage->setObjectName(QString::fromUtf8("homePage"));
        sizePolicy.setHeightForWidth(homePage->sizePolicy().hasHeightForWidth());
        homePage->setSizePolicy(sizePolicy);
        homePage->setMinimumSize(QSize(881, 421));
        homePage->setStyleSheet(QString::fromUtf8("background-color: rgb(38, 36, 42);"));
        horizontalLayout_8 = new QHBoxLayout(homePage);
        horizontalLayout_8->setObjectName(QString::fromUtf8("horizontalLayout_8"));
        cameraview = new QGraphicsView(homePage);
        cameraview->setObjectName(QString::fromUtf8("cameraview"));
        sizePolicy.setHeightForWidth(cameraview->sizePolicy().hasHeightForWidth());
        cameraview->setSizePolicy(sizePolicy);
        cameraview->setMinimumSize(QSize(441, 401));
        cameraview->setStyleSheet(QString::fromUtf8("background-color: rgb(0, 0, 0);"));
        cameraview->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        cameraview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        horizontalLayout_8->addWidget(cameraview);

        showchat = new QWidget(homePage);
        showchat->setObjectName(QString::fromUtf8("showchat"));
        sizePolicy.setHeightForWidth(showchat->sizePolicy().hasHeightForWidth());
        showchat->setSizePolicy(sizePolicy);
        showchat->setMinimumSize(QSize(401, 401));
        gridLayout_2 = new QGridLayout(showchat);
        gridLayout_2->setObjectName(QString::fromUtf8("gridLayout_2"));
        gridLayout_2->setHorizontalSpacing(5);
        gridLayout_2->setVerticalSpacing(0);

        horizontalLayout_8->addWidget(showchat);


        gridLayout_5->addWidget(homePage, 0, 0, 1, 1);

        homePage->raise();
        systemManagement->raise();
        materialSelection->raise();

        verticalLayout->addWidget(mainWindow);

        buttonlist = new QWidget(centralwidget);
        buttonlist->setObjectName(QString::fromUtf8("buttonlist"));
        sizePolicy.setHeightForWidth(buttonlist->sizePolicy().hasHeightForWidth());
        buttonlist->setSizePolicy(sizePolicy);
        buttonlist->setMinimumSize(QSize(881, 51));
        buttonlist->setMaximumSize(QSize(16777215, 100));
        horizontalLayout_5 = new QHBoxLayout(buttonlist);
        horizontalLayout_5->setSpacing(0);
        horizontalLayout_5->setObjectName(QString::fromUtf8("horizontalLayout_5"));
        horizontalLayout_5->setContentsMargins(0, 0, 0, 0);
        homepage = new QPushButton(buttonlist);
        homepage->setObjectName(QString::fromUtf8("homepage"));
        sizePolicy.setHeightForWidth(homepage->sizePolicy().hasHeightForWidth());
        homepage->setSizePolicy(sizePolicy);
        QFont font6;
        font6.setFamily(QString::fromUtf8("\345\256\213\344\275\223"));
        font6.setPointSize(14);
        homepage->setFont(font6);
        homepage->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"    border-left: 1px solid #e0e0e0;   /* \345\267\246\350\276\271\346\241\206 */\n"
"    border-right: 1px solid #e0e0e0;  /* \345\217\263\350\276\271\346\241\206 */\n"
"    border-top: none;              /* \345\216\273\346\216\211\344\270\212\350\276\271\346\241\206 */\n"
"    border-bottom: 1px solid #e0e0e0;           /* \345\216\273\346\216\211\344\270\213\350\276\271\346\241\206 */\n"
"    border-radius: 0px;            /* \345\216\273\346\216\211\345\233\233\350\247\222\345\274\247\345\272\246 */\n"
"	\n"
"	color: green;\n"
"	background-color: rgb(242, 242, 242);\n"
"}\n"
"QPushButton:pressed {\n"
"    background-color: #e0e0e0;     /* \346\214\211\344\270\213\350\203\214\346\231\257\346\233\264\346\267\261 */\n"
"}"));

        horizontalLayout_5->addWidget(homepage);

        materialselection = new QPushButton(buttonlist);
        materialselection->setObjectName(QString::fromUtf8("materialselection"));
        sizePolicy.setHeightForWidth(materialselection->sizePolicy().hasHeightForWidth());
        materialselection->setSizePolicy(sizePolicy);
        materialselection->setFont(font6);
        materialselection->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"    border-left: 1px solid #e0e0e0;   /* \345\267\246\350\276\271\346\241\206 */\n"
"    border-right: 1px solid #e0e0e0;  /* \345\217\263\350\276\271\346\241\206 */\n"
"    border-top: none;              /* \345\216\273\346\216\211\344\270\212\350\276\271\346\241\206 */\n"
"    border-bottom: 1px solid #e0e0e0;           /* \345\216\273\346\216\211\344\270\213\350\276\271\346\241\206 */\n"
"    border-radius: 0px;            /* \345\216\273\346\216\211\345\233\233\350\247\222\345\274\247\345\272\246 */\n"
"	\n"
"	background-color: rgb(242, 242, 242);\n"
"}\n"
"QPushButton:pressed {\n"
"    background-color: #e0e0e0;     /* \346\214\211\344\270\213\350\203\214\346\231\257\346\233\264\346\267\261 */\n"
"}"));

        horizontalLayout_5->addWidget(materialselection);

        systemmanagement = new QPushButton(buttonlist);
        systemmanagement->setObjectName(QString::fromUtf8("systemmanagement"));
        sizePolicy.setHeightForWidth(systemmanagement->sizePolicy().hasHeightForWidth());
        systemmanagement->setSizePolicy(sizePolicy);
        systemmanagement->setFont(font6);
        systemmanagement->setStyleSheet(QString::fromUtf8("QPushButton {\n"
"    border-left: 1px solid #e0e0e0;   /* \345\267\246\350\276\271\346\241\206 */\n"
"    border-right: 1px solid #e0e0e0;  /* \345\217\263\350\276\271\346\241\206 */\n"
"    border-top: none;              /* \345\216\273\346\216\211\344\270\212\350\276\271\346\241\206 */\n"
"    border-bottom: 1px solid #e0e0e0;           /* \345\216\273\346\216\211\344\270\213\350\276\271\346\241\206 */\n"
"    border-radius: 0px;            /* \345\216\273\346\216\211\345\233\233\350\247\222\345\274\247\345\272\246 */\n"
"	\n"
"	background-color: rgb(242, 242, 242);\n"
"}\n"
"QPushButton:pressed {\n"
"    background-color: #e0e0e0;     /* \346\214\211\344\270\213\350\203\214\346\231\257\346\233\264\346\267\261 */\n"
"}"));

        horizontalLayout_5->addWidget(systemmanagement);


        verticalLayout->addWidget(buttonlist);

        MainWindow->setCentralWidget(centralwidget);
        statusbar = new QStatusBar(MainWindow);
        statusbar->setObjectName(QString::fromUtf8("statusbar"));
        sizePolicy.setHeightForWidth(statusbar->sizePolicy().hasHeightForWidth());
        statusbar->setSizePolicy(sizePolicy);
        MainWindow->setStatusBar(statusbar);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "MainWindow", nullptr));
        label_3->setText(QString());
        label->setText(QCoreApplication::translate("MainWindow", "          AI\345\205\211\351\200\211\346\234\272", nullptr));
        powerbutton->setText(QString());
        label_4->setText(QCoreApplication::translate("MainWindow", " \345\275\223\345\211\215\345\210\206\351\200\211\347\232\204\347\211\251\346\226\231\347\247\215\347\261\273\344\270\272\357\274\232", nullptr));
        label_6->setText(QCoreApplication::translate("MainWindow", "\345\261\236\346\200\247", nullptr));
        label_7->setText(QCoreApplication::translate("MainWindow", "\347\211\271\345\276\201", nullptr));
        label_8->setText(QCoreApplication::translate("MainWindow", "\351\242\234\350\211\262", nullptr));
        label_9->setText(QCoreApplication::translate("MainWindow", "\345\223\201\347\261\273", nullptr));
        label_10->setText(QCoreApplication::translate("MainWindow", "\346\240\207\347\255\276", nullptr));
        label_11->setText(QCoreApplication::translate("MainWindow", "\345\275\242\346\200\201", nullptr));
        label_21->setStyleSheet(QString());
        label_21->setText(QString());
        label_12->setText(QCoreApplication::translate("MainWindow", "\345\223\201\347\233\270", nullptr));
        reset->setText(QCoreApplication::translate("MainWindow", "\351\207\215\347\275\256", nullptr));
        run->setText(QCoreApplication::translate("MainWindow", "\346\211\247\350\241\214", nullptr));
        label_15->setText(QCoreApplication::translate("MainWindow", "\350\256\276\345\244\207KEY", nullptr));
        label_18->setText(QCoreApplication::translate("MainWindow", "VBGBULH21K4JBKU2V54", nullptr));
        label_20->setText(QCoreApplication::translate("MainWindow", "127.0.0.1", nullptr));
        label_2->setText(QCoreApplication::translate("MainWindow", "\350\257\255\350\250\200", nullptr));
        label_19->setText(QCoreApplication::translate("MainWindow", "1.0.0", nullptr));
        label_17->setText(QCoreApplication::translate("MainWindow", "IP\344\277\241\346\201\257", nullptr));
        label_13->setText(QCoreApplication::translate("MainWindow", "\350\256\276\345\244\207ID", nullptr));
        label_14->setText(QCoreApplication::translate("MainWindow", "12345678", nullptr));
        label_16->setText(QCoreApplication::translate("MainWindow", "\345\233\272\344\273\266\347\211\210\346\234\254", nullptr));
        homepage->setText(QCoreApplication::translate("MainWindow", "\351\246\226\351\241\265", nullptr));
        materialselection->setText(QCoreApplication::translate("MainWindow", "\347\211\251\346\226\231\351\200\211\346\213\251", nullptr));
        systemmanagement->setText(QCoreApplication::translate("MainWindow", "\347\263\273\347\273\237\347\256\241\347\220\206", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
