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
    uint64 PG_INX;
    uint64 MCB_va;
    uint64 MCB_pa;
    uint64 MCB_size;
    struct MCB_t* MCB_last;
    struct MCB_t* MCB_next;
}MCB;

MCB* mcb_head = NULL;
uint64 user_free_va = USER_FREE_ADDRESS_START;

uint64 allocate_new_page() {
    char *tmp = alloc_page();
    memset(tmp, 0, (size_t)PGSIZE);
    map_pages(current->pagetable, g_ufree_page, PGSIZE, (uint64)tmp, prot_to_type(PROT_READ | PROT_WRITE,1));
    g_ufree_page += PGSIZE;
//    sprint("%lx\n",tmp+0x94);
//    *(tmp+0xac)= 'c';
    return (uint64)tmp;
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
        uint64 pa = allocate_new_page();
        mcb_head = (MCB *)pa;
        mcb_head->MCB_last = NULL;
        mcb_head->MCB_next = NULL;
        mcb_head->MCB_size = size;
        mcb_head->MCB_va = addr;
        mcb_head->MCB_pa = pa;
        mcb_head->PG_INX = pa;
        user_free_va = USER_FREE_ADDRESS_START + size + sizeof(MCB);
        return (uint64)mcb_head + sizeof(MCB);
    }
    MCB * cur = mcb_head;
    MCB * next = cur->MCB_next;
    while((void *)cur->MCB_next != NULL) {
        next = (MCB *) addr_align(cur->MCB_pa + cur->MCB_size + sizeof(MCB));
        uint64 empty_size = cur->MCB_next->MCB_pa - (uint64)next;
        empty_size = uint64_min(empty_size, (cur->PG_INX + PGSIZE) - (uint64)next);
        if (empty_size < size + sizeof(MCB)) {
            cur = cur->MCB_next;
        } else {
            MCB * tmp = (MCB *)next;
            tmp->MCB_next = cur->MCB_next;
            tmp->MCB_last = cur;
            (tmp->MCB_next)->MCB_last = tmp;
            cur->MCB_next = tmp;
            tmp->MCB_size = size;
            tmp->PG_INX = cur->PG_INX;
            tmp->MCB_va = cur->MCB_va + cur->MCB_size + sizeof(MCB);
            tmp->MCB_pa = cur->MCB_pa + cur->MCB_size + sizeof(MCB);
            return (uint64)tmp + sizeof(MCB);
        }
    }
    addr = addr_align(cur->MCB_pa + cur->MCB_size + sizeof(MCB));
    if((cur->PG_INX + PGSIZE) - addr >= size + sizeof(MCB)) {
        MCB * tmp = (MCB *) addr;
//        sprint("--------%d------\n",size);
//            sprint("--------------%lx-----------------\n",tmp);
        tmp->MCB_size = size;
        tmp->MCB_pa = cur->MCB_pa + cur->MCB_size + sizeof(MCB);
        tmp->MCB_va = cur->MCB_va + cur->MCB_size + sizeof(MCB);
        tmp->PG_INX = cur->PG_INX;
        cur->MCB_next = tmp;
        tmp->MCB_next = NULL;
        tmp->MCB_last = cur;
        return (uint64)tmp;
    } else {
        addr = g_ufree_page;
        uint64 pa = allocate_new_page();
        MCB * tmp = (MCB *)addr;
        tmp->MCB_size = size;
        tmp->MCB_pa = pa;
        tmp->MCB_va = addr;
        tmp->PG_INX = addr;
        tmp->MCB_next =NULL;
        tmp->MCB_last = cur;
        cur->MCB_next = tmp;
        return (uint64)tmp;
    }
    return 0;
}

//
// reclaim a page, indicated by "va". added @lab2_2
//
uint64 sys_user_free_page(uint64 va) {
  user_vm_unmap((pagetable_t)current->pagetable, va, PGSIZE, 1);
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
    case SYS_user_allocate_page: {
        uint64 va;
        va = sys_user_allocate_page(a1);
        sprint("%lx\n", va);
        return va;
    }
//      return sys_user_allocate_page(a1);
    case SYS_user_free_page:
      return sys_user_free_page(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
