file(GLOB_RECURSE SRC_FILES "src/*.c")

idf_component_register(SRCS ${SRC_FILES}
        INCLUDE_DIRS "include"
        EMBED_FILES "certs/ca_cert.pem")

set(COMPONENT_REQUIRES "spiffs")
spiffs_create_partition_image(ap_storage ./spiffs FLASH_IN_PROJECT)