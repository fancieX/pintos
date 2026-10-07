#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t process_execute (const char *file_name) {
    char *fn_copy, *save_ptr;
    tid_t tid;

    // Make a copy of FILE_NAME.
    // Otherwise there's a race between the caller and load().
    fn_copy = palloc_get_page (0);
    if (fn_copy == NULL) return TID_ERROR;
    strlcpy (fn_copy, file_name, PGSIZE);

    // Extract the program name for the thread (first token)
    char thread_name[16];
    strlcpy(thread_name, file_name, sizeof thread_name);
    char *prog_name = strtok_r(thread_name, " ", &save_ptr);

    // Create a new thread to execute PROG_NAME, passing the FULL fn_copy string
    tid = thread_create (prog_name, PRI_DEFAULT, start_process, fn_copy);
    if (tid == TID_ERROR) palloc_free_page (fn_copy);
    return tid;
}

/* Helper function to push arguments onto the x86 user stack */
void push_arguments(char *cmd_line, void **esp) {
    int argc = 0;
    char *token, *save_ptr;
    char *argv[128]; 

    for (token = strtok_r(cmd_line, " ", &save_ptr); token != NULL; 
        token = strtok_r(NULL, " ", &save_ptr)) {
        
        *esp -= strlen(token) + 1; 
        strlcpy(*esp, token, strlen(token) + 1); 
        argv[argc++] = *esp; 
    }

    *esp = (void *)((uint32_t)(*esp) & ~3);

    *esp -= sizeof(char *);
    *((char **)*esp) = NULL;

    for (int i = argc - 1; i >= 0; i--) {
        *esp -= sizeof(char *);
        *((char **)*esp) = argv[i];
    }

    char **argv_addr = *esp;
    *esp -= sizeof(char **);
    *((char ***)*esp) = argv_addr;

    *esp -= sizeof(int);
    *((int *)*esp) = argc;

    *esp -= sizeof(void *);
    *((void **)*esp) = NULL;
}

static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  /* Extract executable name to pass to load(). */
  char fn_copy[64];
  char *save_ptr;
  strlcpy (fn_copy, file_name, sizeof fn_copy);
  char *prog_name = strtok_r (fn_copy, " ", &save_ptr);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;

  success = load (prog_name, &if_.eip, &if_.esp);
  printf ("DEBUG: load(%s) returned %d\n", prog_name, success);

  /* If load succeeded, push arguments onto the user stack. */
  if (success) {
    push_arguments (file_name, &if_.esp);
  }

  /* Free the page allocated in process_execute(). */
  palloc_free_page (file_name);

  /* If load failed, terminate the thread. */
  if (!success) {
    thread_exit ();
  }

  /* Start the user process. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status. */
int
process_wait (tid_t child_tid UNUSED) 
{
  /* Temporary delay loop so parent does not kill Pintos prematurely */
  for (volatile int i = 0; i < 500000000; i++);
  return -1;
}

/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  pd = cur->pagedir;
  if (pd != NULL) 
    {
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current thread. */
void
process_activate (void)
{
  struct thread *t = thread_current ();
  pagedir_activate (t->pagedir);
  tss_update ();
}

/* ELF definitions */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

#define PE32Wx PRIx32
#define PE32Ax PRIx32
#define PE32Ox PRIx32
#define PE32Hx PRIx16

struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_STACK   0x6474e551

#define PF_X 1
#define PF_W 2
#define PF_R 4

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  printf ("DEBUG: inside load() for '%s'\n", file_name);
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    {
      printf ("load: pagedir_create failed\n");
      goto done;
    }
  process_activate ();

  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable header\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          printf ("DEBUG: load segment unsupported type %d\n", phdr.p_type);
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                {
                  printf ("DEBUG: load_segment returned false\n");
                  goto done;
                }
            }
          else
            {
              printf ("DEBUG: validate_segment returned false\n");
              goto done;
            }
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    {
      printf ("DEBUG: setup_stack returned false\n");
      goto done;
    }

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  file_close (file);
  return success;
}

static bool install_page (void *upage, void *kpage, bool writable);

static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  if (phdr->p_memsz == 0)
    return false;
  
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  if (phdr->p_vaddr < PGSIZE)
    return false;

  return true;
}

static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        {
          printf ("DEBUG: load_segment: palloc_get_page returned NULL\n");
          return false;
        }

      int bytes_read = file_read (file, kpage, page_read_bytes);
      if (bytes_read != (int) page_read_bytes)
        {
          printf ("DEBUG: load_segment: file_read failed! Expected %zu, got %d (file_len=%d, ofs=%d)\n",
                  page_read_bytes, bytes_read, file_length (file), (int) ofs);
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      if (!install_page (upage, kpage, writable)) 
        {
          printf ("DEBUG: load_segment: install_page failed for upage %p\n", upage);
          palloc_free_page (kpage);
          return false; 
        }

      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}

static bool
setup_stack (void **esp) 
{
  uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage == NULL) {
    printf ("DEBUG: setup_stack: palloc_get_page failed!\n");
    return false;
  }

  success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
  if (success) {
    *esp = PHYS_BASE;
    printf ("DEBUG: setup_stack succeeded, *esp = %p\n", *esp);
  } else {
    printf ("DEBUG: setup_stack: install_page failed!\n");
    palloc_free_page (kpage);
  }
  return success;
}

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}