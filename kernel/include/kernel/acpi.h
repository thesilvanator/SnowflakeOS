#pragma once

#include <stdint.h>

typedef struct acpi_rsdp1_t {
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_addr;
} __attribute__((packed)) acpi_rsdp1_t;

typedef struct acpi_rsdp2_t {
    acpi_rsdp1_t rsdp1;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t checksum;
    uint8_t reserved[3];
} __attribute__((packed)) acpi_rsdp2_t;
