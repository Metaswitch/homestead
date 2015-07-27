# Top level Makefile for Homestead

.PHONY: all
all:
	${MAKE} -f alarms-header.mk
	${MAKE} -f homestead.mk

.PHONY: test run_test coverage coverage-check coverage_raw debug vg vg-check vg_raw
test run_test coverage coverage-check coverage_raw debug vg vg-check vg_raw:
	${MAKE} -f homestead.mk $@

.PHONY: clean
clean:
	${MAKE} -f homestead.mk clean
	${MAKE} -f alarms-header.mk clean
