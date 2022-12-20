#include <kernel/acpi.h>
#include <kernel/multiboot2.h>
#include <kernel/paging.h>
#include <kernel/sys.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct acpi_table_t {
    char sig[4];
    acpi_table_hdr_t* addr;
} acpi_table_t;

static bool determine_rsdp_version(mb2_t* boot);

static bool acpi_setup_mem(mb2_t* boot);
static bool acpi_find_mem(mb2_t* boot);
static bool acpi_map_mem(void);

static bool validate_rsdp(void);
static bool validate_rsdt(void);
static bool validate_table_hdr(acpi_table_hdr_t* hdr);
static bool validate_checksum(uint8_t* ptr, uint32_t size);

acpi_info_t acpi_info = {0};

acpi_table_t acpi_tables[] = {
    [ACPI_DMAR_TABLE] = {"DMAR", NULL},
};

acpi_info_t* get_acpi_info(void) {
    return &acpi_info;
}

bool init_acpi(mb2_t* boot) {
    printk("initializing ACPI");
    if (!determine_rsdp_version(boot))
        return false;

    if (!validate_rsdp())
        return false;

    if (!acpi_setup_mem(boot))
        return false;

    if (!validate_rsdt())
        return false;

    acpi_query_tables();

    return true;
}

// use multiboot to determine our acpi version and set our rsdp accordingly
static bool determine_rsdp_version(mb2_t* boot) {
    mb2_tag_rsdp1_t* mb2_rsdp_1;
    mb2_tag_rsdp2_t* mb2_rsdp_2;

    if (!(mb2_rsdp_2 = (mb2_tag_rsdp2_t*) mb2_find_tag(boot, MB2_TAG_RSDP2))) {
        if (!(mb2_rsdp_1 = (mb2_tag_rsdp1_t*) mb2_find_tag(boot, MB2_TAG_RSDP1))) {
            printke("No mb2 rsdp tag");
            return false;
        }

        acpi_info.type = ACPI_VER1;
        acpi_info.rsdp = &mb2_rsdp_1->rsdp;
    } else {
        acpi_info.type = ACPI_EXTD;
        acpi_info.rsdp = &mb2_rsdp_2->rsdp;
    }

    return true;
}

static bool acpi_setup_mem(mb2_t* boot) {
    if (!acpi_find_mem(boot))
        return false;

    if (!acpi_map_mem())
        return false;

    printk("offset is 0x%x", acpi_info.p2v_offset);

    return true;
}

/*
 * For some reason, the memory map provided by multiboot doesn't always indicate
 * the acpi region as type 0x3, sometimes (usually when qemu isn't using UEFI
 * and acpi v2.0+), it is just marked as type 0x2 reserved. This function looks
 * through the mem map regions provided by bios and captured by multiboot to see
 * if our rsdt points into one of these regions. If it does, we note that
 * region is the acpi region. If it is marked properly as type 0x3, this will be
 * captured in pmm on boot.
 */
static bool acpi_find_mem(mb2_t* boot) {
    bool found = true;
    // if pmm didn't find the mem region already
    if (!acpi_info.phys_base) {
        found = false;

        // need to walk what the bootloader gave us and see if
        // the rsdp points into one of the reserved memory locations
        mb2_tag_mmap_t* mm_tag = (mb2_tag_mmap_t*) mb2_find_tag(boot, MB2_TAG_MMAP);
        mb2_mmap_entry_t* entry;
        if (!mm_tag)
            return false;

        uint32_t mm_count = mm_tag->header.size / mm_tag->entry_size;
        for (uint32_t i = 0; i < mm_count; i++) {
            entry = &mm_tag->entries[i];
            uintptr_t entry_base = (uintptr_t) entry->base_addr;
            uint32_t entry_len = (uint32_t) entry->length;

            if ((uintptr_t) acpi_info.rsdt >= entry_base && (uintptr_t) acpi_info.rsdt <= entry_base + entry_len) {
                acpi_info.phys_base = entry_base;
                acpi_info.region_size = entry_len;
                found = true;
                break;
            }
        }
    }

    if (!found) {
        printke("couldn't find acpi memory region");
        return false;
    }

    printk("acpi phys mem region 0x%p - 0x%p", acpi_info.phys_base, acpi_info.phys_base + acpi_info.region_size);

    return true;
}

/*
 * pmm will mark acpi memory region as used, so it won't be overwritten.
 * However, we can't identity map it, because the vm addr might collide with
 * userspace or some other part of the kernel. Instead allocate a safe space in
 * the heap and then map the acpi region there.
 *
 * After this step addresses within the acpi region are usable and should only
 * be accessed through the ACPI_ADDR_P2V macro. For example the rsdt points to
 * physical memory. So after `acpi_map_mem` we can access and use
 * `ACPI_ADDR_P2V(acpi_info.rsdt)` to access it at its virtual address
 */
