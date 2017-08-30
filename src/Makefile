TARGETS := homestead
TEST_TARGETS := homestead_test

COMMON_SOURCES := a_record_resolver.cpp \
                  accesslogger.cpp \
                  accumulator.cpp \
                  alarm.cpp \
                  astaire_resolver.cpp \
                  base_communication_monitor.cpp \
                  base_hss_cache.cpp \
                  baseresolver.cpp \
                  base64.cpp \
                  cassandra_connection_pool.cpp \
                  cassandra_store.cpp \
                  communicationmonitor.cpp \
                  counter.cpp \
                  cx.cpp \
                  diameter_handlers.cpp \
                  diameter_hss_connection.cpp \
                  diameterstack.cpp \
                  diameterresolver.cpp \
                  dnscachedresolver.cpp \
                  dnsparser.cpp \
                  exception_handler.cpp \
                  http_handlers.cpp \
                  health_checker.cpp \
                  homestead_xml_utils.cpp \
                  hsprov_hss_connection.cpp \
                  hsprov_store.cpp \
                  hss_cache_processor.cpp \
                  hss_connection.cpp \
                  http_connection_pool.cpp \
                  httpclient.cpp \
                  httpconnection.cpp \
                  httpstack.cpp \
                  httpstack_utils.cpp \
                  impu_store.cpp \
                  load_monitor.cpp \
                  logger.cpp \
                  log.cpp \
                  memcachedstore.cpp \
                  memcached_cache.cpp \
                  memcached_connection_pool.cpp \
                  namespace_hop.cpp \
                  realmmanager.cpp \
                  saslogger.cpp \
                  sproutconnection.cpp \
                  statistic.cpp \
                  statisticsmanager.cpp \
                  snmp_agent.cpp \
                  snmp_row.cpp \
                  snmp_scalar.cpp \
                  utils.cpp \
                  xml_utils.cpp \
                  zmq_lvc.cpp

homestead_SOURCES := ${COMMON_SOURCES} \
                     main.cpp \
                     snmp_counter_table.cpp \
                     snmp_event_accumulator_table.cpp \
                     event_statistic_accumulator.cpp \
                     snmp_cx_counter_table.cpp

homestead_test_SOURCES := ${COMMON_SOURCES} \
                          test_main.cpp \
                          test_interposer.cpp \
                          base_ims_subscription_test.cpp \
                          cx_test.cpp \
                          diameter_handlers_test.cpp \
                          diameter_hss_connection_test.cpp \
                          fakelogger.cpp \
                          fakesnmp.cpp \
                          http_handlers_test.cpp \
                          homestead_xml_utils_test.cpp \
                          hsprov_hss_connection_test.cpp \
                          hsprov_store_test.cpp \
                          impu_store_test.cpp \
                          localstore.cpp \
                          memcachedcache_test.cpp \
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

# We need SAS in the test build as we use it's implementation of lz4
COMMON_LDFLAGS := -L../usr/lib \
                  -lmemcached \
                  -lmemcachedutil \
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
                  -lsas \
                  -lz \
                  -lboost_filesystem \
                  $(shell net-snmp-config --netsnmp-agent-libs)

homestead_LDFLAGS := ${COMMON_LDFLAGS}

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
