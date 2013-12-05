# included mk file for homestead

HOMESTEAD_DIR := ${ROOT}/src
HOMESTEAD_TEST_DIR := ${ROOT}/tests

homestead:
	make -C ${HOMESTEAD_DIR}

homestead_test:
	make -C ${HOMESTEAD_DIR} test

homestead_clean:
	make -C ${HOMESTEAD_DIR} clean

homestead_distclean: homestead_clean

.PHONY: homestead homestead_test homestead_clean homestead_distclean
