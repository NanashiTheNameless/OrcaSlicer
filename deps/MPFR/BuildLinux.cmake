if (NOT DEFINED MPFR_SOURCE_DIR)
    message(FATAL_ERROR "MPFR_SOURCE_DIR is required")
endif ()

file(GLOB_RECURSE _mpfr_makefile_ins "${MPFR_SOURCE_DIR}/*/Makefile.in" "${MPFR_SOURCE_DIR}/Makefile.in")
if (_mpfr_makefile_ins)
    file(TOUCH ${_mpfr_makefile_ins})
endif ()

execute_process(
    COMMAND make -j
    WORKING_DIRECTORY "${MPFR_SOURCE_DIR}"
    RESULT_VARIABLE _mpfr_make_result
)

if (NOT _mpfr_make_result EQUAL 0)
    message(FATAL_ERROR "MPFR build failed with exit code ${_mpfr_make_result}")
endif ()
