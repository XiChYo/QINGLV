QT       += core gui
QT       -= widgets
CONFIG   += c++14 console
CONFIG   -= app_bundle
CONFIG   += link_pkgconfig utf8_source
PKGCONFIG += opencv4

TEMPLATE = app
TARGET   = calibrate_rpm_to_speed

DEFINES += QT_MESSAGELOGCONTEXT

# 本项目内部 include 都以 src/ 为根:'pipeline/xxx.h' / 'config/runtime_config.h' / ...
INCLUDEPATH += $$PWD/../../src
INCLUDEPATH += /usr/include            # rknn_api.h(随 RKNPU 驱动包安装)

# 复用生产代码:YoloWorker + YoloSession + 后处理 + 配置 + 日志
HEADERS += \
    $$PWD/../../src/pipeline/pipeline_clock.h \
    $$PWD/../../src/pipeline/pipeline_types.h \
    $$PWD/../../src/pipeline/yolo_session.h \
    $$PWD/../../src/pipeline/yolo_worker.h \
    $$PWD/../../src/pipeline/postprocess.h \
    $$PWD/../../src/pipeline/postprocess_ex.h \
    $$PWD/../../src/config/runtime_config.h \
    $$PWD/../../src/infra/logger.h \
    $$PWD/log_parser.h

SOURCES += \
    $$PWD/../../src/pipeline/pipeline_clock.cpp \
    $$PWD/../../src/pipeline/yolo_session.cpp \
    $$PWD/../../src/pipeline/yolo_worker.cpp \
    $$PWD/../../src/pipeline/postprocess_ex.cpp \
    $$PWD/../../src/config/runtime_config.cpp \
    $$PWD/../../src/infra/logger.cpp \
    $$PWD/log_parser.cpp \
    $$PWD/calibrate_rpm_to_speed.cpp

# rknn 运行时(随 rknpu 驱动包安装到 /usr/lib)
unix:!macx: LIBS += -L/usr/lib -lrknnrt
