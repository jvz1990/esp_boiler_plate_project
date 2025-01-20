file(GLOB_RECURSE SRC_FILES "src/*.c")

idf_component_register(SRCS ${SRC_FILES}
        INCLUDE_DIRS "include/boiler_plate"
        EMBED_FILES "ca_cert.pem")
