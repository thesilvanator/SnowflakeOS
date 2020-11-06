#include <kernel/proc.h>
#include <kernel/timer.h>
#include <kernel/paging.h>
#include <kernel/pmm.h>
#include <kernel/gdt.h>
#include <kernel/fpu.h>
#include <kernel/fs.h>
#include <kernel/sys.h>

#include <kernel/sched_robin.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern uint32_t irq_handler_end;

process_t* current_process = NULL;
sched_t* scheduler = NULL;

static uint32_t next_pid = 1;

void init_proc() {
    scheduler = sched_robin();
}

/* Creates a process running the code specified at `code` in raw instructions
 * and add it to the process queue, after the currently executing process.
 * `argv` is the array of arguments, NULL terminated.
 */
process_t* proc_run_code(uint8_t* code, uint32_t size, char** argv) {
    static uintptr_t temp_page = 0;

    if (!temp_page) {
        temp_page = (uintptr_t) kamalloc(0x1000, 0x1000);
    }

    // Save arguments before switching directory and losing them
    list_t* args = list_new();

    while (argv && *argv) {
        list_add_front(args, strdup(*argv));
        argv++;
    }

    // Allocate one page more than the program size to accomodate static
    // variables.
    // TODO: this assumes .bss sections are marked as progbits
    uint32_t num_code_pages = divide_up(size, 0x1000);
    uint32_t num_stack_pages = PROC_STACK_PAGES;

    process_t* process = kmalloc(sizeof(process_t));
    uintptr_t kernel_stack = (uintptr_t) aligned_alloc(4, 0x1000 * PROC_KERNEL_STACK_PAGES);
    uintptr_t pd_phys = pmm_alloc_page();

    // Copy the kernel page directory with a temporary mapping
    page_t* p = paging_get_page(temp_page, false, 0);
    *p = pd_phys | PAGE_PRESENT | PAGE_RW;
    memcpy((void*) temp_page, (void*) 0xFFFFF000, 0x1000);
    directory_entry_t* pd = (directory_entry_t*) temp_page;
    pd[1023] = pd_phys | PAGE_PRESENT | PAGE_RW;

    // ">> 22" grabs the address's index in the page directory, see `paging.c`
    for (uint32_t i = 0; i < (KERNEL_BASE_VIRT >> 22); i++) {
        pd[i] = 0; // Unmap everything below the kernel
    }

    // We can now switch to that directory to modify it easily
    uintptr_t previous_pd = *paging_get_page(0xFFFFF000, false, 0) & PAGE_FRAME;
    paging_switch_directory(pd_phys);

    // Map the code and copy it to physical pages, zero out the excess memory
    // for static variables
    // TODO: don't require contiguous pages
    uintptr_t code_phys = pmm_alloc_pages(num_code_pages);
    paging_map_pages(0x00001000, code_phys, num_code_pages, PAGE_USER | PAGE_RW);
    memcpy((void*) 0x00001000, (void*) code, size);
    memset((uint8_t*) 0x1000 + size, 0, num_code_pages * 0x1000 - size);

    // Map the stack
    uintptr_t stack_phys = pmm_alloc_pages(num_stack_pages);
    paging_map_pages(0xC0000000 - 0x1000 * num_stack_pages, stack_phys,
        num_stack_pages, PAGE_USER | PAGE_RW);

    /* Setup the (argc, argv) part of the userstack, start by copying the given
     * arguments on that stack. */
    list_t* arglist = list_new();
    char* ustack_char = (char*) (0xC0000000 - 1);

    for (uint32_t i = 0; i < args->count; i++) {
        char* arg = list_get_at(args, i);
        uint32_t len = strlen(arg);

        // We need (ustack_char - len) to be 4-bytes aligned
        ustack_char -= ((uintptr_t) ustack_char - len) % 4;
        char* dest = ustack_char - len;

        strncpy(dest, arg, len);
        ustack_char -= len + 1; // Keep pointing to a free byte

        list_add(arglist, (void*) dest);
        kfree(arg);
    }

    kfree(args);

    /* Write `argv` to the stack with the pointers created previously.
     * Note that we switch to an int pointer; we're writing addresses here. */
    uint32_t* ustack_int = (uint32_t*) ((uintptr_t) ustack_char & ~0x3);

    for (uint32_t i = 0; i < arglist->count; i++) {
        char* arg = list_get_at(arglist, i);
        *(ustack_int--) = (uintptr_t) arg;
    }

    // Push program arguments
    uintptr_t argsptr = (uintptr_t) (ustack_int + 1);
    *(ustack_int--) = arglist->count ? argsptr : (uintptr_t) NULL;
    *(ustack_int--) = arglist->count;

    kfree(arglist);

    // Switch to the original page directory
    paging_switch_directory(previous_pd);

    *process = (process_t) {
        .pid = next_pid++,
        .code_len = num_code_pages,
        .stack_len = num_stack_pages,
        .directory = pd_phys,
        .kernel_stack = kernel_stack + PROC_KERNEL_STACK_PAGES * 0x1000 - 4,
        .saved_kernel_stack = kernel_stack + PROC_KERNEL_STACK_PAGES * 0x1000 - 4,
        .initial_user_stack = (uintptr_t) ustack_int,
        .mem_len = 0,
        .sleep_ticks = 0,
        .fds = list_new(),
        .cwd = strdup("/")
    };

    // We use this label as the return address from `proc_switch_process`
    uint32_t* jmp = &irq_handler_end;

    // Setup the process's kernel stack as if it had already been interrupted
    asm volatile (
        // Save our stack in %ebx
        "mov %%esp, %%ebx\n"

        // Temporarily use the new process's kernel stack
        "mov %[kstack], %%eax\n"
        "mov %%eax, %%esp\n"

        // Stuff popped by `iret`
        "push $0x23\n"         // user ds selector
        "mov %[ustack], %%eax\n"
        "push %%eax\n"         // %esp
        "push $0x202\n"        // %eflags with `IF` bit set
        "push $0x1B\n"         // user cs selector
        "push $0x00001000\n"   // %eip
        // Push error code, interrupt number
        "sub $8, %%esp\n"
        // `pusha` equivalent
        "sub $32, %%esp\n"
        // push data segment registers
        "mov $0x20, %%eax\n"
        "push %%eax\n"
        "push %%eax\n"
        "push %%eax\n"
        "push %%eax\n"

        // Push proc_switch_process's `ret` %eip
        "mov %[jmp], %%eax\n"
        "push %%eax\n"
        // Push garbage %ebx, %esi, %edi, %ebp
        "push $1\n"
        "push $2\n"
        "push $3\n"
        "push $4\n"

        // Save the new process's %esp in %eax
        "mov %%esp, %%eax\n"
        // Restore our stack
        "mov %%ebx, %%esp\n"
        // Update the new process's %esp
        "mov %%eax, %[esp]\n"
        : [esp] "=r" (process->saved_kernel_stack)
        : [kstack] "r" (process->kernel_stack),
          [ustack] "r" (process->initial_user_stack),
          [jmp] "r" (jmp)
        : "%eax", "%ebx"
    );

    scheduler->sched_add(scheduler, process);

    return process;
}