static bool acpi_map_mem(void) {
    uint32_t num_pages = divide_up(acpi_info.region_size, PAGE_SIZE);
    uintptr_t ptr = (uintptr_t) kamalloc(acpi_info.region_size, PAGE_SIZE);

    paging_unmap_pages(ptr, num_pages);
    paging_map_pages(ptr, acpi_info.phys_base, num_pages, 0);

    // update acpi_info offset and mem region base
    acpi_info.p2v_offset = ptr - acpi_info.phys_base;
    acpi_info.virt_base = ptr;

    printk("acpi virt mem region 0x%p - 0x%p", acpi_info.virt_base, acpi_info.virt_base + acpi_info.region_size);

    return true;
}

/*
 * Go through all the available tables in the ACPI memory region, validate their
 * checksum, and mark their location for later use. This is done so that the
 * tables don't have to be searched any time a subsystem wants a acpi table.
 * Instead, we just mark any acpi tables we want to capture in the `acpi_tables`
 * struct above, and then can use `acpi_get_table` in O(1) time.
 */
void acpi_query_tables(void) {
    uint32_t num_tables;
    uintptr_t tables;
    uint32_t offset;
    acpi_rsdt_t* rsdt = ACPI_ADDR_P2V(acpi_info.rsdt);

    if (acpi_info.type == ACPI_VER1)
        offset = 4;
    else if (acpi_info.type == ACPI_EXTD)
        offset = 8;
    else
        return;

    tables = (uintptr_t) rsdt->ptrs;
    num_tables = (rsdt->hdr.length - sizeof(acpi_table_hdr_t)) / offset;

    printk("available acpi tables: ");
    for (uint32_t i = 0; i < num_tables; i++) {
        bool captured = false;
        acpi_table_hdr_t* hdr;
        hdr = ACPI_ADDR_P2V(*((acpi_table_hdr_t**) (tables + offset * i)));

        if (!validate_table_hdr(hdr))
            continue;

        for (uint32_t j = 0; j < (sizeof(acpi_tables) / sizeof(acpi_table_t)); j++) {
            if (!strncmp(hdr->signature, acpi_tables[j].sig, 4)) {
                captured = true;
                acpi_tables[j].addr = hdr;
                break;
            }
        }

        printk("  hdr name: %.4s %s", hdr->signature, captured ? "- captured" : "");
    }
}

acpi_table_hdr_t* acpi_get_table(ACPI_Table_Type type) {
    if (type > (sizeof(acpi_tables) / sizeof(acpi_table_t)) - 1) {
        printke("trying to get invalid table");
        return NULL;
    }

    return acpi_tables[type].addr;
}

static bool validate_rsdp(void) {
    uint32_t size;
    char* sig = acpi_info.rsdp->signature;

    // validate signature
    if (strncmp(sig, "RSD PTR ", 8)) {
        printke("rsdp signature incorrect: %.8s", sig);
        return false;
    }

    if (acpi_info.type == ACPI_VER1)
        size = RSDP1_SIZE;
    else if (acpi_info.type == ACPI_EXTD)
        size = RSDP2_SIZE;
    else
        return false;

    if (!validate_checksum((uint8_t*) acpi_info.rsdp, size)) {
        printke("error validating rdsp: 0x%p", acpi_info.rsdp);
        return false;
    }

    // set our rsdt
    if (acpi_info.type == ACPI_VER1)
        acpi_info.rsdt = (acpi_rsdt_t*) acpi_info.rsdp->rsdt_addr;
    if (acpi_info.type == ACPI_EXTD)
        acpi_info.rsdt = (acpi_rsdt_t*) (uint32_t) acpi_info.rsdp->xsdt_addr;

    printk("type: 0x%x", acpi_info.type);
    printk("OEM ID: %.6s", acpi_info.rsdp->oem_id);
    printk("revision: 0x%x", acpi_info.rsdp->revision);

    return true;
}

static bool validate_rsdt(void) {
    bool ret = validate_table_hdr(&ACPI_ADDR_P2V(acpi_info.rsdt)->hdr);
    if (!ret)
        printke("error validating rdst");

    return ret;
}

static bool validate_table_hdr(acpi_table_hdr_t* hdr) {
    bool ret = validate_checksum((uint8_t*) hdr, hdr->length);
    if (!ret)
        printke("invalid system descriptor table: %.4s", hdr->signature);

    return ret;
}

static bool validate_checksum(uint8_t* ptr, uint32_t size) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size; i++) {
        sum += ptr[i];
    }

    if (sum & 0xFF) {
        printke("incorrect checksum - total sum was 0x%x", sum);
        return false;
    }

    return true;
}
