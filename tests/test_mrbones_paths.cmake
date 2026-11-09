string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(ini "${TEST_DIR}/mrbones-path-test-${suffix}.ini")
file(WRITE "${ini}" "manifest=${TEST_DIR}/missing-manifest-ini.txt\n")
set(ENV{MB_CONFIG} "${ini}")
set(ENV{MB_DISC1} "")
set(ENV{MB_DISC2} "")
set(ENV{MB_MANIFEST} "")
execute_process(COMMAND "${EXE}" WORKING_DIRECTORY "${TEST_DIR}"
                RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 4)
if(NOT "${result}" STREQUAL "1" OR NOT error MATCHES "missing-manifest-ini")
    file(REMOVE "${ini}")
    message(FATAL_ERROR "Configured manifest ignored: ${result}; ${error}")
endif()

set(ENV{MB_DISC1} "${TEST_DIR}/missing-disc1-test.cue")
set(ENV{MB_MANIFEST} "${TEST_DIR}/missing-manifest-test.txt")

execute_process(COMMAND "${EXE}" WORKING_DIRECTORY "${TEST_DIR}"
                RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 4)
if(NOT "${result}" STREQUAL "1" OR NOT error MATCHES "missing-manifest-test")
    file(REMOVE "${ini}")
    message(FATAL_ERROR "No-argument manifest ignored: ${result}; ${error}")
endif()

file(REMOVE "${ini}")
set(ENV{MB_CONFIG} "${TEST_DIR}/missing-path-test.ini")
set(ENV{MB_MANIFEST} "${MANIFEST}")
execute_process(COMMAND "${EXE}" WORKING_DIRECTORY "${TEST_DIR}"
                RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 4)
if(NOT "${result}" STREQUAL "1" OR NOT error MATCHES "missing-disc1-test")
    message(FATAL_ERROR "No-argument disc 1 ignored: ${result}; ${error}")
endif()

set(ENV{MB_MANIFEST} "")
execute_process(COMMAND "${EXE}" WORKING_DIRECTORY "${TEST_DIR}"
                RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 4)
if(NOT "${result}" STREQUAL "1" OR NOT error MATCHES "missing-disc1-test")
    message(FATAL_ERROR "Default manifest was not found beside executable: ${result}; ${error}")
endif()

set(ENV{MB_MANIFEST} "${MANIFEST}")
set(ENV{MB_DISC1} "${DISC1}")
set(ENV{MB_DISC2} "${TEST_DIR}/missing-disc2-env.cue")
execute_process(COMMAND "${EXE}" WORKING_DIRECTORY "${TEST_DIR}"
                RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 10)
if(NOT "${result}" STREQUAL "1" OR NOT error MATCHES "missing-disc2-env")
    message(FATAL_ERROR "No-argument disc 2 ignored: ${result}; ${error}")
endif()

set(ENV{MB_DISC1} "${TEST_DIR}/missing-disc1-env.cue")
set(ENV{MB_MANIFEST} "${TEST_DIR}/missing-manifest-env.txt")
execute_process(COMMAND "${EXE}" "${DISC1}" "${MANIFEST}"
                --disc2 "${TEST_DIR}/missing-disc2-cli.cue"
                WORKING_DIRECTORY "${TEST_DIR}"
                RESULT_VARIABLE result ERROR_VARIABLE error TIMEOUT 10)
if(NOT "${result}" STREQUAL "1" OR NOT error MATCHES "missing-disc2-cli")
    message(FATAL_ERROR "Command-line precedence broken: ${result}; ${error}")
endif()
