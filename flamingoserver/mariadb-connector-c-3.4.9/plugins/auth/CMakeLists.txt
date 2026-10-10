SET(AUTH_DIR ${CC_SOURCE_DIR}/plugins/auth)

INCLUDE_DIRECTORIES(${AUTH_DIR})
INCLUDE_DIRECTORIES(${CC_SOURCE_DIR}/include)

SET(CRYPTO_PLUGIN 1)

IF(WITH_SSL STREQUAL "OPENSSL")
  SET(CRYPT_SOURCE ${CC_SOURCE_DIR}/libmariadb/secure/openssl_crypt.c)
  SET(CRYPT_LIBS ${SSL_LIBRARIES})
ELSEIF(WITH_SSL STREQUAL "GNUTLS")
  SET(CRYPT_SOURCE ${CC_SOURCE_DIR}/libmariadb/secure/gnutls_crypt.c)
  INCLUDE(${CC_SOURCE_DIR}/cmake/FindNettle.cmake)
  INCLUDE(${CC_SOURCE_DIR}/cmake/FindHogweed.cmake)
  SET(CRYPT_LIBS ${SSL_LIBRARIES})
  IF (NETTLE_FOUND AND HOGWEED_FOUND)
    SET(PARSEC_EXTRA_LIBS ${NETTLE_LIBRARIES} ${HOGWEED_LIBRARIES})
  ENDIF()
ELSEIF(WITH_SSL STREQUAL "SCHANNEL")
  add_compile_definitions(HAVE_WINCRYPT)
  SET(CRYPT_SOURCE ${CC_SOURCE_DIR}/libmariadb/secure/win_crypt.c)
  SET(CRYPT_LIBS crypt32 bcrypt)
ELSE()
  UNSET(CRYPTO_PLUGIN)
ENDIF()

#native password
REGISTER_PLUGIN(TARGET mysql_native_password
                TYPE AUTH
                CONFIGURATIONS STATIC
                DEFAULT STATIC
                SOURCES ${AUTH_DIR}/my_auth.c)

#Dialog client authentication plugin
REGISTER_PLUGIN(TARGET dialog
                TYPE AUTH
                CONFIGURATIONS DYNAMIC STATIC OFF
                DEFAULT DYNAMIC
                LIBRARIES ${CMAKE_DL_LIBS}
                SOURCES ${CC_SOURCE_DIR}/plugins/auth/dialog.c
                        ${CC_SOURCE_DIR}/libmariadb/get_password.c)


# Crypto plugins
IF(CRYPTO_PLUGIN)

  #ED25519 client authentication plugin

  SET(REF10_DIR ${AUTH_DIR}/ref10)
  SET(REF10_SOURCES ${REF10_DIR}/fe_0.c ${REF10_DIR}/fe_isnegative.c ${REF10_DIR}/fe_sub.c ${REF10_DIR}/ge_p1p1_to_p2.c
                    ${REF10_DIR}/ge_p3_to_cached.c ${REF10_DIR}/open.c ${REF10_DIR}/fe_1.c ${REF10_DIR}/fe_isnonzero.c
                    ${REF10_DIR}/fe_tobytes.c ${REF10_DIR}/ge_p1p1_to_p3.c ${REF10_DIR}/ge_p3_to_p2.c ${REF10_DIR}/sc_muladd.c
                    ${REF10_DIR}/fe_add.c ${REF10_DIR}/fe_mul.c ${REF10_DIR}/ge_add.c ${REF10_DIR}/ge_p2_0.c ${REF10_DIR}/ge_precomp_0.c
                    ${REF10_DIR}/sc_reduce.c ${REF10_DIR}/fe_cmov.c ${REF10_DIR}/fe_neg.c ${REF10_DIR}/ge_double_scalarmult.c 
                    ${REF10_DIR}/ge_p2_dbl.c ${REF10_DIR}/ge_scalarmult_base.c ${REF10_DIR}/sign.c ${REF10_DIR}/fe_copy.c
                    ${REF10_DIR}/fe_pow22523.c ${REF10_DIR}/ge_frombytes.c ${REF10_DIR}/ge_p3_0.c ${REF10_DIR}/ge_sub.c
                    ${REF10_DIR}/verify.c ${REF10_DIR}/fe_frombytes.c ${REF10_DIR}/fe_sq2.c ${REF10_DIR}/ge_madd.c
                    ${REF10_DIR}/ge_p3_dbl.c ${REF10_DIR}/ge_tobytes.c ${REF10_DIR}/fe_invert.c ${REF10_DIR}/fe_sq.c
                    ${REF10_DIR}/ge_msub.c ${REF10_DIR}/ge_p3_tobytes.c ${REF10_DIR}/keypair.c)

  IF(MSVC)
     # Silence conversion (integer truncation) warnings from reference code
     SET_SOURCE_FILES_PROPERTIES(${REF10_SOURCES} PROPERTY COMPILE_FLAGS "/wd4244 /wd4146 -DHAVE_THREAD_LOCAL")
  ENDIF()

  INCLUDE_DIRECTORIES(${REF10_DIR})
  ADD_LIBRARY(ed25519_ref10 OBJECT ${REF10_SOURCES})
  SET_PROPERTY(TARGET ed25519_ref10 PROPERTY POSITION_INDEPENDENT_CODE ON)

  IF(WITH_SSL STREQUAL "SCHANNEL")
    SET(PARSEC_EXTRA_LIBS ed25519_ref10)
  ENDIF()

  REGISTER_PLUGIN(TARGET client_ed25519
                TYPE AUTH
                CONFIGURATIONS DYNAMIC STATIC OFF
                DEFAULT DYNAMIC
                SOURCES ${AUTH_DIR}/ed25519.c
                        ${CRYPT_SOURCE}
                INCLUDES ${REF10_DIR}
                LIBRARIES ${CRYPT_LIBS} ed25519_ref10)


  IF(CMAKE_C_COMPILER_ID MATCHES "GNU" AND CMAKE_C_COMPILER_VERSION LESS 11
     AND CMAKE_C_COMPILER_VERSION GREATER 6)
    SET_SOURCE_FILES_PROPERTIES(${REF10_SOURCES} PROPERTY COMPILE_FLAGS
                                -fno-sanitize=shift)
  ENDIF()

  # SHA256 caching plugin for MySQL 8.0 connection
  REGISTER_PLUGIN(TARGET caching_sha2_password
                TYPE AUTH
                CONFIGURATIONS DYNAMIC STATIC OFF
                DEFAULT DYNAMIC
                SOURCES ${AUTH_DIR}/caching_sha2_pw.c
                        ${CRYPT_SOURCE}
                LIBRARIES ${CRYPT_LIBS})

  IF(WITH_SSL STREQUAL "GNUTLS" AND NOT WIN32)
    MESSAGE1(SHA256_PASSWORD "sha256_password not supported by GnuTLS due to missing OAEP padding")
  ELSE()
    REGISTER_PLUGIN(TARGET sha256_password
                    TYPE AUTH
                    CONFIGURATIONS DYNAMIC STATIC OFF
                    DEFAULT DYNAMIC
                    SOURCES ${AUTH_DIR}/sha256_pw.c
                    LIBRARIES ${CRYPT_LIBS})
  ENDIF()

  IF(WITH_SSL STREQUAL "GNUTLS" AND (NOT NETTLE_FOUND OR NOT HOGWEED_FOUND))
    MESSAGE1(PARSEC "PARSEC requires nettle and hogweed libraries and headers when built with GnuTLS.")
  ELSEIF(WITH_SSL STREQUAL "OPENSSL" AND NOT HAVE_evp_pkey)
    MESSAGE1(PARSEC "PARSEC requires EVP_PKEY interface when built with OpenSSL.")
  ELSE()
    REGISTER_PLUGIN(TARGET parsec
            TYPE MARIADB_CLIENT_PLUGIN_AUTH
            CONFIGURATIONS DYNAMIC STATIC OFF
            DEFAULT DYNAMIC
            SOURCES ${AUTH_DIR}/parsec.c ${CRYPT_SOURCE}
            LIBRARIES ${CRYPT_LIBS} ${PARSEC_EXTRA_LIBS})
  ENDIF()

