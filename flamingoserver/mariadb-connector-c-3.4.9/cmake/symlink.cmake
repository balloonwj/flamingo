#
#  Copyright (C) 2013-2016 MariaDB Corporation AB
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the COPYING-CMAKE-SCRIPTS file.
#
MACRO(create_symlink symlink_name target install_path)
# According to cmake documentation symlinks work on unix systems only
IF(UNIX)
  # Get target components 
  ADD_CUSTOM_COMMAND(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${symlink_name}
    COMMAND ${CMAKE_COMMAND} ARGS -E remove -f ${symlink_name}
    COMMAND ${CMAKE_COMMAND} ARGS -E create_symlink $<TARGET_FILE_NAME:${target}> ${symlink_name}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DEPENDS ${target}
    )
  
  ADD_CUSTOM_TARGET(SYM_${symlink_name}
    ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${symlink_name})
  SET_TARGET_PROPERTIES(SYM_${symlink_name} PROPERTIES CLEAN_DIRECT_OUTPUT 1)

  IF(CMAKE_GENERATOR MATCHES "Xcode")
    # For Xcode, replace project config with install config
    STRING(REPLACE "${CMAKE_CFG_INTDIR}" 
      "\${CMAKE_INSTALL_CONFIG_NAME}" output ${CMAKE_CURRENT_BINARY_DIR}/${symlink_name})
  ENDIF()

  # presumably this will be used for libmysql*.so symlinks
  INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/${symlink_name} DESTINATION ${install_path}
          COMPONENT Development)
ENDIF()
ENDMACRO()
