# included mk file for the freeDiameter module

FREEDIAMETER_DIR := ${MODULE_DIR}/freeDiameter
FREEDIAMETER_BUILD_DIR := ${ROOT}/build/freeDiameter
FREEDIAMETER_MAKEFILE := ${FREEDIAMETER_BUILD_DIR}/build/Makefile

${FREEDIAMETER_BUILD_DIR}:
	mkdir -p ${FREEDIAMETER_BUILD_DIR}

${FREEDIAMETER_MAKEFILE}: ${FREEDIAMETER_BUILD_DIR}
	cd ${FREEDIAMETER_BUILD_DIR} && cmake ${FREEDIAMETER_DIR}

freeDiameter: ${FREEDIAMETER_MAKEFILE}
	make -C ${FREEDIAMETER_BUILD_DIR}
	#make -C ${FREEDIAMETER_DIR} install

freeDiameter_test: ${FREEDIAMETER_MAKEFILE}
	make -C ${FREEDIAMETER_BUILD_DIR} test

freeDiameter_clean: ${FREEDIAMETER_MAKEFILE}
	make -C ${FREEDIAMETER_BUILD_DIR} clean

freeDiameter_distclean:
	rm -rf ${FREEDIAMETER_BUILD_DIR}

.PHONY: freeDiameter freeDiameter_test freeDiameter_clean freeDiameter_distclean
