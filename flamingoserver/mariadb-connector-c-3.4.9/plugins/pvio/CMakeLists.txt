SET(PVIO_DIR ${CC_SOURCE_DIR}/plugins/pvio)

INCLUDE_DIRECTORIES(${PVIO_DIR})
INCLUDE_DIRECTORIES(${CC_SOURCE_DIR}/include)

#native password
REGISTER_PLUGIN(TARGET pvio_socket
                TYPE PVIO
                CONFIGURATIONS STATIC DYNAMIC DEFAULT
                DEFAULT STATIC
                SOURCES ${CC_SOURCE_DIR}/plugins/pvio/pvio_socket.c)

IF(WIN32)
  # named pipe
  REGISTER_PLUGIN(TARGET pvio_npipe
                TYPE PVIO
                CONFIGURATIONS STATIC DYNAMIC DEFAULT
                DEFAULT STATIC
                SOURCES ${CC_SOURCE_DIR}/plugins/pvio/pvio_npipe.c)

  # shared memory
  REGISTER_PLUGIN(TARGET pvio_shmem
                TYPE PVIO
                CONFIGURATIONS STATIC DYNAMIC DEFAULT
                DEFAULT DYNAMIC
                SOURCES ${CC_SOURCE_DIR}/plugins/pvio/pvio_shmem.c)
ENDIF()
