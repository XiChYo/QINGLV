# 一次性 smoke 工具:让 LogParser 跑真实 saveRawPic/log.txt,核对真值
# 不进 git,只是 M0.5 阶段开发者本地验真用
QT       += core
QT       -= gui
CONFIG   += c++14 console
CONFIG   -= app_bundle
CONFIG   += utf8_source
TEMPLATE = app
TARGET   = sanity_check

SOURCES += $$PWD/log_parser.cpp $$PWD/sanity_check.cpp
HEADERS += $$PWD/log_parser.h
