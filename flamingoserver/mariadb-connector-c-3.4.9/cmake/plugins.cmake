#
#  Copyright (C) 2013-2018 MariaDB Corporation AB
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the COPYING-CMAKE-SCRIPTS file.
#
# plugin configuration

include(${CC_SOURCE_DIR}/cmake/install_plugins.cmake)
include(${CC_SOURCE_DIR}/cmake/sign.cmake)

FUNCTION(REGISTER_PLUGIN)

  SET(one_value_keywords TARGET DISABLED TYPE DEFAULT)
  SET(multi_value_keywords CONFIGURATIONS SOURCES LIBRARIES INCLUDES COMPILE_OPTIONS)

  cmake_parse_arguments(CC_PLUGIN
                        "${options}"
                        "${one_value_keywords}"
                        "${multi_value_keywords}"
                        ${ARGN})

  # overwrite default if it was specified with cmake option
  string(TOUPPER ${CC_PLUGIN_TARGET} cc_plugin)
  if(NOT "${CLIENT_PLUGIN_${cc_plugin}}" STREQUAL "")
    SET(CC_PLUGIN_DEFAULT ${CLIENT_PLUGIN_${cc_plugin}})
  endif()

  # use uppercase
  string(TOUPPER ${CC_PLUGIN_TARGET} target_name)
  string(TOUPPER "${CC_PLUGIN_CONFIGURATIONS}" CC_PLUGIN_CONFIGURATIONS)

  if(NOT ${PLUGIN_${target_name}} STREQUAL "")
    string(TOUPPER ${PLUGIN_${target_name}} PLUGIN_${target_name})
    set(CC_PLUGIN_DEFAULT ${PLUGIN_${target_name}})
  endif()

# check if default value is valid
  string(TOUPPER ${CC_PLUGIN_DEFAULT} CC_PLUGIN_DEFAULT)
  list(FIND CC_PLUGIN_CONFIGURATIONS ${CC_PLUGIN_DEFAULT} configuration_found)
  if(${configuration_found} EQUAL -1)
    message(FATAL_ERROR "Invalid plugin type ${CC_PLUGIN_DEFAULT}. Allowed plugin types are ${CC_PLUGIN_CONFIGURATIONS}")
  endif()

# check if plugin is disabled
  string(TOUPPER "${CC_PLUGIN_DISABLED}" CC_PLUGIN_DISABLED)
  if("${CC_PLUGIN_DISABLED}" STREQUAL "YES")
    set(PLUGINS_DISABLED ${PLUGINS_DISABLED} ${CC_PLUGIN_TARGET} PARENT_SCOPE)
  endif()

  if(NOT ${CC_PLUGIN_DEFAULT} STREQUAL "OFF")
    set(PLUGIN_${CC_PLUGIN_TARGET}_TYPE ${CC_PLUGIN_TYPE} PARENT_SCOPE)

    if(${CC_PLUGIN_DEFAULT} MATCHES "DYNAMIC")

      set(PLUGINS_DYNAMIC ${PLUGINS_DYNAMIC} ${CC_PLUGIN_TARGET} PARENT_SCOPE)
      add_library(${CC_PLUGIN_TARGET} MODULE ${CC_PLUGIN_SOURCES})
      if(CMAKE_SYSTEM_NAME MATCHES "Linux")
        if(NOT WITH_ASAN AND NOT WITH_TSAN AND NOT WITH_UBSAN AND NOT WITH_MSAN)
          target_link_options(${CC_PLUGIN_TARGET} PRIVATE "-Wl,--no-undefined")
        endif()
        target_link_options(${CC_PLUGIN_TARGET} PRIVATE "-Wl,--dynamic-list=${CC_SOURCE_DIR}/plugins/dynamic_plugin_exports.txt")
      elseif(WIN32)
        set(target ${CC_PLUGIN_TARGET})
        set(FILE_TYPE "VFT_DLL")
        set(FILE_DESCRIPTION "MariaDB client plugin")
        set(FILE_VERSION ${CPACK_PACKAGE_VERSION})
        set(ORIGINAL_FILE_NAME "${target}.dll")
        configure_file(${CC_SOURCE_DIR}/win/resource.rc.in
                       ${CC_BINARY_DIR}/win/${target}.rc
                       @ONLY)
        target_sources(${CC_PLUGIN_TARGET} PRIVATE
                       ${CC_BINARY_DIR}/win/${target}.rc ${CC_SOURCE_DIR}/plugins/plugin.def)
      endif()
      target_link_libraries(${CC_PLUGIN_TARGET} ${CC_PLUGIN_LIBRARIES})
      set_target_properties(${CC_PLUGIN_TARGET} PROPERTIES PREFIX "")
      set_target_properties(${CC_PLUGIN_TARGET}
                                 PROPERTIES COMPILE_FLAGS
                                 "-DPLUGIN_DYNAMIC=1 ${CC_PLUGIN_COMPILE_OPTIONS}")
      if (NOT "${CC_PLUGIN_INCLUDES}" STREQUAL "")
        target_include_directories(${CC_PLUGIN_TARGET} PRIVATE ${CC_PLUGIN_INCLUDES})
      endif()
      if (${CC_TARGET_COMPILE_OPTIONS})
        target_compile_options(${CC_PLUGIN_TARGET} ${CC_TARGET_COMPILE_OPTIONS})
      endif()

      if(WIN32)
        SIGN_TARGET(${target})
      endif()
      INSTALL_PLUGIN(${CC_PLUGIN_TARGET} ${CMAKE_CURRENT_BINARY_DIR})
    endif()
    if(${CC_PLUGIN_DEFAULT} MATCHES "STATIC")
      set(PLUGINS_STATIC ${PLUGINS_STATIC} ${CC_PLUGIN_TARGET} PARENT_SCOPE)
      set(LIBMARIADB_PLUGIN_CFLAGS ${LIBMARIADB_PLUGIN_CFLAGS} ${CC_PLUGIN_COMPILE_OPTIONS} PARENT_SCOPE)
      set(LIBMARIADB_PLUGIN_INCLUDES ${LIBMARIADB_PLUGIN_INCLUDES} ${CC_PLUGIN_INCLUDES} PARENT_SCOPE)
      set(LIBMARIADB_PLUGIN_SOURCES ${LIBMARIADB_PLUGIN_SOURCES} ${CC_PLUGIN_SOURCES} PARENT_SCOPE)
      set(LIBMARIADB_PLUGIN_LIBS ${LIBMARIADB_PLUGIN_LIBS} ${CC_PLUGIN_LIBRARIES} PARENT_SCOPE)
    endif()
  else()
    set(PLUGINS_OFF ${PLUGINS_OFF} ${CC_PLUGIN_TARGET})
  endif()
endfunction()
