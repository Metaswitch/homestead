# Top level Makefile for building homestead

# this should come first so make does the right thing by default
all: build

ROOT ?= ${PWD}
MK_DIR := ${ROOT}/mk
PREFIX ?= ${ROOT}/usr
INSTALL_DIR ?= ${PREFIX}
MODULE_DIR := ${ROOT}/modules

DEB_COMPONENT := homestead
DEB_MAJOR_VERSION := 1.0${DEB_VERSION_QUALIFIER}
DEB_NAMES := homestead-libs homestead-libs-dbg homestead homestead-dbg

INCLUDE_DIR := ${INSTALL_DIR}/include
LIB_DIR := ${INSTALL_DIR}/lib

SUBMODULES := libevhtp freeDiameter thrift cassandra sas-client

include $(patsubst %, ${MK_DIR}/%.mk, ${SUBMODULES})
include ${MK_DIR}/homestead.mk

build: ${SUBMODULES} homestead

test: ${SUBMODULES} homestead_test

testall: $(patsubst %, %_test, ${SUBMODULES}) test

clean: $(patsubst %, %_clean, ${SUBMODULES}) homestead_clean
	rm -rf ${ROOT}/usr
	rm -rf ${ROOT}/build

distclean: $(patsubst %, %_distclean, ${SUBMODULES}) homestead_distclean
	rm -rf ${ROOT}/usr
	rm -rf ${ROOT}/build

include build-infra/cw-deb.mk

.PHONY: deb
deb: build deb-only

.PHONY: all build test clean distclean
