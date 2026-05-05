QT       += core testlib gui
CONFIG   += c++14 console testcase
CONFIG   -= app_bundle
CONFIG   += link_pkgconfig
PKGCONFIG += opencv4

TEMPLATE = app
TARGET   = pipeline_tests

DEFINES += QT_MESSAGELOGCONTEXT

# 本项目内部 include 都以 src/ 为根:'pipeline/xxx.h'、'config/runtime_config.h' 等。
# offline_sim/ 也加进来,driver / log_parser 头文件互相 include 时按 'offline_sim/xxx.h'
# 或裸 'log_parser.h' 都能命中(后者由 offline_sim 自身相对路径解析)。
INCLUDEPATH += $$PWD/../src
INCLUDEPATH += $$PWD

# 被测生产代码:只挑不依赖 RKNN/MVS/HR_Pro 的模块。
SOURCES += \
    $$PWD/../src/pipeline/pipeline_clock.cpp \
    $$PWD/../src/config/runtime_config.cpp \
    $$PWD/../src/pipeline/postprocess_ex.cpp \
    $$PWD/../src/pipeline/tracker_worker.cpp \
    $$PWD/../src/pipeline/dispatcher.cpp \
    $$PWD/../src/infra/logger.cpp

HEADERS += \
    $$PWD/../src/pipeline/pipeline_clock.h \
    $$PWD/../src/pipeline/pipeline_types.h \
    $$PWD/../src/config/runtime_config.h \
    $$PWD/../src/pipeline/postprocess.h \
    $$PWD/../src/pipeline/postprocess_ex.h \
    $$PWD/../src/pipeline/tracker_worker.h \
    $$PWD/../src/pipeline/dispatcher.h \
    $$PWD/../src/infra/logger.h

# postprocess_ex 仅依赖 postprocess.h 的 SegObject 声明,postprocess.h 本身
# 不引入 rknn 头,这里不用链接 RKNN。

# offline_sim 子工程的纯 Qt 模块(M0.5 / M1 / M2 通用):
SOURCES += \
    $$PWD/offline_sim/log_parser.cpp \
    $$PWD/offline_sim/offline_camera_driver.cpp \
    $$PWD/offline_sim/offline_encoder_driver.cpp

HEADERS += \
    $$PWD/offline_sim/log_parser.h \
    $$PWD/offline_sim/offline_camera_driver.h \
    $$PWD/offline_sim/offline_encoder_driver.h

SOURCES += \
    $$PWD/test_pipeline_clock.cpp \
    $$PWD/test_runtime_config.cpp \
    $$PWD/test_postprocess_ex.cpp \
    $$PWD/test_tracker_worker.cpp \
    $$PWD/test_dispatcher.cpp \
    $$PWD/test_offline_drivers.cpp \
    $$PWD/test_main.cpp

# 不用 resources.qrc / ui
