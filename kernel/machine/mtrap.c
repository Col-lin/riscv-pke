#include "kernel/riscv.h"
#include "kernel/process.h"
#include "spike_interface/spike_utils.h"
#include "kernel/elf.h"
#include <string.h>

static void error_printer();

static void handle_instruction_access_fault() { error_printer(); panic("Instruction access fault!"); }

static void handle_load_access_fault() { error_printer(); panic("Load access fault!"); }

static void handle_store_access_fault() { error_printer(); panic("Store/AMO access fault!"); }

static void handle_illegal_instruction() { error_printer(); panic("Illegal instruction!"); }

static void handle_misaligned_load() { error_printer(); panic("Misaligned Load!"); }

static void handle_misaligned_store() { error_printer(); panic("Misaligned AMO!"); }

// added @lab1_3
static void handle_timer() {
  int cpuid = 0;
  // setup the timer fired at next time (TIMER_INTERVAL from now)
  *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIMECMP(cpuid) + TIMER_INTERVAL;

  // setup a soft interrupt in sip (S-mode Interrupt Pending) to be handled in S-mode
  write_csr(sip, SIP_SSIP);
}

//
// handle_mtrap calls a handling function according to the type of a machine mode interrupt (trap).
//
void handle_mtrap() {
  uint64 mcause = read_csr(mcause);
  switch (mcause) {
    case CAUSE_MTIMER:
      handle_timer();
      break;
    case CAUSE_FETCH_ACCESS:
      handle_instruction_access_fault();
      break;
    case CAUSE_LOAD_ACCESS:
      handle_load_access_fault();
    case CAUSE_STORE_ACCESS:
      handle_store_access_fault();
      break;
    case CAUSE_ILLEGAL_INSTRUCTION:
      // TODO (lab1_2): call handle_illegal_instruction to implement illegal instruction
      // interception, and finish lab1_2.
//      panic( "call handle_illegal_instruction to accomplish illegal instruction interception for lab1_2.\n" );
      handle_illegal_instruction();

      break;
    case CAUSE_MISALIGNED_LOAD:
      handle_misaligned_load();
      break;
    case CAUSE_MISALIGNED_STORE:
      handle_misaligned_store();
      break;

    default:
      sprint("machine trap(): unexpected mscause %p\n", mcause);
      sprint("            mepc=%p mtval=%p\n", read_csr(mepc), read_csr(mtval));
      panic( "unexpected exception happened in M-mode.\n" );
      break;
  }
}

struct stat f_stat;
char error_path[256];
char error_file[10000];

void error_printer() {
    uint64 exception_addr = read_csr(mepc);

    for(int i = 0; i < current->line_ind; ++i) {
        if(exception_addr >= current->line[i].addr) continue;
        addr_line *expecline = current->line + i - 1;
        int dir_length = strlen(current->dir[current->file[expecline->file].dir]);
        strcpy(error_path, current->dir[current->file[expecline->file].dir]);
        error_path[dir_length] = '/';
        strcpy(error_path + dir_length + 1, current->file[expecline->file].file);

        spike_file_t * _FILE_ = spike_file_open(error_path, O_RDONLY, 0);
        spike_file_stat(_FILE_, &f_stat);
        spike_file_read(_FILE_, error_file, f_stat.st_size);
        spike_file_close(_FILE_);
        int off = 0, line_cnt = 0;
        while(off < f_stat.st_size) {
            int temp = off;
            while (temp < f_stat.st_size && error_file[temp] != '\n') ++temp;
            if(line_cnt == expecline->line - 1) {
                error_file[temp] = '\0';
                sprint("Runtime error at %s:%d\n%s\n",
                       error_path, expecline->line, error_file + off);
                break;
            } else {
                ++line_cnt;
                off = temp + 1;
            }
        }
        break;
    }
    return ;
}