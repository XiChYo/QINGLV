QT       += core gui network concurrent

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++14
CONFIG += utf8_source
CONFIG += link_pkgconfig
CONFIG += ssl
PKGCONFIG += libcurl
PKGCONFIG += opencv4


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
    ConveyorTracker.cpp \
    boardcontrol.cpp \
    caldistance.cpp \
    camerathread.cpp \
    logger.cpp \
    main.cpp \
    mainwindow.cpp \
    postprocess.cpp \
    savelocalpic.cpp \
    updatemanager.cpp \
    uploadpictooss.cpp \
    yolorecognition.cpp \
    yolothread.cpp

HEADERS += \
    ConveyorTracker.h \
    boardcontrol.h \
    caldistance.h \
    camerathread.h \
    logger.h \
    mainwindow.h \
    library/mvs/includes/MvCameraControl.h \
    postprocess.h \
    savelocalpic.h \
    updatemanager.h \
    uploadpictooss.h \
    valvecmd.h \
    yolorecognition.h \
    yoloresulttypes.h \
    yolothread.h


FORMS += \
    mainwindow.ui

TRANSLATIONS += \
    guangxuan_zh_CN.ts \
    guangxuan_zh_EN.ts

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    guangxuan_zh_EN.ts \
    guangxuan_zh_CN.ts

RESOURCES += \
    resources.qrc

unix:!macx: LIBS += -L$$PWD/bin/lib/aarch64/ -lMvCameraControl

INCLUDEPATH += $$PWD/bin/lib/aarch64
DEPENDPATH += $$PWD/bin/lib/aarch64



unix:!macx: LIBS += -L$$PWD/../../../../../usr/lib/ -lrknnrt

INCLUDEPATH += $$PWD/../../../../../usr/include
DEPENDPATH += $$PWD/../../../../../usr/include

#INCLUDEPATH += /usr/include/c++/9
