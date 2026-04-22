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
# DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

# ---------------------------------------------------------------------------
# 源码(按角色分组)
# ---------------------------------------------------------------------------
SOURCES += \
    src/app/main.cpp \
    src/app/mainwindow.cpp \
    src/infra/logger.cpp \
    src/config/runtime_config.cpp \
    src/pipeline/boardcontrol.cpp \
    src/pipeline/camera_worker.cpp \
    src/pipeline/dispatcher.cpp \
    src/pipeline/pipeline_clock.cpp \
    src/pipeline/postprocess_ex.cpp \
    src/pipeline/tracker_worker.cpp \
    src/pipeline/yolo_session.cpp \
    src/pipeline/yolo_worker.cpp \
    src/legacy/robotcontrol.cpp \
    src/legacy/savelocalpic.cpp \
    src/legacy/tcpforrobot.cpp \
    src/legacy/updatemanager.cpp \
    src/legacy/uploadpictooss.cpp

HEADERS += \
    src/app/mainwindow.h \
    src/infra/logger.h \
    src/config/runtime_config.h \
    src/pipeline/boardcontrol.h \
    src/pipeline/camera_worker.h \
    src/pipeline/dispatcher.h \
    src/pipeline/pipeline_clock.h \
    src/pipeline/pipeline_types.h \
    src/pipeline/postprocess.h \
    src/pipeline/postprocess_ex.h \
    src/pipeline/tracker_worker.h \
    src/pipeline/yolo_session.h \
    src/pipeline/yolo_worker.h \
    src/legacy/robotcontrol.h \
    src/legacy/savelocalpic.h \
    src/legacy/tcpforrobot.h \
    src/legacy/updatemanager.h \
    src/legacy/uploadpictooss.h

FORMS += \
    src/app/mainwindow.ui

# ---------------------------------------------------------------------------
# 本项目内部 include 统一以 src/ 为根:"pipeline/xxx.h"、"infra/logger.h" ...
# 第三方 SDK 通过 INCLUDEPATH 暴露顶层,代码 #include <MvCameraControl.h>。
# ---------------------------------------------------------------------------
INCLUDEPATH += $$PWD/src
INCLUDEPATH += $$PWD/third_party/mvs-sdk/include
INCLUDEPATH += $$PWD/third_party/hr-robot-sdk/include
DEPENDPATH  += $$PWD/src
DEPENDPATH  += $$PWD/third_party/mvs-sdk/include
DEPENDPATH  += $$PWD/third_party/hr-robot-sdk/include

TRANSLATIONS += \
    i18n/guangxuan_zh_CN.ts \
    i18n/guangxuan_zh_EN.ts

DISTFILES += \
    i18n/guangxuan_zh_CN.ts \
    i18n/guangxuan_zh_EN.ts

RESOURCES += \
    resources.qrc

# ---------------------------------------------------------------------------
# 部署 / 第三方库链接
# ---------------------------------------------------------------------------
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

unix:!macx: LIBS += -L$$PWD/third_party/mvs-sdk/lib/aarch64/ -lMvCameraControl
unix:!macx: LIBS += -L$$PWD/third_party/hr-robot-sdk/lib/ -lHR_Pro

# RKNN runtime 使用系统安装(/usr/include + /usr/lib,随 rknpu 驱动包安装)
unix:!macx: LIBS += -L/usr/lib/ -lrknnrt
INCLUDEPATH += /usr/include
DEPENDPATH  += /usr/include
