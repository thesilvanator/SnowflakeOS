#pragma once

#include <stdbool.h>
#include <stdint.h>

#define RSDP1_SIZE 20
#define RSDP2_SIZE 36

#define ACPI_ADDR_P2V(x) ((typeof(x)) (((uintptr_t) x) + acpi_info.p2v_offset))
#define ACPI_SIG_TO_U32(x) (*((uint32_t*) x))

typedef enum ACPI_Version {
    ACPI_INVALID = 0,
    ACPI_VER1 = 1,
    ACPI_EXTD = 2,
} ACPI_Version;

typedef enum ACPI_Table_Type {
    ACPI_FACP_TABLE = 0,
    ACPI_DMAR_TABLE,
} ACPI_Table_Type;

typedef struct acpi_rsdp_t {
    char signature[8];
    uint8_t checksum1;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_addr;
    uint32_t length;     // only availabe if ACPI v2.0+
    uint64_t xsdt_addr;  // only availabe if ACPI v2.0+
    uint8_t checksum2;   // only availabe if ACPI v2.0+
    uint8_t reserved[3]; // only availabe if ACPI v2.0+
} __attribute__((packed)) acpi_rsdp_t;

typedef struct acpi_table_hdr_t {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_rev;
    uint32_t creator_id;
    uint32_t creator_rev;
    uint8_t data[];
} __attribute__((packed)) acpi_table_hdr_t;

/*
 * This holds information regarding ACPI, its made so that the rsdp and rsdt
 * fields are generic regardless of if its ACPI 1.0 or 2.0+
 */
typedef struct acpi_info_t {
    ACPI_Version type;
    acpi_rsdp_t* rsdp;
    acpi_table_hdr_t* rsdt;
    uintptr_t phys_base;
    uintptr_t virt_base;
    uint32_t region_size; // size of mem region the firmware allocates for acpi
    uint32_t p2v_offset;  // offset to convert a acpi physical addr to virtual
} acpi_info_t;

#include <kernel/multiboot2.h>

bool init_acpi(mb2_t* boot);
acpi_info_t* get_acpi_info(void);
void acpi_query_tables(void);
acpi_table_hdr_t* acpi_get_table(ACPI_Table_Type type);
