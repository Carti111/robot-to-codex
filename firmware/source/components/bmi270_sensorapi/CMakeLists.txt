idf_component_register(
    SRCS "bmi2.c" "bmi270.c"
    INCLUDE_DIRS "."
    EMBED_FILES "bmi260-init-data.fw"
)
