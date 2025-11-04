QT += core websockets
CONFIG += console
CONFIG -= app_bundle

TARGET = websocket_server
TEMPLATE = app

SOURCES += websocket_server_with_routing.cpp

# 设置输出目录
DESTDIR = ../build/bin/Release