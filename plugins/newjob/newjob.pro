#-------------------------------------------------
#
# Project created by QtCreator 2011-03-22T19:33:11
#
#-------------------------------------------------

QT       += gui widgets network
TEMPLATE = lib
CONFIG += plugin
VERSION = 1.0.0
TARGET = $$qtLibraryTarget(newjob)

GENERATED_DIR = ../../generated/plugin/newjob
# Use common project definitions.
include(../../common.pri)

# For plugins
INCLUDEPATH    += ../../librecad/src/plugins

SOURCES += newjob.cpp
HEADERS += newjob.h newjob_geom.h

# Installation Directory
win32 {
        DESTDIR = ../../windows/resources/plugins
}
unix {
    macx {
        DESTDIR = ../../LibreCAD.app/Contents/PlugIns/LibreCAD
    }
    else {
        DESTDIR = ../../unix/resources/plugins
    }
}
