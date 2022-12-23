/*
 * This file adds support for Intel based IOMMUs whos information is gathered
 * via the ACPI DMAR table. This does not add support for AMD IOMMUs.
 */

#include <kernel/acpi.h>
#include <kernel/iommu.h>
#include <kernel/paging.h>
#include <kernel/pci.h>
#include <kernel/sys.h>

#include <list.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static intel_iommu_t iommu;

static bool create_drhd_unit(acpi_dmar_drhd_t* hdr);
static void drhd_enumerate_pci_devs(dmar_drhd_unit_t* drhd);

bool init_iommu(void) {
    acpi_dmar_report_hdr_t *hdr, *next;
    acpi_dmar_report_hdr_t *start, *end;
    acpi_dmar_table_t* dmar = (acpi_dmar_table_t*) acpi_get_table(ACPI_DMAR_TABLE);

    printk("Initalizing Intel IOMMU");

    if (!dmar) {
        printke("dmar table unavailable");
        return false;
    }

    iommu.drhd_units = LIST_HEAD_INIT(iommu.drhd_units);
    iommu.dmar = dmar;

    printk("dmar is available - host address width: 0x%x flags: 0x%x len: 0x%x",
        dmar->host_addr_width, dmar->flags, dmar->hdr.length);
    printk("querying remapping structures: ");

    /*
     * loop through each dmar remapping entry
     */
    start = (acpi_dmar_report_hdr_t*) dmar->remapping_structs;
    end = (acpi_dmar_report_hdr_t*) ((uintptr_t) dmar + dmar->hdr.length);
    for (hdr = start; hdr < end; hdr = next) {
        next = (acpi_dmar_report_hdr_t*) (((uintptr_t) hdr) + hdr->length);

        switch (hdr->type) {
        case IOMMU_DRHD:
            create_drhd_unit((acpi_dmar_drhd_t*) hdr);
            break;
        default:
            printke("  unknown, unsupported, or reserved header type 0x%x", hdr->type);
        }
    }

    return true;
}

static bool create_drhd_unit(acpi_dmar_drhd_t* hdr) {
    static uint8_t next_id = 0;
    dmar_drhd_unit_t* drhd;
    uint32_t reg_size = PAGE_SIZE << hdr->size;

    if (next_id >= IOMMU_MAX_DRHD_UNITS - 1) {
        printke("Too many DRHD units");
        return false;
    }

    if (hdr->flags & DMAR_INCLUDE_PCI_ALL) {
        printke("DMAR DRHD INCLUDE_PCI_ALL not supported");
        return false;
    }

    drhd = (dmar_drhd_unit_t*) kmalloc(sizeof(dmar_drhd_unit_t));

    if (!drhd)
        return false;

    drhd->id = next_id++;
    drhd->acpi_struct = hdr;
    drhd->pci_devs = LIST_HEAD_INIT(drhd->pci_devs);
    drhd->reg_base_addr_virt = (volatile void*) kamalloc(reg_size, reg_size);

    if (!drhd->reg_base_addr_virt) {
        printke("error creating remapping drhd to virtual address");
        goto cleanup;
    }

    // remap the register for access
    paging_unmap_pages((uintptr_t) drhd->reg_base_addr_virt, reg_size / PAGE_SIZE);
    paging_map_pages((uintptr_t) drhd->reg_base_addr_virt, drhd->acpi_struct->reg_base_addr,
        reg_size / PAGE_SIZE, PAGE_RW);

    printk("  DRHD 0x%x found - reg addr: 0x%p reg size: 0x%x seg num 0x%x", drhd->id,
        (uint32_t) drhd->acpi_struct->reg_base_addr, PAGE_SIZE << drhd->acpi_struct->size,
        drhd->acpi_struct->segment_num);

    drhd_enumerate_pci_devs(drhd);

    if (!list_add(&iommu.drhd_units, drhd)) {
        printke("unable to add drhd to iommu list");
        goto cleanup;
    }
    return true;

cleanup:
    kfree(drhd);
    return false;
}

static void drhd_enumerate_pci_devs(dmar_drhd_unit_t* drhd) {
    acpi_dmar_dev_scope_entry_t *entry, *next;
    acpi_dmar_dev_scope_entry_t *start, *end;
    pci_device_t* dev;

    /*
     * loop through each device scope entry in a drhd remapping struct
     */
    start = (acpi_dmar_dev_scope_entry_t*) drhd->acpi_struct->dev_scope_entries;
    end = (acpi_dmar_dev_scope_entry_t*) ((uintptr_t) drhd->acpi_struct + drhd->acpi_struct->hdr.length);
    for (entry = start; entry < end; entry = next) {
        next = (acpi_dmar_dev_scope_entry_t*) ((uintptr_t) entry + entry->length);

        if (entry->type != IOMMU_PCI_EP_DEV) {
            printke("    unable to add device type 0x%x - only pci endpoint devices are supported",
                entry->type);
            continue;
        }
        dev = pci_find_device(entry->start_bus_num, entry->path[0], entry->path[1]);
        if (!dev) {
            printke("    couldn't find pci dev at location: %x:%x:%x", entry->start_bus_num,
                entry->path[0], entry->path[1]);
            continue;
        }

        printk("    found pci dev %d", dev->id);
        if (!list_add(&drhd->pci_devs, dev)) {
            printke("unable to add pci dev %d", dev->id);
            continue;
        }
        drhd->dev_count++;
    }
}
