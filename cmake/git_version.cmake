# Git version detection for Sally
# Generates git_version.h with version info from git tags
# Format: x.y.z-shorthash (e.g., 1.0.0-9c98a4b)

find_package(Git QUIET)

# Sally local development version base.
#
# Deliberately NOT a CACHE entry. `set(... CACHE ...)` only applies when the entry does
# not already exist, so once a build directory had been configured, bumping this line did
# nothing there - the stale cached value won and every build was stamped with the previous
# release's version. That is silent and survives rebuilds, so it is only noticed when
# someone reads the About box.
#
# As a normal variable it is re-evaluated on every configure, while an explicit
# -DSALLY_LOCAL_DEV_VERSION=x.y.z on the command line still wins because that defines the
# variable before this file runs.
# Sally local development version base. THIS LINE IS THE SINGLE SOURCE OF TRUTH.
#
# Keep it AHEAD of the newest released tag: v1.0.23 is published, so leaving it at 1.0.23
# would stamp unreleased builds with a version already shipped.
#
# It is deliberately neither a CACHE entry nor merely `if(NOT DEFINED)`-guarded, because
# both let a value cached by an EARLIER configure win, and that has now shipped wrong
# versions twice:
#
#   * `set(... CACHE STRING ...)` is skipped entirely when the entry already exists, so a
#     build directory configured before a bump kept emitting the old version forever.
#   * `if(NOT DEFINED ...)` is satisfied by a stale cache entry just as well as by a real
#     -D, so sibling build trees (the private test suite configures Sally sub-builds of its
#     own) silently kept their old value.
#
# The second one bites harder than it looks, because configure_file below writes into the
# SOURCE tree: every build directory shares one src/git_version.h, so whichever configured
# last decides the version for all of them. Making this line unconditional means they all
# agree, and the shared header stops being a race.
#
# If you genuinely need a different value, pass -DSALLY_VERSION_OVERRIDE=x.y.z. That is a
# distinct name, so it cannot be confused with a leftover cache entry.
if(SALLY_VERSION_OVERRIDE)
    set(SALLY_LOCAL_DEV_VERSION "${SALLY_VERSION_OVERRIDE}")
else()
    set(SALLY_LOCAL_DEV_VERSION "1.0.24")
endif()

if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    # Get the latest tag
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --match "v*" --abbrev=0
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_TAG_RESULT
    )

    # Get short commit hash
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT_SHORT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE GIT_HASH_RESULT
    )

    if(GIT_TAG_RESULT EQUAL 0 AND GIT_HASH_RESULT EQUAL 0)
        if(SALLY_LOCAL_DEV_VERSION)
            set(GIT_TAG_VERSION "${SALLY_LOCAL_DEV_VERSION}")
        else()
            # Strip leading 'v' if present (v1.0.0 -> 1.0.0)
            string(REGEX REPLACE "^v" "" GIT_TAG_VERSION "${GIT_TAG}")
        endif()

        # Check if we're exactly on the tag
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --match "v*" --exact-match
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_EXACT_TAG
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
            RESULT_VARIABLE GIT_EXACT_RESULT
        )

        if(GIT_EXACT_RESULT EQUAL 0)
            # Exactly on tag: just use version (e.g., 1.0.0)
            set(GIT_VERSION "${GIT_TAG_VERSION}")
        else()
            # Ahead of tag: use version-hash (e.g., 1.0.0-9c98a4b)
            set(GIT_VERSION "${GIT_TAG_VERSION}-${GIT_COMMIT_SHORT}")
        endif()

        set(GIT_VERSION_AVAILABLE TRUE)
        message(STATUS "Git version: ${GIT_VERSION}")
    else()
        set(GIT_VERSION_AVAILABLE FALSE)
    endif()
else()
    set(GIT_VERSION_AVAILABLE FALSE)
endif()

if(NOT GIT_VERSION_AVAILABLE)
    set(GIT_VERSION "")
    set(GIT_COMMIT_SHORT "")
    message(STATUS "Git version: not available (using static version)")
endif()

# Configure the header file
configure_file(
    "${CMAKE_SOURCE_DIR}/src/git_version.h.in"
    "${CMAKE_SOURCE_DIR}/src/git_version.h"
    @ONLY
)
