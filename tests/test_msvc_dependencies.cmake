foreach(source duck_avi stage_runner)
    execute_process(
        COMMAND "${NINJA}" -t deps "CMakeFiles/mbcore.dir/src/core/${source}.c.obj"
        WORKING_DIRECTORY "${BUILD_DIR}"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if (NOT result EQUAL 0 OR NOT output MATCHES "${source}\\.h")
        message(FATAL_ERROR "Ninja did not record ${source}.h: ${output} ${error}")
    endif()
endforeach()
