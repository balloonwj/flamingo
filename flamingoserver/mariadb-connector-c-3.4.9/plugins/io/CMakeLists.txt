IF (WITH_CURL)
  FIND_PACKAGE(CURL)
  IF(CURL_FOUND)

    add_compile_definitions(HAVE_REMOTEIO=1)
    #remote io plugin
    REGISTER_PLUGIN(TARGET remote_io
                  TYPE REMOTEIO
                  CONFIGURATIONS DYNAMIC STATIC OFF
                  DEFAULT DYNAMIC
                  SOURCES ${CC_SOURCE_DIR}/plugins/io/remote_io.c
                  INCLUDES ${CURL_INCLUDE_DIR}
                  LIBRARIES ${CURL_LIBRARIES})
  ENDIF()
ENDIF()
