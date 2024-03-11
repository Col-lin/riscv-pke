/*
 * contains the implementation of all syscalls.
 */

#include <stdint.h>
#include <errno.h>

#include "util/types.h"
#include "syscall.h"
#include "string.h"
#include "process.h"
#include "util/functions.h"
#include "pmm.h"
#include "vmm.h"
#include "spike_interface/spike_utils.h"
#include "memlayout.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  // buf is now an address in user space of the given app's user stack,
  // so we have to transfer it into phisical address (kernel is running in direct mapping).
  assert( current );
  char* pa = (char*)user_va_to_pa((pagetable_t)(current->pagetable), (void*)buf);
  sprint(pa);
  return 0;
}

//
// implement the SYS_user_exit syscall
//
ssize_t sys_user_exit(uint64 code) {
  sprint("User exit with code:%d.\n", code);
  // in lab1, PKE considers only one app (one process).
  // therefore, shutdown the system when the app calls exit()
  shutdown(code);
}

//
// maybe, the simplest implementation of malloc in the world ... added @lab2_2
//

/*
uint64 sys_user_allocate_page() {
  void* pa = alloc_page();
  uint64 va = g_ufree_page;
  g_ufree_page += PGSIZE;
  user_vm_map((pagetable_t)current->pagetable, va, PGSIZE, (uint64)pa,
         prot_to_type(PROT_WRITE | PROT_READ, 1));
//    sprint("%lx\n",va);
  return va;
}
 */
typedef struct MCB_t {
    uint64 PG_INX;          // address of the first physical page of the mcb
    uint64 MCB_va;          // va of the mcb
    uint64 MCB_pa;          // pa of the mcb
    uint64 MCB_size;        // size of the mcb , include 2 parts : mcb_index and data part
    struct MCB_t* MCB_last;     // chain structure
    struct MCB_t* MCB_next;
}MCB;

MCB* mcb_head = NULL;

uint64 allocate_new_page(uint64 va, uint64 size) {
    uint64 page_s = ROUNDDOWN(va, PGSIZE);
    char *head = NULL;
//    sprint("va: %lx\npage_s: %lx\nUFAS: %lx\nsize: %lx\n", va, page_s, USER_FREE_ADDRESS_START, size);
//    sprint(":::%lx\n:::%lx\n",va+size,page_s+PGSIZE);
    for(int i = va; i < va + size; i += PGSIZE) {
//        sprint("----------%lx--------\n",ROUNDDOWN(i, PGSIZE));
        uint64 pte= lookup_pa(current->pagetable, i);
        char * tmp = NULL;
        if ((void *)pte == NULL) {
            tmp = alloc_page();
//            sprint("tmp:%lx\n",tmp);
            map_pages(current->pagetable, ROUNDDOWN(i, PGSIZE), PGSIZE,
                      (uint64)tmp, prot_to_type(PROT_READ | PROT_WRITE, 1));
        }
        else
            tmp = (char *)pte;
        if(ROUNDDOWN(i, PGSIZE) == page_s) {
            head = tmp + va - i;
//            sprint("tmp&head:\n%lx\n%lx\n",pte,head);
        }
    }
    return (uint64)head;
}

uint64 uint64_min(uint64 a, uint64 b) {
    return a<b?a:b;
}

uint64 addr_align(uint64 adddr) {
    uint64 align = adddr % 8;
    return adddr + 8 - align;
}

