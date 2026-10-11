/*
MIT License

Copyright (c) 2026 Shenzhen Open Source Co-Creation Technology Co., Ltd. (AtomGit)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef LITTLSFS_ADAPT_H
#define LITTLSFS_ADAPT_H

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

struct lfs;

/** 已挂载时返回内部 lfs 实例，供 LVGL lv_fs_littlefs_register_drive() 使用；未挂载返回 NULL */
struct lfs *fs_adapt_get_lfs(void);

int fs_adapt_mount(void);
void fs_adapt_unmount(void);

int fs_adapt_open(const char *path, int oflag);
int fs_adapt_close(int fd);
int fs_adapt_read(int fd, char *buf, unsigned int len);
int fs_adapt_write(int fd, const char *buf, unsigned int len);
int fs_adapt_delete(const char *path);
/** 将 littlefs 内文件改名/覆盖；newpath 已存在时由底层替换（比先删再写在「文件被占用」时更可靠） */
int fs_adapt_rename(const char *oldpath, const char *newpath);
int fs_adapt_stat(const char *path, unsigned int *file_size);
int fs_adapt_seek(int fd, int offset, unsigned int whence);
int fs_adapt_tell(int fd);
int fs_adapt_sync(int fd);
int fs_adapt_mkdir(const char *path);

int fs_adapt_dir_open(const char *path, void **dir_handle);

int fs_adapt_dir_read(void *dir_handle, char *name, int name_size, int *type);

int fs_adapt_dir_close(void *dir_handle);

int fs_adapt_rename(const char *oldpath, const char *newpath);

void lfs_test(void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif
