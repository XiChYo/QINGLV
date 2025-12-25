QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++14
CONFIG += utf8_source


# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    camerathread.cpp \
    logger.cpp \
    main.cpp \
    mainwindow.cpp \
    uploadpictooss.cpp

HEADERS += \
    camerathread.h \
    logger.h \
    mainwindow.h \
    library/mvs/includes/MvCameraControl.h \
    uploadpictooss.h


FORMS += \
    mainwindow.ui

TRANSLATIONS += \
    guangxuan_zh_CN.ts \
    guangxuan_zh_EN.ts

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target



#win32: LIBS += -L$$PWD/library/mvs/libs/ -lMvCameraControl

#INCLUDEPATH += $$PWD/library/mvs/includes
#DEPENDPATH += $$PWD/library/mvs/includes

#unix:!macx: LIBS += -L$$PWD/../../下载/MVS-3.0.1_x86_64_20241128/MVS/lib/64/ -lMvCameraControl

#INCLUDEPATH += $$PWD/../../下载/MVS-3.0.1_x86_64_20241128/MVS/lib/64
#DEPENDPATH += $$PWD/../../下载/MVS-3.0.1_x86_64_20241128/MVS/lib/64



unix:!macx: LIBS += -L$$PWD/../../../../opt/MVS/lib/64/ -lMvCameraControl

INCLUDEPATH += $$PWD/../../../../opt/MVS/lib/64
DEPENDPATH += $$PWD/../../../../opt/MVS/lib/64

DISTFILES += \
    guangxuan_zh_EN.ts
