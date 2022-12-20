/*
 * This file adds support for Intel based IOMMUs whos information is gathered
 * via the ACPI DMAR table. This does not add support for AMD IOMMUs.
 */

#include <kernel/acpi.h>
#include <kernel/iommu.h>
#include <kernel/paging.h>
#include <kernel/sys.h>

#include <stdbool.h>

static void drhd_enumerate_device_scope(acpi_dmar_drhd_t* drhd);

bool init_iommu(void) {
    acpi_dmar_report_hdr_t* hdr;
    uintptr_t offset = 0;
    acpi_dmar_table_t* dmar = (acpi_dmar_table_t*) acpi_get_table(ACPI_DMAR_TABLE);

    printk("initalizing Intel IOMMU");

    if (!dmar) {
        printk("dmar table unavailable");
        return false;
    }

    printk("dmar is available - host address width: 0x%x", dmar->host_addr_width);
    printk("flags are 0x%x, len is 0x%x", dmar->flags, dmar->hdr.length);

    while (offset < dmar->hdr.length - sizeof(*dmar)) {
        hdr = (acpi_dmar_report_hdr_t*) (dmar->remapping_structs + offset);

        switch (hdr->type) {
        case IOMMU_DRHD:
            drhd_enumerate_device_scope((acpi_dmar_drhd_t*) hdr);
            break;
        default:
            printk("  unknown, unsupported, or reserved header type 0x%x", hdr->type);
        }

        offset += hdr->length;
    }
    return true;
}

static void drhd_enumerate_device_scope(acpi_dmar_drhd_t* drhd) {
    uint32_t offset = 0;
    acpi_dmar_dev_scope_entry_t* entry;

    printk("  DRHD found - addr: 0x%p size: 0x%x seg num 0x%x", (uint32_t) drhd->reg_base_addr,
        PAGE_SIZE << drhd->size, drhd->segment_num);
    printk("  flags are 0x%x, len is 0x%x", drhd->flags, drhd->hdr.length);
    while (offset < drhd->hdr.length - sizeof(*drhd)) {
        entry = (acpi_dmar_dev_scope_entry_t*) (drhd->dev_scope_entries + offset);

        printk("    device scope: type 0x%x start bus num: 0x%x path: 0x%x%x", entry->type,
            entry->start_bus_num, entry->path[0], entry->path[1]);
        offset += entry->length;
    }
}
