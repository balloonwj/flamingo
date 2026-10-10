SET(CLIENT_LIB_DIR ${CC_BINARY_DIR}/libmariadb/${CMAKE_BUILD_TYPE})
SET(CLIENT_BIN_DIR ${CC_BINARY_DIR}/client/${CMAKE_BUILD_TYPE})
SET(CLIENT_DBG_DIR ${CC_BINARY_DIR}/libmariadb/Debug)
SET(CLIENT_INC_DIR ${CC_SOURCE_DIR}/include)

SET(PRODUCT_NAME "MariaDB Connector C")
SET(PRODUCT_INSTALL_DIR "MariaDB")
SET(PRODUCT_MANUFACTURER "MariaDB Corporation")
SET(PRODUCT_VERSION "${CPACK_PACKAGE_VERSION}")

IF (${CMAKE_SIZEOF_VOID_P} EQUAL 8)
  SET(PRODUCT_NAME "${PRODUCT_NAME} 64-bit")
  SET(PLATFORM "win64")
  SET(IS_WIN64 "yes")
  SET(WIXPLATFORM "x64")
  SET(PRODUCT_UPGRADE_CODE "4E630B8C-4645-416D-A561-45D88E7BDCF1")
  SET(FOLDER "ProgramFiles64Folder")
ELSE()
  SET(PLATFORM "win32")
  SET(IS_WIN64 "no")
  SET(WIXPLATFORM "x86")
  SET(PRODUCT_UPGRADE_CODE "FCBC4419-07C8-4595-9803-DFD602A84F29")
  SET(FOLDER "ProgramFilesFolder")
ENDIF()

#
# process dynamic plugins
#
FOREACH(plugin ${PLUGINS_DYNAMIC})
  SET(TARGET ${plugin})
  # Get dependencies
  SET(DYNAMIC_TARGETS ${DYNAMIC_TARGETS} ${TARGET})
  SET(FILE ${CC_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${TARGET}.dll)
  SET(PDB ${CC_BINARY_DIR}/${CMAKE_BUILD_TYPE}/${TARGET}.pdb)
  MESSAGE(STATUS "Location for ${TARGET}: ${FILE}")
  # build file list
  STRING(REPLACE "$(Configuration)" "RelWithDebInfo" FILE ${FILE})
  SET(MARIADB_PLUGINS "${MARIADB_PLUGINS} <File Id=\"${TARGET}.dll\" Name=\"${TARGET}.dll\" DiskId=\"1\" Source=\"${FILE}\"/>\n")
  SET(MARIADB_PLUGINS "${MARIADB_PLUGINS} <File Id=\"${TARGET}.pdb\" Name=\"${TARGET}.pdb\" DiskId=\"1\" Source=\"${PDB}\"/>\n")
ENDFOREACH()

FOREACH(src ${WIX_INCLUDES})
  STRING(REPLACE "${CC_SOURCE_DIR}/include/" "" src ${src})
  # check binary dir
  STRING(REPLACE "${CC_BINARY_DIR}/include/" "" chgbin ${src})
  IF (${chgbin} STREQUAL ${src})
    SET(SRC_DIR ${CC_SOURCE_DIR})
  ELSE()
    SET(SRC_DIR ${CC_BINARY_DIR})
    STRING(REPLACE "${CC_BINARY_DIR}/include/" "" src ${src})
  ENDIF()
  STRING(REPLACE "-" "_" src_id ${src})
  STRING(REPLACE "mysql/" "" src_id ${src_id})
  STRING(REPLACE "mysql/" "" src_name ${src})
  STRING(REPLACE "mariadb/" "" src_name ${src_name})
  STRING(REPLACE "mariadb/" "" src_id ${src_id})
  IF(${src} MATCHES "mysql/")
    SET(MARIADB_INCLUDEMYSQL_FILES "${MARIADB_INCLUDEMYSQL_FILES} <File Id=\"${src_id}\" Name=\"${src_name}\"  DiskId=\"1\"  Source=\"${SRC_DIR}/include/${src}\"/>\n")
  ELSEIF (${src} MATCHES "mariadb/")
    SET(MARIADB_INCLUDEMARIADB_FILES "${MARIADB_INCLUDEMARIADB_FILES} <File Id=\"${src_id}\" Name=\"${src_name}\"  DiskId=\"1\"  Source=\"${SRC_DIR}/include/${src}\"/>\n")
  ELSE()
    SET(MARIADB_INCLUDE_FILES "${MARIADB_INCLUDE_FILES} <File Id=\"${src_id}\" Name=\"${src_name}\"  DiskId=\"1\"  Source=\"${SRC_DIR}/include/${src}\"/>\n")
  ENDIF()
ENDFOREACH()


IF(NOT WIX_DIR)
  SET(WIX_DIR $ENV{WIX}/bin)
ENDIF()

IF(NOT PACKAGE_STATUS_SUFFIX)
  SET(MSI_PACKAGE "mariadb-connector-c-${PRODUCT_VERSION}-${PLATFORM}.msi")
ELSE()
  SET(MSI_PACKAGE "mariadb-connector-c-${PRODUCT_VERSION}-${PACKAGE_STATUS_SUFFIX}-${PLATFORM}.msi")
ENDIF()

IF(WITH_SIGNCODE)
  IF(EXISTS "/tools/sign.bat")
    ADD_CUSTOM_TARGET(SIGNMSI
      DEPENDS ${MSI_PACKAGE}
      COMMAND /tools/sign.bat ${MSI_PACKAGE})
  ELSE()
    ADD_CUSTOM_TARGET(SIGNMSI
      DEPENDS ${MSI_PACKAGE}
      COMMAND signtool sign ${SIGN_OPTIONS} ${MSI_PACKAGE})
  ENDIF()
  ADD_DEPENDENCIES(SIGNMSI ${MSI_PACKAGE})
  SET_TARGET_PROPERTIES(SIGNMSI PROPERTIES EXCLUDE_FROM_ALL OFF)
ENDIF()

ADD_CUSTOM_TARGET(
        ${MSI_PACKAGE}
        DEPENDS WIXOBJ
#mariadb-connector-c.wixobj
        COMMAND ${WIX_DIR}/light.exe -ext WixUIExtension mariadb-connector-c.wixobj -o ${MSI_PACKAGE})

ADD_CUSTOM_TARGET(WIXOBJ
  DEPENDS mariadb-connector-c.xml}
        COMMAND ${WIX_DIR}/candle.exe mariadb-connector-c.xml -o mariadb-connector-c.wixobj)

SET_TARGET_PROPERTIES(${MSI_PACKAGE} PROPERTIES EXCLUDE_FROM_ALL OFF)
SET_TARGET_PROPERTIES(${WIXOBJ} PROPERTIES EXCLUDE_FROM_ALL OFF)
ADD_DEPENDENCIES(${MSI_PACKAGE} WIXOBJ)
ADD_DEPENDENCIES(WIXOBJ libmariadb mariadbclient ${DYNAMIC_TARGETS}) 

CONFIGURE_FILE(${CC_SOURCE_DIR}/win/packaging/mariadb-connector-c.xml.in
  ${CC_BINARY_DIR}/win/packaging/mariadb-connector-c.xml)