uint64 sys_user_allocate_page(uint64 size) {
    uint64 addr = USER_FREE_ADDRESS_START;
    if ((void *)mcb_head == NULL)  {
        uint64 pa = allocate_new_page(USER_FREE_ADDRESS_START, size + sizeof(MCB));
        mcb_head = (MCB *)pa;
//        sprint("-------------%lx--------------\n",pa);
        mcb_head->MCB_last = NULL;
        mcb_head->MCB_next = NULL;
        mcb_head->MCB_size = size;
        mcb_head->MCB_va = addr;
        mcb_head->MCB_pa = pa;
        mcb_head->PG_INX = pa;
        return (uint64)addr + sizeof(MCB);
    }
    MCB * cur = mcb_head;
    if(mcb_head->MCB_va != USER_FREE_ADDRESS_START) {
        uint64 empty_size = uint64_min(PGSIZE, mcb_head->MCB_va - USER_FREE_ADDRESS_START);
        if (empty_size >= size + sizeof(MCB)) {
            uint64 pa;
            if (mcb_head->MCB_va - USER_FREE_ADDRESS_START > PGSIZE) {
                pa = allocate_new_page(USER_FREE_ADDRESS_START, size + sizeof(MCB));
            } else {
                pa = mcb_head->PG_INX;
            }
            cur = (MCB *)pa;
            cur->MCB_va = USER_FREE_ADDRESS_START;
            cur->MCB_pa = pa;
            cur->PG_INX = pa;
            cur->MCB_size = size;
            cur->MCB_last = NULL;
            cur->MCB_next = mcb_head;
            mcb_head->MCB_last = cur;
            mcb_head = cur;
            return (uint64)mcb_head->MCB_va + sizeof(MCB);
        }
    }
    while((void *)cur->MCB_next != NULL) {
        addr = addr_align(cur->MCB_va + cur->MCB_size + sizeof(MCB));
        uint64 empty_size = cur->MCB_next->MCB_va - (uint64)addr;
        empty_size = uint64_min(empty_size,
                                ((cur->MCB_va >> 12) << 12 ) + PGSIZE - (uint64)addr);
        if (empty_size < size + sizeof(MCB)) {
            cur = cur->MCB_next;
        } else {
            uint64 pa = allocate_new_page(addr, size + sizeof(MCB));
            MCB * tmp = (MCB *)pa;
            tmp->MCB_next = cur->MCB_next;
            tmp->MCB_last = cur;
            (tmp->MCB_next)->MCB_last = tmp;
            cur->MCB_next = tmp;
            tmp->MCB_size = size;
            tmp->PG_INX = (uint64)PTE2PA(*page_walk(current->pagetable, addr, 1));
            tmp->MCB_pa = (uint64)pa;
            tmp->MCB_va = (uint64)addr;
            return (uint64)tmp->MCB_va + sizeof(MCB);
        }
    }
    addr = addr_align(cur->MCB_va + cur->MCB_size + sizeof(MCB));
    uint64 pa = allocate_new_page(addr, size + sizeof(MCB));
    MCB * tmp = (MCB *)pa;
    tmp->MCB_next = NULL;
    tmp->MCB_last = cur;
    tmp->MCB_va = addr;
    tmp->MCB_pa = pa;
    tmp->MCB_size = size;
    tmp->PG_INX = (uint64)PTE2PA(*page_walk(current->pagetable, addr, 1));
    return (uint64)addr + sizeof(MCB);
}

//
// reclaim a page, indicated by "va". added @lab2_2
//
/*
uint64 sys_user_free_page(uint64 va) {
  user_vm_unmap((pagetable_t)current->pagetable, va, PGSIZE, 1);
  return 0;
}
 */

uint64 uint64_max(uint64 a, uint64 b) {
    return a>b?a:b;
}

void mcb_free_page(MCB * mcb) {
    for(int i = mcb->MCB_va; i < mcb->MCB_va + sizeof(MCB); i += PGSIZE) {
        uint64 inx = lookup_pa(current->pagetable, i);
        MCB * cur = mcb_head;
        bool page_free = TRUE;
        while((void *)cur != NULL) {
            if(lookup_pa(current->pagetable, cur->MCB_va) == inx) {
                page_free = FALSE;
                break;
            }
            cur = cur->MCB_next;
        }
        if(page_free)
            user_vm_unmap(current->pagetable, ROUNDDOWN(i, PGSIZE), PGSIZE, 1);
    }
}

uint64 sys_user_free_page(uint64 va) {
    MCB * cur = mcb_head;
    MCB * free_mcb = NULL;
    while((void *)cur != NULL) {
        if(va >= cur->MCB_va && va <= cur->MCB_va + cur->MCB_size) {
            free_mcb = cur;
            break;
        }
        cur = cur->MCB_next;
    }
    if((void *)free_mcb == NULL)
        return -1;
    if(free_mcb == mcb_head) {
        mcb_head = mcb_head->MCB_next;
        if((void *)mcb_head != NULL)
            mcb_head->MCB_last = NULL;
    } else {
        free_mcb->MCB_last->MCB_next = free_mcb->MCB_next;
        if ((void *) free_mcb->MCB_next != NULL)
            free_mcb->MCB_next->MCB_last = free_mcb->MCB_last;
    }
//    sprint("--------%lx--------\n", va);
    mcb_free_page(free_mcb);
    return 0;
}

//
// [a0]: the syscall number; [a1] ... [a7]: arguments to the syscalls.
// returns the code of success, (e.g., 0 means success, fail for otherwise)
//
long do_syscall(long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7) {
  switch (a0) {
    case SYS_user_print:
      return sys_user_print((const char*)a1, a2);
    case SYS_user_exit:
      return sys_user_exit(a1);
    // added @lab2_2
    case SYS_user_allocate_page:
      return sys_user_allocate_page(a1);
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
