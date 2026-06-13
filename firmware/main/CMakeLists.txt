idf_component_register(
    SRCS "app_main.cpp" "early_boot_log.c" "fix_spi_flash_mmap.c"
    INCLUDE_DIRS "."
    REQUIRES app_core esp_app_format spi_flash
    WHOLE_ARCHIVE
)

# BOX-3 octal PSRAM: bypass broken spi_flash_mmap for partition-table load (IDF 5.4.1).
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-Wl,--wrap=spi_flash_mmap"
    "-Wl,--wrap=spi_flash_munmap")
