# Local fixes to the upstream submodules, kept as patch files in
# lib-patches/<submodule>/ and applied to the submodule's working tree at
# configure time (each only once: skipped when it already reverse-applies).
# A new or changed patch file makes the next build reconfigure, so pulling a
# patch and building is enough.
if (NOT GIT_EXECUTABLE)
    find_program(GIT_EXECUTABLE git REQUIRED)
endif()

function(bz_apply_lib_patches submodule)
    if (CMAKE_SCRIPT_MODE_FILE)
        # cmake -P (tools/setup-windows.ps1): no build to reconfigure.
        file(GLOB patches "${CMAKE_SOURCE_DIR}/lib-patches/${submodule}/*.patch")
    else()
        file(GLOB patches CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/lib-patches/${submodule}/*.patch")
        set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${patches})
    endif()
    list(SORT patches)
    foreach(patch IN LISTS patches)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace --reverse --check "${patch}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/lib/${submodule}"
            RESULT_VARIABLE already_applied OUTPUT_QUIET ERROR_QUIET)
        if (already_applied EQUAL 0)
            continue()
        endif()
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${patch}"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/lib/${submodule}"
            RESULT_VARIABLE result ERROR_VARIABLE error)
        if (NOT result EQUAL 0)
            message(FATAL_ERROR "Couldn't apply ${patch} to lib/${submodule}:\n${error}")
        endif()
        message(STATUS "Applied ${patch} to lib/${submodule}")
    endforeach()
endfunction()
