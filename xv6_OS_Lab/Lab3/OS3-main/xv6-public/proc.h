
#define MAX_PSYC_PAGES 15  //maximum number of process's pages in the physical memory
#define MAX_TOTAL_PAGES 30 // maximum number of pages for process.

struct page {
  uint virtualAddr; //Page's virtual address
  uint pagestate; /* pagestates: NOTUSED = 0, MAINMEMORY = 1, SWAPFILE = 2 */
  uint offset_in_file;         // If page is in file, this field stores its offset inside the swap file
  uint counter;     //counter to see when last used.
  uint pages_array_index;     // The page's index in the pages array

};


// Doubly linked list for pages currently in main memory
struct page_in_mem {
  struct page pg; //There will be a page
  struct page_in_mem *next; //pointer to next page in list
  struct page_in_mem *prev; //pointer to prev page in list
};

// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes)
  pde_t* pgdir;                // Page table
  char *kstack;                // Bottom of kernel stack for this process
  enum procstate state;        // Process state
  int pid;                     // Process ID
  struct proc *parent;         // Parent process
  struct trapframe *tf;        // Trap frame for current syscall
  struct context *context;     // swtch() here to run process
  void *chan;                  // If non-zero, sleeping on chan
  int killed;                  // If non-zero, have been killed
  struct file *ofile[NOFILE];  // Open files
  struct inode *cwd;           // Current directory
  char name[16];               // Process name (debugging)
  uint va;                     // Process virtual address;
  // bool va_set;                 // Process virtual address initialised or not
  // Every process will have a file to which it will write in case of swapping.
  struct file *swapFile;        

  // for every process following are necessary attributes
  struct page_in_mem paging_meta_data[MAX_TOTAL_PAGES];   //Array of paging meta_data
  struct page_in_mem *head_page_in_mem;    //header to the doubly linked list for the process
  uint num_pages_in_file;
  uint num_pages_in_main_mem;
};

void update_recently_accessed(void); 

// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
