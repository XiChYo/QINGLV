# ============================================================================
# test_log_parser.pro — 独立 QTest 单测工程,M0.5 雏形阶段用。
#
# 设计要点:
#   - 纯 Qt(core + testlib),不依赖 OpenCV / RKNN / MVS。
#   - 与生产工程 guangxuan.pro 完全解耦,本地 mac 也能 qmake && make 跑通。
#   - M3 阶段会被合并到 tests/offline_sim/offline_sim.pro 里;同时本测试入口
#     会被注册到 tests/tests.pro 的 test_main.cpp。
#
# 用法:
#     cd tests/offline_sim
#     qmake test_log_parser.pro
#     make -j4
#     ./test_log_parser
# ============================================================================

QT       += core testlib
QT       -= gui

CONFIG   += c++14 console testcase
CONFIG   -= app_bundle
CONFIG   += utf8_source

TEMPLATE = app
TARGET   = test_log_parser

DEFINES += QT_MESSAGELOGCONTEXT

SOURCES += \
    $$PWD/log_parser.cpp \
    $$PWD/test_log_parser.cpp

HEADERS += \
    $$PWD/log_parser.h
