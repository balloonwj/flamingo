#
#  Copyright (C) 2013-2016 MariaDB Corporation AB
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the COPYING-CMAKE-SCRIPTS file.
#
MACRO(CREATE_EXPORT_FILE op outfile version symbols alias_version)
  IF(WIN32)
    SET(EXPORT_CONTENT "EXPORTS\n")
    FOREACH(exp_symbol ${symbols})
      SET(EXPORT_CONTENT ${EXPORT_CONTENT} "${exp_symbol}\n")
    ENDFOREACH()
  ELSE()
    SET(EXPORT_CONTENT "VERSION {\n${version} {\nglobal:\n")
    FOREACH(exp_symbol ${symbols})
      SET(EXPORT_CONTENT "${EXPORT_CONTENT} ${exp_symbol}\\;\n")
    ENDFOREACH()
    SET(EXPORT_CONTENT "${EXPORT_CONTENT}local:\n *\\;\n}\\;\n")
    IF ("${alias_version}" STRGREATER "")
      SET(EXPORT_CONTENT "${EXPORT_CONTENT}${alias_version} {\n}\\;\n}\\;\n")
      FOREACH(exp_symbol ${symbols})
        SET(EXPORT_CONTENT "${EXPORT_CONTENT}\"${exp_symbol}@${alias_version}\" = ${exp_symbol}\\;\n")
      ENDFOREACH()
    ELSE()
      SET(EXPORT_CONTENT "${EXPORT_CONTENT}}\\;\n")
    ENDIF()
  ENDIF()
  FILE(${op} ${CMAKE_CURRENT_BINARY_DIR}/${outfile} ${EXPORT_CONTENT})
ENDMACRO()
