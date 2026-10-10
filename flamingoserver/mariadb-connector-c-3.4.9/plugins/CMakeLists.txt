SET(PLUGIN_EXTRA_FILES ${CC_SOURCE_DIR}/libmariadb/ma_errmsg.c)

IF(WIN32 OR (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  # Plugin exports handled via declspec(dllexport) or visibility attribute
  SET(OLD_CMAKE_C_VISIBILITY_PRESET ${CMAKE_C_VISIBILITY_PRESET})
  SET(CMAKE_C_VISIBILITY_PRESET hidden)
ENDIF()

FILE(GLOB plugin_dirs ${CC_SOURCE_DIR}/plugins/*)
FOREACH(dir ${plugin_dirs})
  IF (EXISTS ${dir}/CMakeLists.txt)
    INCLUDE(${dir}/CMakeLists.txt)
  ENDIF()
ENDFOREACH()

IF(WIN32 OR (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang"))
  # Plugin exports handled via declspec(dllexport) or visibility attribute
  IF(OLD_CMAKE_C_VISIBILITY_PRESET)
    SET(CMAKE_C_VISIBILITY_PRESET ${OLD_CMAKE_C_VISIBILITY_PRESET})
  ELSE()
    UNSET(CMAKE_C_VISIBILITY_PRESET)
  ENDIF()
ENDIF()
