#
## Helper invoked via "cmake -P" to run the exotic test generator.
##
## add_custom_command() runs commands without a shell, so it cannot redirect
## stdout to a file. execute_process() can, hence this wrapper.
##
## Expects: EXOTIC, SRCDIR, OUTPUT, INPUTS (a ;-separated list)
#

if(NOT EXOTIC OR NOT SRCDIR OR NOT OUTPUT OR NOT INPUTS)
    message(FATAL_ERROR "RunExotic.cmake: EXOTIC, SRCDIR, OUTPUT and INPUTS are all required")
endif()

execute_process(
    COMMAND ${EXOTIC} --standalone ${INPUTS}
    WORKING_DIRECTORY ${SRCDIR}
    OUTPUT_FILE ${OUTPUT}
    ERROR_VARIABLE exotic_stderr
    RESULT_VARIABLE exotic_result)

if(NOT exotic_result EQUAL 0)
    # Do not leave a truncated file behind to be picked up as valid output.
    file(REMOVE ${OUTPUT})
    message(FATAL_ERROR "exotic failed (${exotic_result}): ${exotic_stderr}")
endif()