ENDIF()

#GSSAPI client authentication plugin
IF(NOT WIN32)
  INCLUDE(${CC_SOURCE_DIR}/cmake/FindGSSAPI.cmake)
  IF(GSSAPI_FOUND)
    SET(GSSAPI_SOURCES ${AUTH_DIR}/auth_gssapi_client.c ${AUTH_DIR}/gssapi_client.c ${AUTH_DIR}/gssapi_errmsg.c)
    IF (CMAKE_VERSION VERSION_GREATER 3.18)
      INCLUDE(CheckLinkerFlag)
      CHECK_LINKER_FLAG(C -Wl,--as-needed have__Wl___as_needed)
    ENDIF()
  ENDIF()
  SET(AUTH_GSSAPI_DEFAULT_CONFIG DYNAMIC)
ELSE()
  SET(GSSAPI_LIBS secur32)
  SET(GSSAPI_SOURCES ${AUTH_DIR}/auth_gssapi_client.c ${AUTH_DIR}/sspi_client.c ${AUTH_DIR}/sspi_errmsg.c)
  SET(AUTH_GSSAPI_DEFAULT_CONFIG DYNAMIC_AND_STATIC)
ENDIF()
IF(GSSAPI_SOURCES)
  REGISTER_PLUGIN(TARGET auth_gssapi_client
                  TYPE AUTH
                  CONFIGURATIONS DYNAMIC STATIC OFF DYNAMIC_AND_STATIC
                  DEFAULT ${AUTH_GSSAPI_DEFAULT_CONFIG}
                  SOURCES ${GSSAPI_SOURCES}
                  INCLUDES ${AUTH_DIR} ${GSSAPI_INCS}
                  LIBRARIES ${GSSAPI_LIBS})
  IF(CMAKE_C_COMPILER_ID MATCHES "Clang")
    SET_SOURCE_FILES_PROPERTIES(${GSSAPI_SOURCES} PROPERTY COMPILE_FLAGS "-Wno-deprecated-declarations")
  ENDIF()
  IF(have__Wl___as_needed AND TARGET auth_gssapi_client)
    SET_TARGET_PROPERTIES(auth_gssapi_client PROPERTIES LINK_FLAGS "-Wl,--as-needed")
  ENDIF()
ENDIF()

# old_password plugin
REGISTER_PLUGIN(TARGET mysql_old_password
                TYPE AUTH
                CONFIGURATIONS STATIC DYNAMIC OFF
                DEFAULT STATIC
                DISABLED YES
                SOURCES ${AUTH_DIR}/old_password.c)

# Cleartext
REGISTER_PLUGIN(TARGET mysql_clear_password
                TYPE AUTH
                CONFIGURATIONS DYNAMIC STATIC OFF
                DEFAULT DYNAMIC
                SOURCES ${AUTH_DIR}/mariadb_cleartext.c)