/* Terminates the currently executing process.
 * Implements the `exit` system call.
 */
void proc_exit_current_process() {
    printf("[proc] terminating process %d\n", current_process->pid);

    // Free allocated pages: code, heap, stack, page directory
    directory_entry_t* pd = (directory_entry_t*) 0xFFFFF000;

    for (uint32_t i = 0; i < 768; i++) {
        if (!(pd[i] & PAGE_PRESENT)) {
            continue;
        }

        uintptr_t page = pd[i] & PAGE_FRAME;
        pmm_free_page(page);
    }

    uintptr_t pd_page = pd[1023] & PAGE_FRAME;
    pmm_free_page(pd_page);

    // Free the kernel stack
    kfree((void*) (current_process->kernel_stack - 0x1000 * PROC_KERNEL_STACK_PAGES + 4));

    // Free the file descriptor list
    while (current_process->fds->count) {
        fs_close((uint32_t) list_first(current_process->fds));
        list_remove_at(current_process->fds, 0);
    }

    kfree(current_process->fds);

    // This last line is actually safe, and necessary
    scheduler->sched_exit(scheduler, current_process);
    proc_schedule();
}

/* Runs the scheduler. The scheduler may then decide to elect a new process, or
 * not.
 */
void proc_schedule() {
    process_t* next = scheduler->sched_next(scheduler);

    if (next == current_process) {
        return;
    }

    fpu_switch(current_process, next);
    proc_switch_process(next);
}

/* Called on clock ticks, calls the scheduler.
 */
void proc_timer_callback(registers_t* regs) {
    UNUSED(regs);

    proc_schedule();
}

/* Make the first jump to usermode.
 * A special function is needed as our first kernel stack isn't setup to return
 * to any interrupt handler; we have to `iret` ourselves.
 */
