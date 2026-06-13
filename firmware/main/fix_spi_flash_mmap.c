/**
 * @file fix_spi_flash_mmap.c
 * @brief Work around ESP-IDF 5.4.1 spi_flash_mmap/munmap fault with octal PSRAM (BOX-3).
 *
 * load_partitions() mmap's the partition table then munmap's it; on ESP32-S3 + OCT PSRAM
 * esp_mmu_unmap panics. Use esp_flash_read for that one-shot partition-table mapping.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"
#include "spi_flash_mmap.h"

#define PARTITION_MMAP_SENTINEL ((spi_flash_mmap_handle_t)0x50415254)  // 'PART'

#if CONFIG_SPIRAM && CONFIG_SPIRAM_MODE_OCT

static uint8_t* s_partition_mmap_buf;
static size_t s_partition_mmap_len;

static bool is_partition_table_mmap(size_t src_addr, size_t size, spi_flash_mmap_memory_t memory) {
    if (memory != SPI_FLASH_MMAP_DATA) {
        return false;
    }

    const uint32_t table_offset = CONFIG_PARTITION_TABLE_OFFSET;
    const uint32_t table_align = table_offset & ~(CONFIG_MMU_PAGE_SIZE - 1U);
    const size_t sector_size = 0x1000U;  // SPI_FLASH_SEC_SIZE

    if (size != sector_size) {
        return false;
    }

    return src_addr == table_align;
}

esp_err_t __real_spi_flash_mmap(size_t src_addr, size_t size, spi_flash_mmap_memory_t memory,
                                const void** out_ptr, spi_flash_mmap_handle_t* out_handle);

esp_err_t __wrap_spi_flash_mmap(size_t src_addr, size_t size, spi_flash_mmap_memory_t memory,
                                const void** out_ptr, spi_flash_mmap_handle_t* out_handle) {
    if (!is_partition_table_mmap(src_addr, size, memory)) {
        return __real_spi_flash_mmap(src_addr, size, memory, out_ptr, out_handle);
    }

    const uint32_t table_offset = CONFIG_PARTITION_TABLE_OFFSET;
    const uint32_t table_align = table_offset & ~(CONFIG_MMU_PAGE_SIZE - 1U);
    const uint32_t table_pad = table_offset - table_align;
    const size_t read_len = table_pad + size;

    if (s_partition_mmap_buf == NULL) {
        s_partition_mmap_buf = heap_caps_malloc(read_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (s_partition_mmap_buf == NULL) {
            return ESP_ERR_NO_MEM;
        }
        s_partition_mmap_len = read_len;
    }

    esp_err_t err = esp_flash_read(esp_flash_default_chip, s_partition_mmap_buf, src_addr, read_len);
    if (err != ESP_OK) {
        return err;
    }

    *out_ptr = s_partition_mmap_buf + table_pad;
    *out_handle = PARTITION_MMAP_SENTINEL;
    return ESP_OK;
}

void __real_spi_flash_munmap(spi_flash_mmap_handle_t handle);

void __wrap_spi_flash_munmap(spi_flash_mmap_handle_t handle) {
    if (handle == PARTITION_MMAP_SENTINEL) {
        return;
    }
    __real_spi_flash_munmap(handle);
}

#else

esp_err_t __wrap_spi_flash_mmap(size_t src_addr, size_t size, spi_flash_mmap_memory_t memory,
                                const void** out_ptr, spi_flash_mmap_handle_t* out_handle) {
    return __real_spi_flash_mmap(src_addr, size, memory, out_ptr, out_handle);
}

void __wrap_spi_flash_munmap(spi_flash_mmap_handle_t handle) {
    __real_spi_flash_munmap(handle);
}

#endif
