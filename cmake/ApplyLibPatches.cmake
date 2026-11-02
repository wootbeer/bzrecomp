# Local fixes to the upstream submodules, kept as patch files in
# lib-patches/<submodule>/ and applied to the submodule's working tree at
# configure time. Patches can build on each other (a later one changing what
# an earlier one added), so they're applied as a set, in order: a stamp in
# the submodule's Git folder records which patches (by content) the working
# tree has, and a fingerprint of the patched files. When either changes -- a
# patch added, changed or removed, or the working tree reset by a checkout or
# submodule update -- the working tree is reset to the submodule's own sources
# and all of its patches are applied again. (So don't keep other edits in a
# patched submodule's working tree.) A new or changed patch file makes the
# next build reconfigure, so pulling a patch and building is enough.
if (NOT GIT_EXECUTABLE)
    find_program(GIT_EXECUTABLE git REQUIRED)
endif()

# A fingerprint of the working tree's changes: tracked files' diff (nested
# submodules left out: patching one doesn't change its parent) and the
# untracked files the patches added.
function(bz_lib_tree_fingerprint dir out)
    set(git "${GIT_EXECUTABLE}" -c safe.directory=*)
    execute_process(
        COMMAND ${git} diff --ignore-submodules
        WORKING_DIRECTORY "${dir}"
        OUTPUT_VARIABLE diff ERROR_QUIET)
    execute_process(
        COMMAND ${git} ls-files --others --exclude-standard
        WORKING_DIRECTORY "${dir}"
        OUTPUT_VARIABLE untracked ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REPLACE "\n" ";" untracked "${untracked}")
    set(text "${diff}")
    foreach(file IN LISTS untracked)
        if (NOT file STREQUAL "")
            file(SHA256 "${dir}/${file}" hash)
            string(APPEND text "${file} ${hash}\n")
        endif()
    endforeach()
    string(SHA256 fingerprint "${text}")
    set(${out} "${fingerprint}" PARENT_SCOPE)
endfunction()

function(bz_apply_lib_patches submodule)
    set(dir "${CMAKE_SOURCE_DIR}/lib/${submodule}")
    if (CMAKE_SCRIPT_MODE_FILE)
        # cmake -P (tools/setup-windows.ps1): no build to reconfigure.
        file(GLOB patches "${CMAKE_SOURCE_DIR}/lib-patches/${submodule}/*.patch")
    else()
        file(GLOB patches CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/lib-patches/${submodule}/*.patch")
        set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${patches})
    endif()
    list(SORT patches)
    if (NOT patches)
        return()
    endif()

    set(state "")
    foreach(patch IN LISTS patches)
        get_filename_component(name "${patch}" NAME)
        file(SHA256 "${patch}" hash)
        string(APPEND state "${name} ${hash}\n")
    endforeach()

    # safe.directory: Git refuses to work in a checkout it thinks another user
    # owns, which Windows reports for folders created as Administrator.
    set(git "${GIT_EXECUTABLE}" -c safe.directory=*)
    execute_process(
        COMMAND ${git} rev-parse --absolute-git-dir
        WORKING_DIRECTORY "${dir}"
        RESULT_VARIABLE result OUTPUT_VARIABLE git_dir ERROR_VARIABLE error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "Couldn't find the Git folder of ${dir}:\n${error}")
    endif()
    set(stamp "${git_dir}/bz-lib-patches.txt")
    if (EXISTS "${stamp}")
        file(READ "${stamp}" applied)
        bz_lib_tree_fingerprint("${dir}" fingerprint)
        if (applied STREQUAL "${state}tree ${fingerprint}\n")
            return()
        endif()
    endif()

    file(REMOVE "${stamp}")
    execute_process(
        COMMAND ${git} checkout -- .
        WORKING_DIRECTORY "${dir}"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "Couldn't reset lib/${submodule} to apply its patches:\n${error}")
    endif()
    # Files the patches added are untracked; remove them, or they can't be
    # added again.
    execute_process(
        COMMAND ${git} clean -fdq
        WORKING_DIRECTORY "${dir}"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "Couldn't clean lib/${submodule} to apply its patches:\n${error}")
    endif()
    foreach(patch IN LISTS patches)
        execute_process(
            COMMAND ${git} apply --ignore-whitespace "${patch}"
            WORKING_DIRECTORY "${dir}"
            RESULT_VARIABLE result ERROR_VARIABLE error)
        if (NOT result EQUAL 0)
            message(FATAL_ERROR "Couldn't apply ${patch} to lib/${submodule}:\n${error}")
        endif()
        message(STATUS "Applied ${patch} to lib/${submodule}")
    endforeach()
    bz_lib_tree_fingerprint("${dir}" fingerprint)
    file(WRITE "${stamp}" "${state}tree ${fingerprint}\n")
endfunction()
