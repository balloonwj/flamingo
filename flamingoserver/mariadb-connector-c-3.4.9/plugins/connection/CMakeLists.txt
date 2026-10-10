# Replication
REGISTER_PLUGIN(TARGET replication
                TYPE CONNECTION
                CONFIGURATIONS STATIC DYNAMIC OFF
                DEFAULT OFF
                SOURCES ${CC_SOURCE_DIR}/plugins/connection/replication.c)
