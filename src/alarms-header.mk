# Homestead alarms to JSON Makefile

all: alarm_header

ROOT := $(abspath $(shell pwd)/../)
MK_DIR := ${ROOT}/mk

TARGET := homestead_alarm

TARGET_SOURCES := alarm_header.cpp \
                  json_alarms.cpp \
                  alarmdefinition.cpp

CPPFLAGS += -Wno-write-strings \
            -ggdb3 -std=c++0x

CPPFLAGS += -I${ROOT}/modules/cpp-common/include \
            -I${ROOT}/modules/rapidjson/include

# Add cpp-common/src as VPATH so build will find modules there.
VPATH = ${ROOT}/modules/cpp-common/src

include ${MK_DIR}/platform.mk

alarm_header: build
	${BUILD_DIR}/bin/homestead_alarm
	mv homestead_alarmdefinition.h ${ROOT}/usr/include/
