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
#include "elf.h"

#include "spike_interface/spike_utils.h"

//
// implement the SYS_user_print syscall
//
ssize_t sys_user_print(const char* buf, size_t n) {
  sprint(buf);
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

extern int funcs_count;
extern char funcs_name[64][32];
extern elf_symbol funcs[64];

int func_printer(uint64 addr) {
    for (int i = 0; i < funcs_count; ++i) {
        if (addr >= funcs[i].st_value && addr < funcs[i].st_value + funcs[i].st_size) {
            sprint("%s\n", funcs_name[i]);
            if(strcmp("main", funcs_name[i]) == 0) return 0;
            return 1;
        }
    }
    return 0;
}

ssize_t sys_user_backtrace(uint64 depth) {
    uint64 sp = current->trapframe->regs.sp + 32;
    uint64 ra = sp + 8;
    while(depth--) {
        if(func_printer(*(uint64 *) ra) == 0) return depth;
        ra += 16;
    }
    return depth;
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
    case SYS_user_backtrace:
      return sys_user_backtrace(a1);
    default:
      panic("Unknown syscall %ld \n", a0);
  }
}
