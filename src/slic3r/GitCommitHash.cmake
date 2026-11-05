# Writes GIT_COMMIT_HASH and GIT_COMMIT_SUFFIX into a generated header.
# GIT_COMMIT_SUFFIX is "-dirty" for a build with uncommitted changes, and empty
# otherwise.
#
# Also writes what the update check compares with the nightly manifest:
# GIT_COMMIT_TIME, the committer time of the commit in seconds since the epoch
# (0 when unknown), and ORCA_NIGHTLY_BUILD, 1 for a build CI uploads to the
# nightly release. CI sets orca_nightly_build=1 for those builds only, and
# git_commit_time where the source has no .git directory (Flatpak).
#
# A custom target runs this at the start of every build, which picks up a new
# commit without a reconfigure. The header is rewritten only when the value
# changes.
#
# Inputs: SOURCE_DIR, OUT_FILE.

find_package(Git QUIET)

set(HASH "")
set(SUFFIX "")

if (DEFINED ENV{git_commit_hash} AND NOT "$ENV{git_commit_hash}" STREQUAL "")
    if (GIT_FOUND AND EXISTS "${SOURCE_DIR}/.git")
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short "$ENV{git_commit_hash}"
            WORKING_DIRECTORY ${SOURCE_DIR} OUTPUT_VARIABLE HASH OUTPUT_STRIP_TRAILING_WHITESPACE)
    else ()
        # No .git directory (e.g. Flatpak sandbox) - truncate directly
        string(SUBSTRING "$ENV{git_commit_hash}" 0 7 HASH)
    endif ()
elseif (GIT_FOUND AND EXISTS "${SOURCE_DIR}/.git")
    execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%h
        WORKING_DIRECTORY ${SOURCE_DIR} OUTPUT_VARIABLE HASH OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND ${GIT_EXECUTABLE} diff --quiet HEAD
        WORKING_DIRECTORY ${SOURCE_DIR} RESULT_VARIABLE DIRTY ERROR_QUIET)
    if (DIRTY EQUAL 1)
        set(SUFFIX "-dirty")
    endif ()
endif ()

if (NOT HASH)
    set(HASH "0000000") # uninitialized
endif ()

set(TIME "")
if (GIT_FOUND AND EXISTS "${SOURCE_DIR}/.git" AND NOT HASH STREQUAL "0000000")
    execute_process(COMMAND ${GIT_EXECUTABLE} log -1 --format=%ct "${HASH}"
        WORKING_DIRECTORY ${SOURCE_DIR} OUTPUT_VARIABLE TIME OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
elseif (DEFINED ENV{git_commit_time})
    set(TIME "$ENV{git_commit_time}")
endif ()
if (NOT TIME MATCHES "^[0-9]+$")
    set(TIME "0")
endif ()

# A nightly build is one exact commit, so a build with changes on top of it is not one.
set(NIGHTLY "0")
if ("$ENV{orca_nightly_build}" STREQUAL "1" AND NOT SUFFIX)
    set(NIGHTLY "1")
endif ()

message(STATUS "Build commit: ${HASH}${SUFFIX}, committed ${TIME}, nightly: ${NIGHTLY}")

string(CONCAT CONTENT
    "#pragma once\n"
    "#define GIT_COMMIT_HASH \"${HASH}\"\n"
    "#define GIT_COMMIT_SUFFIX \"${SUFFIX}\"\n"
    "#define GIT_COMMIT_TIME ${TIME}LL\n"
    "#define ORCA_NIGHTLY_BUILD ${NIGHTLY}\n")

set(OLD "")
if (EXISTS "${OUT_FILE}")
    file(READ "${OUT_FILE}" OLD)
endif ()
if (NOT OLD STREQUAL CONTENT)
    file(WRITE "${OUT_FILE}" "${CONTENT}")
endif ()
