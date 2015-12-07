# included mk file for homestead

HOMESTEAD_DIR := ${ROOT}/src
HOMESTEAD_TEST_DIR := ${ROOT}/tests

homestead:
	${MAKE} -C ${HOMESTEAD_DIR}

homestead_test:
	${MAKE} -C ${HOMESTEAD_DIR} test

homestead_full_test:
	${MAKE} -C ${HOMESTEAD_DIR} full_test

homestead_clean:
	${MAKE} -C ${HOMESTEAD_DIR} clean

homestead_distclean: homestead_clean

.PHONY: homestead homestead_test homestead_clean homestead_distclean
