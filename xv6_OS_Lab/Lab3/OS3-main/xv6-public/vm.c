#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

int mem2File(struct page_in_mem * pageToSwapFromMem);
struct page_in_mem * choosePageFromPhysMem(void);
void updateListPointers(struct page_in_mem * pageInList);
int writePageToFile(struct page_in_mem * pageToSwapToFile);
void set_present_clear_pagedout(pde_t *pgdir, char *virtAddr);
void addNewPageToPhysMem(char * addr);
int deletePage(struct page_in_mem * pageToDelete);

//In case of freevm --> should use the global system page directory

uint use_system_pgdir = 0;
pde_t *system_pgdir;

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0){
    return 0;
  }
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){

    if(myproc()->pid > 2){ // if it is a user process

      //check if exceeded maximum number of pages in physical memory,if so, write pages to file
      if(myproc()->num_pages_in_main_mem == MAX_PSYC_PAGES){
        if(myproc()->num_pages_in_file == (MAX_TOTAL_PAGES-MAX_PSYC_PAGES)){
          cprintf("allocuvm: Maximum limit of pages per process reached\n");
          return 0;
        }
        else if(myproc()->swapFile == 0){
          createSwapFile(myproc());
        }

        if(mem2File(choosePageFromPhysMem()) == -1){
          return 0;
        }
      }
    }

    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
    addNewPageToPhysMem((char *)a);
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  struct page_in_mem * pageInArray;
  pte_t *pte;int deletePage(struct page_in_mem * pageToDelete);

  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  int found;
  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    found = 0;
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
     { a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;}
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0){
        cprintf("deallocuvm: process %d is about to panic, pages in memory: %d, pages in file: %d\n", myproc()->pid, myproc()->num_pages_in_main_mem, myproc()->num_pages_in_file);
        panic("kfree");
      }
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
      if ((myproc()->pgdir == pgdir))
      {
        for (pageInArray = &myproc()->paging_meta_data[0]; pageInArray < &myproc()->paging_meta_data[MAX_TOTAL_PAGES]; pageInArray++)
        {
          if (pageInArray->pg.virtualAddr == a)     // If found (according to address), break
          {
            found = 1;
            break;
          }   
        }
        if (found)
        {
          deletePage(pageInArray);  
        }
      }
    }

    // Entry is not present --> checking if in swap file
    else if ((*pte & PTE_PG) && (pgdir == myproc()->pgdir)) 
    {
      uint vAddr = PTE_ADDR((uint) a);
      for (pageInArray = &myproc()->paging_meta_data[0]; pageInArray < &myproc()->paging_meta_data[MAX_TOTAL_PAGES]; pageInArray++)
      {
        if (pageInArray->pg.virtualAddr == vAddr)     // If found (according to address), break
        {
          found = 1;
          break;
        }   
      }

      // Found --> initialize page's field in array
      if (found)
      {
        myproc()->num_pages_in_file--;
        pageInArray->pg.virtualAddr = 0xFFFFFFFF;           
        pageInArray->pg.counter = 0;            
        pageInArray->pg.offset_in_file = -1;                
        pageInArray->pg.pagestate = 0;      
      }
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");

  use_system_pgdir = 1;
  system_pgdir = pgdir;
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  use_system_pgdir = 0;
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

void
addNewPageToPhysMem(char * addr)
{
  struct page_in_mem * tailPage = myproc()->head_page_in_mem;
  struct page_in_mem * pageInArray = 0;
  int page_array_index = 0;
  int found = 0;

  // If head of physical memory pages is null --> set it's virtual address 
  if (!tailPage)
  {
      pageInArray = &myproc()->paging_meta_data[0];
      myproc()->head_page_in_mem = pageInArray;
  }

  // Search for last link in physical memory pages list --> set the next link virtual address
  else
  {
    while (tailPage->next != 0)
    {
        tailPage = tailPage->next;
    }

    // Search if already exists entry in page directory 
    for(pageInArray = &myproc()->paging_meta_data[0]; pageInArray < &myproc()->paging_meta_data[MAX_TOTAL_PAGES]; pageInArray++)
    {
      if (pageInArray->pg.virtualAddr == PTE_ADDR((uint) addr))
      {
        found = 1;
        break;
      }   
      page_array_index++;
    } 

    // If entry doesn't exist, add a new page
    if (!found)
    {
      page_array_index = 0;
      for(pageInArray = &myproc()->paging_meta_data[0]; pageInArray < &myproc()->paging_meta_data[MAX_TOTAL_PAGES]; pageInArray++)
      {
        if(pageInArray->pg.pagestate == 0)
        {
          break;
        }   
        page_array_index++;
      } 
    }
   
    tailPage->next = pageInArray;
    pageInArray->prev = tailPage; 
    myproc()->paging_meta_data[tailPage->pg.pages_array_index] = *tailPage;
  }

  // Initialize page struct fields
  pageInArray->pg.pages_array_index = page_array_index;
  pageInArray->next = 0;
  pageInArray->pg.virtualAddr = (uint) addr;
  pageInArray->pg.pagestate = 1;
  pageInArray->pg.counter = 0;
  pageInArray->pg.offset_in_file = -1;
  
  // Update array of pages
  myproc()->paging_meta_data[page_array_index] = *pageInArray;

  myproc()->num_pages_in_main_mem++;   
}

void
clear_present_set_pagedout(pde_t *pgdir, char *virtAddr)
{
  pte_t *pte;
  pte = walkpgdir(pgdir, virtAddr, 0);
  if(pte == 0)
  {
    panic("clear_present_set_pagedout\n");
  }
  *pte &= ~PTE_P;                                // Clear PTE_P bit
  *pte = *pte | PTE_PG;                          // Set PTE_PG bit (indicating page is Paged Out to swap file)
}


int
mem2File(struct page_in_mem * pageToSwapFromMem)
{
  int check;
  pte_t *pte;

  // Check if should use the system global pgdir
  pte_t * pgdir = myproc()->pgdir;
  if (use_system_pgdir == 1)
  {
    pgdir = system_pgdir;
  }

  pte = walkpgdir(pgdir, (char*)pageToSwapFromMem->pg.virtualAddr, 0);

  // Write page to swap file from page's virtual address
  if ((check = writePageToFile(pageToSwapFromMem)) == -1)
  {
    return -1;
  }

  // Update list pointers
  updateListPointers(pageToSwapFromMem);

  clear_present_set_pagedout(pgdir, (char*) pageToSwapFromMem->pg.virtualAddr);      // Clear PTE_P and set PTE_PG bits

  uint pAddr = PTE_ADDR(*pte);
  char * vAddr = P2V(pAddr);
  kfree(vAddr);
  myproc()->num_pages_in_main_mem--;
  lcr3(V2P (pgdir));
  return 0;
}

void
updateListPointers(struct page_in_mem * pageInList){
  struct page_in_mem *previousPage;
  struct page_in_mem *nextPage;
  if(pageInList->prev != 0){   // Check if not head of list
    previousPage = pageInList->prev;
    previousPage->next = pageInList->next;

    // Not head and not tail (In the middle of the list)
    if (pageInList->next != 0)
    {
      nextPage = pageInList->next;
      nextPage->prev = previousPage;
      // proc->paging_meta_data[nextPage->pg.pages_array_index] = *nextPage;
    }
  }
  else{ // if this is the first page;

    myproc()->head_page_in_mem = pageInList->next;

    // More pages in list
    if (pageInList->next != 0)
    {
      nextPage = pageInList->next;
      nextPage->prev = 0;
      // proc->paging_meta_data[nextPage->pg.pages_array_index] = *nextPage;
    }
  }
  // Set pointer of current page
  pageInList->prev = 0;
  pageInList->next = 0;
}


struct page_in_mem *
choosePageFromPhysMem(void)
{
  struct page_in_mem *pageToRemove = myproc()->head_page_in_mem;
  struct page_in_mem *tmp;
   // If list is null (no page to swap out) --> return to calling function allocuvm
  if (!pageToRemove)
  {
    return 0;
  }

  // cprintf("choosePageFromPhysMem: BEFORE choosing. printList:\n");
  tmp = pageToRemove;
  uint minAccessTime = tmp->pg.counter;
  while(tmp->next != 0){
    tmp = tmp->next;
    if (tmp->pg.counter < minAccessTime)
      {
        pageToRemove = tmp;
        minAccessTime = tmp->pg.counter;
     }
  }
  return pageToRemove;
}

int
writePageToFile(struct page_in_mem * pageToSwapToFile){

  int check;
  int offsetExists = 0;
  // Check if should use the system global pgdir
  pte_t * pgdir = myproc()->pgdir;
  if (use_system_pgdir == 1)
  {
    pgdir = system_pgdir;
  }
  pte_t *pte;
  pte = walkpgdir(pgdir, (char*)pageToSwapToFile->pg.virtualAddr, 0);
  if (pte == 0)
  {
    panic ("writePageToFile: page should exist\n");   
  }
  if (!(*pte & PTE_P))
  {
    panic ("writePageToFile: page should be present\n"); 
  }
  // Write page to swap file (if offset is not 0, swap into the page's offset, else swap into the end of file)
  if (pageToSwapToFile->pg.offset_in_file > -1)
  {
    offsetExists = 1;
    if ((check = writeToSwapFile(myproc(), (char*) pageToSwapToFile->pg.virtualAddr, pageToSwapToFile->pg.offset_in_file, PGSIZE)) == -1)
    {
      cprintf("ERROR in writePageToFile first if\n");
      return -1;
    }
  }
  else if ((check = writeToSwapFile(myproc(), (char*) pageToSwapToFile->pg.virtualAddr, PGSIZE * (myproc()->num_pages_in_file), PGSIZE)) == -1)
  {
    cprintf("ERROR in writePageToFile second if\n");
    return -1;
  }

  // If new page into file --> set offset in file
  if (!offsetExists){
    pageToSwapToFile->pg.offset_in_file = PGSIZE * (myproc()->num_pages_in_file);
  }

  // Set state and flags
  pageToSwapToFile->pg.pagestate = 2;
  myproc()->num_pages_in_file++;
  cprintf("writePage: process %d, wrote page in virtual address 0x%x to File. Number of pages in file: %d\n\n", myproc()->pid, pageToSwapToFile->pg.virtualAddr, myproc()->num_pages_in_file);
  return 0;
}

int
file2mem(struct page_in_mem * pageToSwapFromFile)
{
  int check;
  int offset = pageToSwapFromFile->pg.offset_in_file;

  // Read page buffer from file to buffer 
  if (offset < 0)
  {
    cprintf("file2mem  : page not in file\n");
    return -1;
  }

  // Check if should use the system global pgdir
  pte_t * pgdir = myproc()->pgdir;
  if (use_system_pgdir == 1)
  {
    pgdir = system_pgdir;
  }

  pte_t * pte;
  pte = walkpgdir(pgdir, (void *)(PTE_ADDR(pageToSwapFromFile->pg.virtualAddr)), 0);
  char * memInPgdir;
  if((memInPgdir = kalloc()) == 0)
  {
    panic("file2mem: out of memory\n");
  }

  mappages(pgdir, (char *) pageToSwapFromFile->pg.virtualAddr, PGSIZE, V2P(memInPgdir), PTE_W | PTE_U);
  if ((check = readFromSwapFile(myproc(), memInPgdir, offset, PGSIZE)) == -1)
  {
    cprintf("file2mem: READ FAIL\n");
    return -1;
  }

  if ((pte = walkpgdir(pgdir, (void*)pageToSwapFromFile->pg.virtualAddr, 0)) == 0) {
    panic("file2mem: pte should exist");
  }
  
  set_present_clear_pagedout(pgdir, (char *) pageToSwapFromFile->pg.virtualAddr);

  myproc()->num_pages_in_main_mem++; 

  // Update page's fields
  // pageToSwapFromFile->pg.num_times_accessed = 0;
  pageToSwapFromFile->pg.offset_in_file = -1;
  pageToSwapFromFile->pg.pagestate = 1;

  myproc()->num_pages_in_file--;

  // Insert page to physical memory pages list
  struct page_in_mem * tailPage = myproc()->head_page_in_mem;
  while (tailPage->next != 0)
  {
      tailPage = tailPage->next;
  }
  tailPage->next = pageToSwapFromFile;
  pageToSwapFromFile->prev = tailPage;
  pageToSwapFromFile->next = 0;

  // Update array of pages
  // proc->paging_meta_data[tailPage->pg.pages_array_index] = *tailPage;
  // proc->paging_meta_data[pageToSwapFromFile->pg.pages_array_index] = *pageToSwapFromFile;
  return 0;
}


int
swapPages(uint virtAddr)
{
  cprintf("swapPages starts\n");
  struct page_in_mem * pageToSwapToFile = choosePageFromPhysMem();          // Get the chosen page by policy to remove from physical memory to the file
  struct page_in_mem * pageToSwapToMem;
  int found = 0;

  // Look for required page entry in paging meta data array
  for (pageToSwapToMem = &myproc()->paging_meta_data[0]; pageToSwapToMem < &myproc()->paging_meta_data[MAX_TOTAL_PAGES]; pageToSwapToMem++)
  {
    if (pageToSwapToMem->pg.virtualAddr == virtAddr)
    {
      found = 1;
      break;
    }   
  } 

  // If didn't find the required page, return -1 error
  if (!found)
  {
    return -1;
  }

  // Update the offset where to insert to file the page to swap out
  pageToSwapToFile->pg.offset_in_file = pageToSwapToMem->pg.offset_in_file;  

  // Read from swap file and write page to swap file
  file2mem(pageToSwapToMem);
  mem2File(pageToSwapToFile);
  
  
  

  // Check if should use the system global pgdir
  // pte_t * pgdir = myproc()->pgdir;
  // if (use_system_pgdir == 1)
  // {
  //   pgdir = system_pgdir;
  // }
  // set_PTE_A(pgdir,(char *) pageToSwapToMem->pg.virtualAddr);

  // cprintf("swapPages ends.\npage number %d --> file.\npage number %d with virtual address 0x%x--> physical memory.\n",pageToSwapToFile->pg.pages_array_index, pageToSwapToMem->pg.pages_array_index, pageToSwapToMem->pg.virtualAddr);
  cprintf("Swap Pages done \n");
  // printList();
  return 0;
}

int
deletePage(struct page_in_mem * pageToDelete)
{
  if (myproc()->num_pages_in_main_mem > 0)
  {
    myproc()->num_pages_in_main_mem--;
  }
  int found_PageFromFile = 0;

  // Initialize all page's entry in array fields
  pageToDelete->pg.virtualAddr = 0xFFFFFFFF;
  // pageToDelete->pg.num_times_accessed = 0;           // Initialize number of time page has been accessed
  pageToDelete->pg.pagestate = 0;                  // Page state
  pageToDelete->pg.offset_in_file = -1;              // If page is in file, this field stores its offset inside the swap file

  // If any pages on file --> bring a page from file into physical memory
  if (myproc()->num_pages_in_file > 0)
  {
    struct page_in_mem * bringPageFromFile;
    for (bringPageFromFile = &myproc()->paging_meta_data[0]; bringPageFromFile < &myproc()->paging_meta_data[MAX_TOTAL_PAGES]; bringPageFromFile++)
    {
      if (bringPageFromFile->pg.pagestate == 2)
      {
        found_PageFromFile = 1;
        break;
      }   
    } 
    if(found_PageFromFile)
    {
      file2mem(bringPageFromFile);
    } 
  }

  updateListPointers(pageToDelete);
  myproc()->paging_meta_data[pageToDelete->pg.pages_array_index] = *pageToDelete;
  // cprintf("deletePage process %d number of pages in mem = %d\n", proc->pid, proc->num_pages_in_phys_mem);
  return 0;
}

void
set_present_clear_pagedout(pde_t *pgdir, char *virtAddr)
{
  pte_t *pte;
  pte = walkpgdir(pgdir, virtAddr, 0);
  if(pte == 0)
  {
    panic("set_present_clear_pagedout\n");
  }
  *pte &= ~PTE_PG;                                // Clear PTE_PG bit
  *pte = *pte | PTE_P;                            // Set PTE_P bit (indicating page is Paged Out to swap file)
}