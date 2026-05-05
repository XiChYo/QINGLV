QT       += core gui
QT       -= widgets
CONFIG   += c++14 console utf8_source link_pkgconfig
CONFIG   -= app_bundle
PKGCONFIG += opencv4

TEMPLATE = app
TARGET   = offline_sim

DEFINES += QT_MESSAGELOGCONTEXT

# ----------------------------------------------------------------------------
# 工程内 include 都按 'src/' 为根:'pipeline/xxx.h' / 'config/xxx.h' 等。
# 同目录的 sink 头文件可裸 include('valve_sink.h')。
# ----------------------------------------------------------------------------
INCLUDEPATH += $$PWD/../../src
INCLUDEPATH += $$PWD
# 注意:不要再 'INCLUDEPATH += /usr/include',那会把系统头变成普通 -I,
# 反而打断 libstdc++ 'cstdlib → #include_next <stdlib.h>' 的搜索链
# (g++ 9 + Qt 5.12.8 实测 fatal: stdlib.h: No such file or directory)。
# rknn_api.h 在 /usr/include 下,g++ 默认就会查到,无需显式加。

# ----------------------------------------------------------------------------
# 复用生产代码:YoloWorker / YoloSession / postprocess_ex / TrackerWorker /
#   Dispatcher / RuntimeConfig / Logger / pipeline_clock。
# 不引入:CameraWorker(MVS 依赖) / BoardWorker(HR_Pro 依赖) / mainwindow(QtWidgets)。
# ----------------------------------------------------------------------------
HEADERS += \
    $$PWD/../../src/pipeline/pipeline_clock.h \
    $$PWD/../../src/pipeline/pipeline_types.h \
    $$PWD/../../src/pipeline/yolo_session.h \
    $$PWD/../../src/pipeline/yolo_worker.h \
    $$PWD/../../src/pipeline/postprocess.h \
    $$PWD/../../src/pipeline/postprocess_ex.h \
    $$PWD/../../src/pipeline/tracker_worker.h \
    $$PWD/../../src/pipeline/dispatcher.h \
    $$PWD/../../src/config/runtime_config.h \
    $$PWD/../../src/infra/logger.h

SOURCES += \
    $$PWD/../../src/pipeline/pipeline_clock.cpp \
    $$PWD/../../src/pipeline/yolo_session.cpp \
    $$PWD/../../src/pipeline/yolo_worker.cpp \
    $$PWD/../../src/pipeline/postprocess_ex.cpp \
    $$PWD/../../src/pipeline/tracker_worker.cpp \
    $$PWD/../../src/pipeline/dispatcher.cpp \
    $$PWD/../../src/config/runtime_config.cpp \
    $$PWD/../../src/infra/logger.cpp

# ----------------------------------------------------------------------------
# offline_sim 自身模块(driver / sink / log_parser / main)
# ----------------------------------------------------------------------------
HEADERS += \
    $$PWD/log_parser.h \
    $$PWD/offline_camera_driver.h \
    $$PWD/offline_encoder_driver.h \
    $$PWD/valve_sink.h \
    $$PWD/sort_task_sink.h \
    $$PWD/annotated_frame_sink.h

SOURCES += \
    $$PWD/log_parser.cpp \
    $$PWD/offline_camera_driver.cpp \
    $$PWD/offline_encoder_driver.cpp \
    $$PWD/valve_sink.cpp \
    $$PWD/sort_task_sink.cpp \
    $$PWD/annotated_frame_sink.cpp \
    $$PWD/main.cpp

# rknn 运行时(随 rknpu 驱动包安装到 /usr/lib)
unix:!macx: LIBS += -L/usr/lib -lrknnrt
