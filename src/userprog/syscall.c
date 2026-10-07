#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h" // For shutdown_power_off() if you implement SYS_HALT

static void syscall_handler (struct intr_frame *);

// Include the validation helper from Phase 2
static bool is_valid_user_ptr(const void *vaddr) {
    if (vaddr == NULL || !is_user_vaddr(vaddr)) {
        return false;
    }
    return pagedir_get_page(thread_current()->pagedir, vaddr) != NULL;
}

// A helper to safely terminate a process that passes bad memory
void exit_process(int status) {
    struct thread *cur = thread_current();
    printf("%s: exit(%d)\n", cur->name, status);
    // Note: Later in Phase 5 (Process Control), you will save this status 
    // to the thread struct so the parent process can read it via wait().
    thread_exit();
}

void syscall_init (void) {
    intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler (struct intr_frame *f) {
    // 1. Validate the stack pointer itself before reading the syscall number
    if (!is_valid_user_ptr(f->esp)) {
        exit_process(-1);
    }

    // 2. Dereference the stack pointer to get the syscall number
    int syscall_number = *(int *)(f->esp);

    // 3. Dispatch based on the syscall number
    switch (syscall_number) {
        case SYS_HALT:
            shutdown_power_off();
            break;

        case SYS_EXIT: {
            // Validate where the first argument (status) is stored
            if (!is_valid_user_ptr(f->esp + 4)) {
                exit_process(-1);
            }
            int status = *((int *)(f->esp + 4));
            exit_process(status);
            break;
        }

        case SYS_WRITE: {
            // Validate the locations of the 3 arguments: fd, buffer, size
            if (!is_valid_user_ptr(f->esp + 4) || 
                !is_valid_user_ptr(f->esp + 8) || 
                !is_valid_user_ptr(f->esp + 12)) {
                exit_process(-1);
            }

            int fd = *((int *)(f->esp + 4));
            const void *buffer = (const void *)*((uint32_t *)(f->esp + 8));
            unsigned size = *((unsigned *)(f->esp + 12));

            // We must also validate the buffer pointer the user passed us!
            if (!is_valid_user_ptr(buffer)) {
                exit_process(-1);
            }

            // File Descriptor 1 is standard output (the console)
            if (fd == 1) {
                // putbuf writes to the console. It breaks large buffers into chunks internally.
                putbuf(buffer, size);
                f->eax = size; // Return the number of bytes written
            } else {
                // For Phase 4, you will handle actual file writes here.
                f->eax = -1; 
            }
            break;
        }

        default:
            // Unhandled syscalls should terminate the process
            exit_process(-1);
            break;
    }
}