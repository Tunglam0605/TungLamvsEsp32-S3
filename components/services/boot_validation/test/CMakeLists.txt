idf_component_register(SRCS "test_boot_validation.c"
                       PRIV_INCLUDE_DIRS "../private_include"
                       REQUIRES unity boot_validation
                       WHOLE_ARCHIVE)
