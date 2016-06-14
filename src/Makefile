TARGETS := homestead
TEST_TARGETS := homestead_test

COMMON_SOURCES := accesslogger.cpp \
                  accumulator.cpp \
                  alarm.cpp \
                  base_communication_monitor.cpp \
                  baseresolver.cpp \
                  base64.cpp \
                  cache.cpp \
                  cassandra_store.cpp \
                  communicationmonitor.cpp \
                  counter.cpp \
                  cx.cpp \
                  diameterstack.cpp \
                  diameterresolver.cpp \
                  dnscachedresolver.cpp \
                  dnsparser.cpp \
                  exception_handler.cpp \
                  handlers.cpp \
                  health_checker.cpp \
                  httpconnection.cpp \
                  httpresolver.cpp \
                  httpstack.cpp \
                  httpstack_utils.cpp \
                  load_monitor.cpp \
                  logger.cpp \
                  log.cpp \
                  namespace_hop.cpp \
                  pdlog.cpp \
                  realmmanager.cpp \
                  saslogger.cpp \
                  sproutconnection.cpp \
                  statistic.cpp \
                  statisticsmanager.cpp \
                  snmp_agent.cpp \
                  snmp_row.cpp \
                  snmp_scalar.cpp \
                  utils.cpp \
                  xmlutils.cpp \
                  zmq_lvc.cpp

homestead_SOURCES := ${COMMON_SOURCES} \
                     main.cpp \
                     snmp_counter_table.cpp \
                     snmp_event_accumulator_table.cpp \
                     snmp_cx_counter_table.cpp

homestead_test_SOURCES := ${COMMON_SOURCES} \
                          test_main.cpp \
                          test_interposer.cpp \
                          cx_test.cpp \
                          xmlutils_test.cpp \
                          cache_test.cpp \
                          handlers_test.cpp \
                          fakelogger.cpp \
                          fakesnmp.cpp \
                          mockfreediameter.cpp \
                          mock_sas.cpp \
                          chargingaddresses_test.cpp \
                          pthread_cond_var_helper.cpp

COMMON_CPPFLAGS := -I../include \
                   -I../usr/include \
                   -I../modules/cpp-common/include \
                   -I../modules/cpp-common/test_utils \
                   -I../modules/rapidjson/include \
                   -I../modules/sas-client/include

homestead_CPPFLAGS := ${COMMON_CPPFLAGS}
homestead_test_CPPFLAGS := ${COMMON_CPPFLAGS} -DGTEST_USE_OWN_TR1_TUPLE=0

COMMON_LDFLAGS := -L../usr/lib \
                  -lthrift \
                  -lcassandra \
                  -lzmq \
                  -lfdcore \
                  -lfdproto \
                  -levhtp \
                  -levent_pthreads \
                  -levent \
                  -lcares \
                  -lboost_regex \
                  -lboost_system \
                  -lpthread \
                  -lcurl \
                  -lc \
                  -lboost_filesystem \
                  $(shell net-snmp-config --netsnmp-agent-libs)

# Only use the real SAS library in the production build.
homestead_LDFLAGS := ${COMMON_LDFLAGS} -lsas -lz

# Test build also uses libcurl (to verify HttpStack operation)
homestead_test_LDFLAGS := ${COMMON_LDFLAGS} -lcurl -ldl

# Use valgrind suppression file for UT
homestead_test_VALGRIND_ARGS := --suppressions=ut/homestead_test.supp

# Add modules/cpp-common/src as a VPATH to pull in required common modules
VPATH := ../modules/cpp-common/src ../modules/cpp-common/test_utils ut

include ../build-infra/cpp.mk

# Alarm definition generation rules
ROOT := ..
MODULE_DIR := ${ROOT}/modules
include ../modules/cpp-common/makefiles/alarm-utils.mk

../usr/include/homestead_alarmdefinition.h : ../build/bin/alarm_header ../homestead.root/usr/share/clearwater/infrastructure/alarms/homestead_alarms.json
	$< -j "../homestead.root/usr/share/clearwater/infrastructure/alarms/homestead_alarms.json" -n "homestead"
	mv homestead_alarmdefinition.h $@
${homestead_OBJECT_DIR}/main.o : ../usr/include/homestead_alarmdefinition.h
