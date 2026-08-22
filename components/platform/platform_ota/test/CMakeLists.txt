idf_component_register(SRCS "test_platform_ota.c"
                       PRIV_INCLUDE_DIRS "../private_include"
                       REQUIRES unity platform_ota
                       WHOLE_ARCHIVE)