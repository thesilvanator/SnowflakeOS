#pragma once
#include <kernel/acpi.h>

#include <stdbool.h>
#include <stdint.h>

typedef enum ACPI_DMAR_Remapping_Type {
    IOMMU_DRHD = 0,
    IOMMU_RMRR = 1,
    IOMMU_ATSR = 2,
    IOMMU_RHSA = 3,
    IOMMU_ANDD = 4,
    IOMMU_SATC = 5,
    IOMMU_SIDP = 6,
} ACPI_DMAR_Remapping_Type;

typedef enum ACPI_DMAR_Dev_Scope_Type {
    IOMMU_PCI_EP_DEV = 0x1,
    IOMMU_PCI_BRIDGE = 0x2,
    IOMMU_IOAPIC = 0x3,
    IOMMU_MSI_HPET = 0x4,
    IOMMU_ACPI_NS_DEV = 0x5,
} ACPI_DMAR_Dev_Scope_Type;

typedef struct acpi_dmar_table_t {
    acpi_table_hdr_t hdr;
    uint8_t host_addr_width;
    uint8_t flags;
    uint8_t reserved[10];
    uint8_t remapping_structs[];
} __attribute__((packed)) acpi_dmar_table_t;

typedef struct acpi_dmar_report_hdr_t {
    uint16_t type;
    uint16_t length;
} __attribute__((packed)) acpi_dmar_report_hdr_t;

typedef struct acpi_dmar_drhd_t {
    acpi_dmar_report_hdr_t hdr;
    uint8_t flags;
    uint8_t size;
    uint16_t segment_num;
    uint64_t reg_base_addr;
    uint8_t dev_scope_entries[];
} __attribute__((packed)) acpi_dmar_drhd_t;

typedef struct acpi_dmar_rmrr_t {
    acpi_dmar_report_hdr_t hdr;
    uint8_t flags;
    uint8_t size;
    uint16_t reserved;
    uint16_t segment_num;
    uint64_t rmr_base_addr;
    uint64_t rmr_limit_addr;
    uint8_t dev_scope_entries[];
} __attribute__((packed)) acpi_dmar_rmrr_t;

typedef struct acpi_dmar_dev_scope_entry_t {
    uint8_t type;
    uint8_t length;
    uint8_t flags;
    uint8_t reserved;
    uint8_t enumeration_id;
    uint8_t start_bus_num;
    uint8_t path[];
} __attribute__((packed)) acpi_dmar_dev_scope_entry_t;

bool init_iommu(void);
