idf_component_register(
    SRCS "main.c"
         "demo_bloub.c"
         "bloub_math.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_lcd esp_timer
)
