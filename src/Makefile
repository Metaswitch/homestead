# Homestead Makefile

all: stage-build

ROOT := $(abspath $(shell pwd)/../)
MK_DIR := ${ROOT}/mk

GTEST_DIR := $(ROOT)/modules/gmock/gtest
GMOCK_DIR := $(ROOT)/modules/gmock

TARGET := homestead
TARGET_TEST := homestead_test

TARGET_SOURCES := accesslogger.cpp \
                  accumulator.cpp \
                  alarm.cpp \
                  baseresolver.cpp \
                  cache.cpp \
                  cassandra_store.cpp \
                  communicationmonitor.cpp \
                  counter.cpp \
                  cx.cpp \
                  diameterstack.cpp \
                  diameterresolver.cpp \
                  dnscachedresolver.cpp \
                  dnsparser.cpp \
                  handlers.cpp \
                  httpconnection.cpp \
                  httpresolver.cpp \
                  httpstack.cpp \
                  httpstack_utils.cpp \
                  load_monitor.cpp \
                  logger.cpp \
                  log.cpp \
                  realmmanager.cpp \
                  saslogger.cpp \
                  sproutconnection.cpp \
                  statistic.cpp \
                  statisticsmanager.cpp \
                  utils.cpp \
                  xmlutils.cpp \
                  zmq_lvc.cpp

TARGET_SOURCES_BUILD := main.cpp

TARGET_SOURCES_TEST := test_main.cpp \
                       test_interposer.cpp \
                       cx_test.cpp \
                       xmlutils_test.cpp \
                       diameterstack_test.cpp \
                       cache_test.cpp \
                       handlers_test.cpp \
                       httpstack_test.cpp \
                       httpstack_utils_test.cpp \
                       fakelogger.cpp \
                       mockfreediameter.cpp \
                       mock_sas.cpp \
                       realmmanager_test.cpp \
                       diameterresolver_test.cpp \
                       chargingaddresses_test.cpp

TARGET_EXTRA_OBJS_TEST := gmock-all.o \
                          gtest-all.o

TEST_XML = $(TEST_OUT_DIR)/test_detail_$(TARGET_TEST).xml
COVERAGE_XML = $(TEST_OUT_DIR)/coverage_$(TARGET_TEST).xml
COVERAGE_LIST_TMP = $(TEST_OUT_DIR)/coverage_list_tmp
COVERAGE_LIST = $(TEST_OUT_DIR)/coverage_list
COVERAGE_MASTER_LIST = ut/coverage-not-yet
VG_XML = $(TEST_OUT_DIR)/vg_$(TARGET_TEST).memcheck
VG_OUT = $(TEST_OUT_DIR)/vg_$(TARGET_TEST).txt
VG_LIST = $(TEST_OUT_DIR)/vg_$(TARGET_TEST)_list

VG_SUPPRESS = ut/$(TARGET_TEST).supp

