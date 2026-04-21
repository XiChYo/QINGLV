QT       += core testlib
QT       -= gui
CONFIG   += c++14 console testcase
CONFIG   -= app_bundle
CONFIG   += link_pkgconfig
PKGCONFIG += opencv4

TEMPLATE = app
TARGET   = pipeline_tests

DEFINES += QT_MESSAGELOGCONTEXT

INCLUDEPATH += $$PWD/..

# 被测生产代码:只挑不依赖 RKNN/MVS/HR_Pro 的模块。
SOURCES += \
    $$PWD/../pipeline_clock.cpp \
    $$PWD/../runtime_config.cpp \
    $$PWD/../postprocess_ex.cpp \
    $$PWD/../tracker_worker.cpp \
    $$PWD/../dispatcher.cpp \
    $$PWD/../logger.cpp

HEADERS += \
    $$PWD/../pipeline_clock.h \
    $$PWD/../pipeline_types.h \
    $$PWD/../runtime_config.h \
    $$PWD/../postprocess.h \
    $$PWD/../postprocess_ex.h \
    $$PWD/../tracker_worker.h \
    $$PWD/../dispatcher.h \
    $$PWD/../logger.h

# 生产代码里 postprocess_ex 依赖 postprocess.h 声明(SegObject 类型),但
# postprocess.cpp 整体依赖 rknn_api.h 等,这里不链接。只要 postprocess.h 本身
# 不包含 rknn 头即可。若 postprocess.h 引入了 rknn,SegObject 迁移到独立头更稳。

# 测试入口(QTest)
SOURCES += \
    $$PWD/test_pipeline_clock.cpp \
    $$PWD/test_runtime_config.cpp \
    $$PWD/test_postprocess_ex.cpp \
    $$PWD/test_tracker_worker.cpp \
    $$PWD/test_dispatcher.cpp \
    $$PWD/test_main.cpp

# 不用 resources.qrc / ui
