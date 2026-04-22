QT       += core testlib
QT       -= gui
CONFIG   += c++14 console testcase
CONFIG   -= app_bundle
CONFIG   += link_pkgconfig
PKGCONFIG += opencv4

TEMPLATE = app
TARGET   = pipeline_tests

DEFINES += QT_MESSAGELOGCONTEXT

# 本项目内部 include 都以 src/ 为根:'pipeline/xxx.h'、'config/runtime_config.h' 等
INCLUDEPATH += $$PWD/../src

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

SOURCES += \
    $$PWD/test_pipeline_clock.cpp \
    $$PWD/test_runtime_config.cpp \
    $$PWD/test_postprocess_ex.cpp \
    $$PWD/test_tracker_worker.cpp \
    $$PWD/test_dispatcher.cpp \
    $$PWD/test_main.cpp

# 不用 resources.qrc / ui