EXTRA_CLEANS += $(TEST_XML) \
                $(COVERAGE_XML) \
                $(VG_XML) $(VG_OUT) \
                $(OBJ_DIR_TEST)/*.gcno \
                $(OBJ_DIR_TEST)/*.gcda \
                *.gcov

CPPFLAGS += -Wno-write-strings \
            -ggdb3 -std=c++0x
CPPFLAGS += -I${ROOT}/include \
            -I${ROOT}/usr/include \
            -I${ROOT}/modules/cpp-common/include \
            -I${ROOT}/modules/cpp-common/test_utils \
            -I${ROOT}/modules/rapidjson/include \
            -I${ROOT}/modules/sas-client/include

# Add modules/cpp-common/src as a VPATH to pull in required common modules
VPATH := ${ROOT}/modules/cpp-common/src:${ROOT}/modules/cpp-common/test_utils

# Production build:
#
# Enable optimization in production only.
CPPFLAGS := $(filter-out -O2,$(CPPFLAGS))
CPPFLAGS_BUILD += -O2

# Test build:
#
# Turn on code coverage.
# Disable optimization, for speed and coverage accuracy.
# Allow testing of private and protected fields/methods.
# Add the Google Mock / Google Test includes.
CPPFLAGS_TEST  += -DUNIT_TEST \
                  -fprofile-arcs -ftest-coverage \
                  -O0 \
                  -fno-access-control \
                  -I$(GTEST_DIR)/include -I$(GMOCK_DIR)/include

LDFLAGS += -L${ROOT}/usr/lib
LDFLAGS += -lthrift \
           -lcassandra \
           -lzmq \
           -lfdcore \
           -lfdproto \
           -levhtp \
           -levent_pthreads \
           -levent \
           -lcares \
           -lboost_regex \
           -lboost_date_time \
           -lpthread \
           -lcurl

# Only use the real SAS library in the production build.
LDFLAGS_BUILD += -lsas

# Test build uses just-built libraries, which may not be installed
LDFLAGS_TEST += -Wl,-rpath=$(ROOT)/usr/lib

# Test build also uses libcurl (to verify HttpStack operation)
LDFLAGS_TEST += -lcurl

# Now the GMock / GTest boilerplate.
GTEST_HEADERS := $(GTEST_DIR)/include/gtest/*.h \
                 $(GTEST_DIR)/include/gtest/internal/*.h
GMOCK_HEADERS := $(GMOCK_DIR)/include/gmock/*.h \
                 $(GMOCK_DIR)/include/gmock/internal/*.h \
                 $(GTEST_HEADERS)

GTEST_SRCS_ := $(GTEST_DIR)/src/*.cc $(GTEST_DIR)/src/*.h $(GTEST_HEADERS)
GMOCK_SRCS_ := $(GMOCK_DIR)/src/*.cc $(GMOCK_HEADERS)
# End of boilerplate

COVERAGEFLAGS = $(OBJ_DIR_TEST) --object-directory=$(shell pwd) --root=${ROOT} \
                --exclude='(^include/|^modules/gmock/|^modules/rapidjson/|^modules/cpp-common/include/|^modules/cpp-common/test_utils|^ut/|^usr/)' \
                --sort-percentage

VGFLAGS = --suppressions=$(VG_SUPPRESS) \
          --gen-suppressions=all \
          --leak-check=full \
          --track-origins=yes \
          --malloc-fill=cc \
          --free-fill=df

# Define JUSTTEST=<testname> to test just that test.  Easier than
# passing the --gtest_filter in EXTRA_TEST_ARGS.
ifdef JUSTTEST
  EXTRA_TEST_ARGS ?= --gtest_filter=*$(JUSTTEST)*
endif

include ${MK_DIR}/platform.mk

.PHONY: stage-build
stage-build: build

.PHONY: test
test: run_test coverage vg coverage-check vg-check

# Run the test.  You can set EXTRA_TEST_ARGS to pass extra arguments
# to the test, e.g.,
#
#   make EXTRA_TEST_ARGS=--gtest_filter=StatefulProxyTest* run_test
#
# runs just the StatefulProxyTest tests.
#
# Ignore failure here; it will be detected by Jenkins.
.PHONY: run_test
run_test: build_test | $(TEST_OUT_DIR)
	rm -f $(TEST_XML)
	rm -f $(OBJ_DIR_TEST)/*.gcda
	$(TARGET_BIN_TEST) $(EXTRA_TEST_ARGS) --gtest_output=xml:$(TEST_XML)

.PHONY: coverage
coverage: | $(TEST_OUT_DIR)
	$(GCOVR) $(COVERAGEFLAGS) --xml > $(COVERAGE_XML)

# Check that we have 100% coverage of all files except those that we
# have declared we're being relaxed on.  In particular, all new files
# must have 100% coverage or be added to $(COVERAGE_MASTER_LIST).
# The string "Marking build unstable" is recognised by the CI scripts
# and if it is found the build is marked unstable.
.PHONY: coverage-check
coverage-check: coverage
	@xmllint --xpath '//class[@line-rate!="1.0"]/@filename' $(COVERAGE_XML) \
		| tr ' ' '\n' \
		| grep filename= \
		| cut -d\" -f2 \
		| sort > $(COVERAGE_LIST_TMP)
	@sort $(COVERAGE_MASTER_LIST) | comm -23 $(COVERAGE_LIST_TMP) - > $(COVERAGE_LIST)
	@if grep -q ^ $(COVERAGE_LIST) ; then \
		echo "Error: some files unexpectedly have less than 100% code coverage:" ; \
		cat $(COVERAGE_LIST) ; \
		/bin/false ; \
		echo "Marking build unstable." ; \
	fi

# Get quick coverage data at the command line. Add --branches to get branch info
# instead of line info in report.  *.gcov files generated in current directory
# if you need to see full detail.
.PHONY: coverage_raw
coverage_raw: | $(TEST_OUT_DIR)
	$(GCOVR) $(COVERAGEFLAGS) --keep

.PHONY: debug
debug: build_test
	gdb --args $(TARGET_BIN_TEST) $(EXTRA_TEST_ARGS)

# Don't run VG against death tests; they don't play nicely.
# Be aware that running this will count towards coverage.
# Don't send output to console, or it might be confused with the full
# unit-test run earlier.
# Test failure should not lead to build failure - instead we observe
# test failure from Jenkins.
.PHONY: vg
vg: build_test | $(TEST_OUT_DIR)
	-valgrind --xml=yes --xml-file=$(VG_XML) $(VGFLAGS) \
	  $(TARGET_BIN_TEST) --gtest_filter='-*DeathTest*' $(EXTRA_TEST_ARGS) > $(VG_OUT) 2>&1

# Check whether there were any errors from valgrind. Output to screen any errors found,
# and details of where to find the full logs.
# The output file will contain <error><kind>ERROR</kind></error>, or 'XPath set is empty'
# if there are no errors.
.PHONY: vg-check
vg-check: vg
	@xmllint --xpath '//error/kind' $(VG_XML) 2>&1 | \
		sed -e 's#<kind>##g' | \
		sed -e 's#</kind>#\n#g' | \
		sort > $(VG_LIST)
	@if grep -q -v "XPath set is empty" $(VG_LIST) ; then \
		echo "Error: some memory errors have been detected" ; \
		cat $(VG_LIST) ; \
		echo "See $(VG_XML) for further details." ; \
	fi

.PHONY: vg_raw
vg_raw: build_test | $(TEST_OUT_DIR)
	-valgrind --gen-suppressions=all $(VGFLAGS) \
	  $(TARGET_BIN_TEST) --gtest_filter='-*DeathTest*' $(EXTRA_TEST_ARGS)

.PHONY: distclean
distclean: clean

# Build rules for GMock/GTest library.
$(OBJ_DIR_TEST)/gtest-all.o : $(GTEST_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) -I$(GTEST_DIR)/include -I$(GMOCK_DIR) -I$(GMOCK_DIR)/include \
            -c $(GTEST_DIR)/src/gtest-all.cc -o $@

$(OBJ_DIR_TEST)/gmock-all.o : $(GMOCK_SRCS_)
	$(CXX) $(CPPFLAGS) -I$(GTEST_DIR) -I$(GTEST_DIR)/include -I$(GMOCK_DIR) -I$(GMOCK_DIR)/include \
            -c $(GMOCK_DIR)/src/gmock-all.cc -o $@

# Build rule for our interposer.
$(OBJ_DIR_TEST)/test_interposer.so: ut/test_interposer.cpp ut/test_interposer.hpp
	$(CXX) $(CPPFLAGS) -shared -fPIC -ldl $< -o $@
