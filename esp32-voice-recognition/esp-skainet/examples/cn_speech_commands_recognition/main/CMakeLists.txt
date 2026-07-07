set(srcs
    main.c
    app_care_logic.c
    speech_commands_action.c
    audio/care_prompts.S
    )

set(requires
    driver
    hardware_driver
    )

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS include
                       REQUIRES ${requires})

component_compile_options(-w)
