idf_component_register(SRCS "src/protocol_types.c"
                             "src/app_event_queue.c"
                             "src/status.c"
                             "src/button_gate.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos)
