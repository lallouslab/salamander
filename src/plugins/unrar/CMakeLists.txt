# Plugin build definition for Sally
# Plugin: unrar

set(UNRAR_SOURCE_DIR "${SAL_PLUGINS}/unrar/unrarsrc")

set(UNRAR_RUNTIME_SOURCES
  "${UNRAR_SOURCE_DIR}/archive.cpp"
  "${UNRAR_SOURCE_DIR}/arcread.cpp"
  "${UNRAR_SOURCE_DIR}/blake2s.cpp"
  "${UNRAR_SOURCE_DIR}/cmddata.cpp"
  "${UNRAR_SOURCE_DIR}/consio.cpp"
  "${UNRAR_SOURCE_DIR}/crc.cpp"
  "${UNRAR_SOURCE_DIR}/crypt.cpp"
  "${UNRAR_SOURCE_DIR}/dll.cpp"
  "${UNRAR_SOURCE_DIR}/encname.cpp"
  "${UNRAR_SOURCE_DIR}/errhnd.cpp"
  "${UNRAR_SOURCE_DIR}/extinfo.cpp"
  "${UNRAR_SOURCE_DIR}/extract.cpp"
  "${UNRAR_SOURCE_DIR}/filcreat.cpp"
  "${UNRAR_SOURCE_DIR}/file.cpp"
  "${UNRAR_SOURCE_DIR}/filefn.cpp"
  "${UNRAR_SOURCE_DIR}/filestr.cpp"
  "${UNRAR_SOURCE_DIR}/find.cpp"
  "${UNRAR_SOURCE_DIR}/getbits.cpp"
  "${UNRAR_SOURCE_DIR}/global.cpp"
  "${UNRAR_SOURCE_DIR}/hash.cpp"
  "${UNRAR_SOURCE_DIR}/headers.cpp"
  "${UNRAR_SOURCE_DIR}/isnt.cpp"
  "${UNRAR_SOURCE_DIR}/largepage.cpp"
  "${UNRAR_SOURCE_DIR}/match.cpp"
  "${UNRAR_SOURCE_DIR}/motw.cpp"
  "${UNRAR_SOURCE_DIR}/options.cpp"
  "${UNRAR_SOURCE_DIR}/pathfn.cpp"
  "${UNRAR_SOURCE_DIR}/qopen.cpp"
  "${UNRAR_SOURCE_DIR}/rar.cpp"
  "${UNRAR_SOURCE_DIR}/rarpch.cpp"
  "${UNRAR_SOURCE_DIR}/rarvm.cpp"
  "${UNRAR_SOURCE_DIR}/rawread.cpp"
  "${UNRAR_SOURCE_DIR}/rdwrfn.cpp"
  "${UNRAR_SOURCE_DIR}/rijndael.cpp"
  "${UNRAR_SOURCE_DIR}/rs.cpp"
  "${UNRAR_SOURCE_DIR}/rs16.cpp"
  "${UNRAR_SOURCE_DIR}/scantree.cpp"
  "${UNRAR_SOURCE_DIR}/secpassword.cpp"
  "${UNRAR_SOURCE_DIR}/sha1.cpp"
  "${UNRAR_SOURCE_DIR}/sha256.cpp"
  "${UNRAR_SOURCE_DIR}/smallfn.cpp"
  "${UNRAR_SOURCE_DIR}/strfn.cpp"
  "${UNRAR_SOURCE_DIR}/strlist.cpp"
  "${UNRAR_SOURCE_DIR}/system.cpp"
  "${UNRAR_SOURCE_DIR}/threadpool.cpp"
  "${UNRAR_SOURCE_DIR}/timefn.cpp"
  "${UNRAR_SOURCE_DIR}/ui.cpp"
  "${UNRAR_SOURCE_DIR}/unicode.cpp"
  "${UNRAR_SOURCE_DIR}/unpack.cpp"
  "${UNRAR_SOURCE_DIR}/volume.cpp"
)

add_library(unrar_engine OBJECT ${UNRAR_RUNTIME_SOURCES})

target_include_directories(unrar_engine PRIVATE "${UNRAR_SOURCE_DIR}")
target_compile_definitions(unrar_engine PRIVATE
  RARDLL
  UNRAR
  SILENT
  _MBCS
)

if(MSVC)
  target_compile_options(unrar_engine PRIVATE /MP /W3 /wd4007 /wd4996)
endif()

sal_add_plugin(NAME unrar
  SOURCES
    "${SAL_SHARED}/dbg.cpp"
    "${SAL_PLUGINS}/unrar/dialogs.cpp"
    "${SAL_PLUGINS}/unrar/precomp.cpp"
    "${SAL_PLUGINS}/unrar/unrar.cpp"
    $<TARGET_OBJECTS:unrar_engine>
  RC "${SAL_PLUGINS}/unrar/unrar.rc"
  DEF "${SAL_PLUGINS}/unrar/unrar.def"
  INCLUDES "${SAL_SHARED}/plugcore"
  NO_SHARED
)

# The RARLAB UnRAR engine is statically linked into this plugin.
set_target_properties(plugin_unrar PROPERTIES OUTPUT_NAME "salunrar")

add_custom_command(TARGET plugin_unrar POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_FILE_DIR:plugin_unrar>/unrar.dll"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "$<TARGET_FILE_DIR:plugin_unrar>/unrar.pdb"
)
