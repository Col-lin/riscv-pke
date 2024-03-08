和 `lab1_challenge1` 相同，本关应该分为两部分

第一部分和前一关类似，需要修改 `elf.c` 文件，从 elf 文件中读出 `.debug_line` 节，提取相关信息
```c
char debug_info_buf[10000];
elf_status load_debug_line(elf_ctx *ctx) {
    elf_sect_header shstr_sh;
    elf_sect_header debug_sh;
    elf_sect_header temp_sh;

    //find shstrtab
    uint16 sect_num = ctx->ehdr.shnum;
    uint64 shstr_offest = ctx->ehdr.shoff + ctx->ehdr.shstrndx * sizeof(elf_sect_header);
    elf_fpread(ctx, (void *)&shstr_sh, sizeof(elf_sect_header),
               shstr_offest);
    char tmp_str[shstr_sh.size];
    elf_fpread(ctx, &tmp_str, shstr_sh.size, shstr_sh.offset);

    //find debug_line
    bool get_debug_line = FALSE;
    for(int i = 0; i < sect_num; ++i) {
        elf_fpread(ctx, (void *)&temp_sh, sizeof(temp_sh),
                   (ctx->ehdr).shoff + i * (ctx->ehdr).shentsize);
        if(strcmp(tmp_str+temp_sh.name,".debug_line")==0){
            debug_sh = temp_sh;
            get_debug_line = TRUE;
            sprint("get debug_line\n");
        }
    }
    if(get_debug_line == FALSE)
        return EL_ERR;
    elf_fpread(ctx, (void *)&debug_info_buf, debug_sh.size, debug_sh.offset);
    make_addr_line(ctx, debug_info_buf, debug_sh.size);
    return EL_OK;
}
```

第二部分需要修改 `mtrap.c` 文件，在内核陷入 `M` 态时，利用已经从 `elf` 文件中读取到的调试信息来找到对应的文件路径和代码行号
```c
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
```