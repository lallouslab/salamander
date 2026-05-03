# Optional developer tools for Sally.
# These targets are intentionally excluded from the default build and release packaging.

include_guard(GLOBAL)

if(NOT WIN32)
  return()
endif()

set(SAL_DEVTOOLS_OUTPUT_DIR "${SAL_OUTPUT_BASE}/$<CONFIG>_${SAL_PLATFORM}/devtools")

function(sal_configure_dev_tool TARGET_NAME)
  set_target_properties(${TARGET_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${SAL_DEVTOOLS_OUTPUT_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${SAL_DEVTOOLS_OUTPUT_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${SAL_DEVTOOLS_OUTPUT_DIR}/bin"
  )

  if(MSVC)
    target_compile_options(${TARGET_NAME} PRIVATE /MP /W3 /J)
    target_link_options(${TARGET_NAME} PRIVATE /MANIFEST:NO)
  endif()
endfunction()

# utfnames: utility for creating and inspecting non-well-formed UTF-16 filenames.
set(SAL_UTFNAMES_MANIFEST_RC "${CMAKE_CURRENT_BINARY_DIR}/devtools/utfnames_manifest.rc")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/devtools")
file(WRITE "${SAL_UTFNAMES_MANIFEST_RC}"
  "#include <winuser.h>\n"
  "CREATEPROCESS_MANIFEST_RESOURCE_ID RT_MANIFEST \"${SAL_ROOT}/tools/utfnames/utfnames.manifest\"\n"
)

add_executable(utfnames EXCLUDE_FROM_ALL
  "${SAL_ROOT}/tools/utfnames/utfnames.cpp"
  "${SAL_UTFNAMES_MANIFEST_RC}"
)
target_compile_features(utfnames PRIVATE cxx_std_20)
target_compile_definitions(utfnames PRIVATE
  _CONSOLE
  WIN32
  WINVER=0x0601
  _WIN32_WINNT=0x0601
  $<$<CONFIG:Debug>:_DEBUG>
  $<${SAL_IS_RELEASE}:NDEBUG>
)
target_link_libraries(utfnames PRIVATE ntdll)
sal_configure_dev_tool(utfnames)

# salbreak: hidden hotkey helper that asks running Sally instances to break.
add_executable(salbreak WIN32 EXCLUDE_FROM_ALL
  "${SAL_ROOT}/tools/salbreak/md5.cpp"
  "${SAL_ROOT}/tools/salbreak/salbreak.cpp"
  "${SAL_ROOT}/tools/salbreak/tasklist.cpp"
  "${SAL_ROOT}/tools/salbreak/salbreak.rc"
)
target_include_directories(salbreak PRIVATE "${SAL_ROOT}/tools/salbreak")
target_compile_definitions(salbreak PRIVATE
  WIN32
  _WINDOWS
  WINVER=0x0601
  _WIN32_WINNT=0x0601
  $<$<CONFIG:Debug>:_DEBUG>
  $<${SAL_IS_RELEASE}:NDEBUG>
)
target_link_libraries(salbreak PRIVATE advapi32)
if(MSVC)
  set_target_properties(salbreak PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
endif()
sal_configure_dev_tool(salbreak)

# RegParser: standalone registry file parser/dumper built from reglib.
add_executable(regparser EXCLUDE_FROM_ALL
  "${SAL_SRC}/reglib/src/tester.cpp"
  "${SAL_SRC}/reglib/src/regparser.rc"
)
set_target_properties(regparser PROPERTIES OUTPUT_NAME "RegParser")
target_include_directories(regparser PRIVATE "${SAL_SRC}/reglib/src")
target_compile_definitions(regparser PRIVATE
  _CONSOLE
  ${SAL_COMMON_DEFINES}
  $<$<CONFIG:Debug>:${SAL_DEBUG_DEFINES}>
  $<${SAL_IS_RELEASE}:${SAL_RELEASE_DEFINES}>
)
target_link_libraries(regparser PRIVATE reglib)
sal_configure_dev_tool(regparser)