void proc_enter_usermode() {
    CLI(); // Interrupts will be reenabled by `iret`

    current_process = scheduler->sched_get_current(scheduler);

    if (!current_process) {
        printf("[proc] no process to run\n");
        abort();
    }

    timer_register_callback(&proc_timer_callback);
    gdt_set_kernel_stack(current_process->kernel_stack);
    paging_switch_directory(current_process->directory);

    asm volatile (
        "mov $0x23, %%eax\n"
        "mov %%eax, %%ds\n"
        "mov %%eax, %%es\n"
        "mov %%eax, %%fs\n"
        "mov %%eax, %%gs\n"
        "push %%eax\n"        // %ss
        "mov %[ustack], %%eax\n"
        "push %%eax\n"       // %esp
        "push $0x202\n"      // %eflags with IF set
        "push $0x1B\n"       // %cs
        "push $0x00001000\n" // %eip
        "iret\n"
        :: [ustack] "r" (current_process->initial_user_stack)
        : "%eax");
}

uint32_t proc_get_current_pid() {
    return current_process->pid;
}

/* Returns a dynamically allocated copy of the current process's current working
 * directory.
 */
char* proc_get_cwd() {
    return strdup(current_process->cwd);
}

void proc_sleep(uint32_t ms) {
    current_process->sleep_ticks = (uint32_t) ((ms*TIMER_FREQ)/1000.0);
    proc_schedule();
}

/* Extends the program's writeable memory by `size` bytes.
 * Note: the real granularity is by the page, but the program doesn't need the
 * details.
 */
void* proc_sbrk(intptr_t size) {
    uintptr_t end = 0x1000 + 0x1000*current_process->code_len + current_process->mem_len;

    // Bytes available in the last allocated page
    int32_t remaining_bytes = (end % 0x1000) ? (0x1000 - (end % 0x1000)) : 0;

    if (size > 0) {
        // We have to allocate more pages
        if (remaining_bytes < size) {
            uint32_t needed_size = size - remaining_bytes;
            uint32_t num = divide_up(needed_size, 0x1000);

            if (!paging_alloc_pages(align_to(end, 0x1000), num)) {
                return (void*) -1;
            }
        }
    } else if (size < 0) {
        if (end + size < 0x1000*current_process->code_len) {
            return (void*) -1; // Can't deallocate the code
        }

        int32_t taken = 0x1000 - remaining_bytes;

        // We must free at least a page
        if (taken + size < 0) {
            uint32_t freed_size = taken - size; // Account for sign
            uint32_t num = divide_up(freed_size, 0x1000);

            // Align to the beginning of the last mapped page
            uintptr_t virt = end - (end % 0x1000);

            for (uint32_t i = 0; i < num; i++) {
                paging_unmap_page(virt - 0x1000*i);
            }
        }
    }

    current_process->mem_len += size;

    return (void*) end;
}

int32_t proc_exec(const char* path, char** argv) {
    uint32_t fd = fs_open(path, O_RDONLY);

    // TODO: check it's not a directory

    if (fd == FS_INVALID_FD) {
        return -1;
    }

    fs_fseek(fd, 0, SEEK_END);
    uint32_t size = fs_ftell(fd);
    uint8_t* data = kmalloc(size);
    fs_fseek(fd, 0, SEEK_SET);
    fs_read(fd, data, size);
    fs_close(fd);

    proc_run_code(data, size, argv);

    return 0;
}

/* Returns whether the current process posesses the file descriptor.
 * TODO: include mode check?
 */
bool proc_has_fd(uint32_t fd) {
    return list_get_index_of(current_process->fds, (void*) fd) < current_process->fds->count;
}

uint32_t proc_open(const char* path, uint32_t mode) {
    uint32_t fd = fs_open(path, mode);

    if (fd != FS_INVALID_FD) {
        list_add_front(current_process->fds, (void*) fd);
    }

    return fd;
}

void proc_close(uint32_t fd) {
    uint32_t idx = list_get_index_of(current_process->fds, (void*) fd);

    if (idx < current_process->fds->count) {
        list_remove_at(current_process->fds, idx);
    }

    fs_close(fd);
}

int32_t proch_chdir(const char* path) {
    // TODO: replace existence check with `stat` or something
    char* npath = fs_normalize_path(path);
    uint32_t fd = fs_open(npath, O_RDONLY);

    if (fd == FS_INVALID_FD) {
        kfree(npath);
        return -1;
    }

    fs_close(fd);
    kfree(current_process->cwd);
    current_process->cwd = npath;

    return 0;
}