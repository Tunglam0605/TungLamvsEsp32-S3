idf_component_register(SRCS "test_ota_boot_validator.c"
                            "test_ota_policy.c"
                            "test_ota_https_source.c"
                       PRIV_INCLUDE_DIRS "../private_include"
                       REQUIRES unity callbox
                       WHOLE_ARCHIVE)
