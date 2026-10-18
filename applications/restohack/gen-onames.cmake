# makedefs is a host utility, but this is a cross build: run it under `cmake -P`
# with a compiler chosen for the build machine, not the ARM target.
execute_process(COMMAND ${HOST_C_COMPILER} -O1 -o ${MAKEDEFS_BIN} ${MAKEDEFS_SRC}
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "could not compile host makedefs (${rc})")
endif()
execute_process(COMMAND ${MAKEDEFS_BIN} ${DEF_OBJECTS}
                OUTPUT_FILE ${OUT_FILE} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "makedefs ${DEF_OBJECTS} failed (${rc})")
endif()
