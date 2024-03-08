解决挑战的第一部分是要从elf文件中读入所有的函数名

首先需要找到 `.shstrtab` 节，以此找到我们所需要的  `.symtab` 和 `.strtab` 节所在地址
```c
uint16 sect_num = ctx->ehdr.shnum;
uint64 shstr_offest = ctx->ehdr.shoff + ctx->ehdr.shstrndx * sizeof(elf_sect_header);
elf_fpread(ctx, (void *)&shstr_sh, sizeof(elf_sect_header),
               shstr_offest);
char tmp_str[shstr_sh.sh_size];
elf_fpread(ctx, &tmp_str, shstr_sh.sh_size, shstr_sh.sh_offset);
```

接着遍历所有的 section ，找到包含函数名和函数名字符串的 `.symtab` 和 `.strtab` 节
```c
for(int i = 0; i < sect_num; ++i) {
    elf_fpread(ctx, (void *)&temp_sh, sizeof(temp_sh),
               (ctx->ehdr).shoff + i * (ctx->ehdr).shentsize);
    uint32 type = temp_sh.sh_type;
    if (type == SHT_SYMTAB) {
        sym_sh = temp_sh;
        find_sym = TRUE;
    } else if(type == SHT_STRTAB) {
        if(strcmp(tmp_str+temp_sh.sh_name,".strtab")==0){
        str_sh = temp_sh;
        find_str = TRUE;
        }
    }
}
```

最后根据 `.symtab` 中的地址，去 `.strtab` 中取出对应字符串即可
```c
int cnt = 0;
uint64 sym_num = sym_sh.sh_size / sizeof(elf_symbol);
for (int i = 0; i < sym_num; i++) {
    elf_symbol tmp_sym;
    elf_fpread(ctx, (void *)&tmp_sym, sizeof(tmp_sym),
               sym_sh.sh_offset + i * sizeof(elf_symbol));
    if (tmp_sym.st_name == 0) continue;
    if (tmp_sym.st_info == 18) {
        char func[32];
        elf_fpread(ctx, &func, sizeof(func),
                   str_sh.sh_offset + tmp_sym.st_name);
        funcs[cnt] = tmp_sym;
        strcpy(funcs_name[cnt], func);
        ++cnt;
    }
}
funcs_count = cnt;
```

第二部分是在用户态的堆栈中取出以此取出函数名并输出

```c
ssize_t sys_user_backtrace(uint64 depth) {
    uint64 sp = current->trapframe->regs.sp + 32;
    uint64 ra = sp + 8;
    while(depth--) {
        if(func_printer(*(uint64 *) ra) == 0) return depth;
        ra += 16;
    }
    return depth;
}
```