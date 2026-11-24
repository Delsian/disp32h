idf_component_register(
    SRCS "matter_panel.cpp" "panel.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_timer nvs_flash openthread espressif__esp_matter
)
