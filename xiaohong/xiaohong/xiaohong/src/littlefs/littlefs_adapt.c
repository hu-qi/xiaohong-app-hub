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

#include "lfs.h"
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "fcntl.h"
#include "securec.h"
#include "soc_osal.h"
#include "littlefs_config.h"
#include "littlefs_adapt.h"

#define LFS_NAME_LEN_MAX 64
#define LFS_OPEN_MAX 32
#define LFS_RAM_BASE 0xA00000

typedef struct {
    uint32_t start_addr;
    uint32_t block_count;
} lfs_block_info_t;

static lfs_block_info_t g_lfs_block_info;
static lfs_t g_lfs;
static uint8_t g_lfs_open_file_cnt = 0;
static uint8_t g_lfs_mounted = 0;

#ifdef LFS_THREADSAFE
uint32_t g_lfs_lock;
#endif

static const char lfs_root_path[] = ".";

static const char *littlefs_adapt_normalize_path(const char *path)
{
    if (path == NULL) {
        return NULL;
    }
    if ((path[0] == '/') && (path[1] == '\0')) {
        return lfs_root_path;
    }
    if (path[0] == '/') {
        return path + 1;
    }
    return path;
}

static uint32_t littlefs_adapt_get_addr(const struct lfs_config *c, lfs_block_t block, lfs_off_t off)
{
    return g_lfs_block_info.start_addr + ((uint32_t)block * c->block_size) + (uint32_t)off;
}

static int littlefs_adapt_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer,
    lfs_size_t size)
{
    if (W25QXX_Read((uint8_t *)buffer, littlefs_adapt_get_addr(c, block, off), size) != ERRCODE_SUCC) {
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int littlefs_adapt_write(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer,
    lfs_size_t size)
{
    if (W25QXX_Prog((const uint8_t *)buffer, littlefs_adapt_get_addr(c, block, off), size) != ERRCODE_SUCC) {
        return LFS_ERR_IO;
    }
    return LFS_ERR_OK;
}

static int littlefs_adapt_erase(const struct lfs_config *c, lfs_block_t block)
{
    uint32_t erase_addr = littlefs_adapt_get_addr(c, block, 0);
    uint32_t sector_idx = erase_addr / W25X_SECTOR_SIZE;
    uint32_t sector_count = c->block_size / W25X_SECTOR_SIZE;
    uint32_t i;

    if ((c->block_size % W25X_SECTOR_SIZE) != 0U) {
        return LFS_ERR_INVAL;
    }

    for (i = 0; i < sector_count; i++) {
        if (W25QXX_Erase_Sector(sector_idx + i) != ERRCODE_SUCC) {
            return LFS_ERR_IO;
        }
    }
    return LFS_ERR_OK;
}

static int littlefs_adapt_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}

#ifdef LFS_THREADSAFE
static int littlefs_adapt_lock(const struct lfs_config *c)
{
    (void)c;
    g_lfs_lock = osal_irq_lock();
    return LFS_ERR_OK;
}

static int littlefs_adapt_unlock(const struct lfs_config *c)
{
    (void)c;
    osal_irq_restore(g_lfs_lock);
    return LFS_ERR_OK;
}
#endif

static errcode_t littlefs_adapt_get_block_info(uint32_t *start_addr, uint32_t *block_count)
{
    if ((start_addr == NULL) || (block_count == NULL)) {
        return ERRCODE_FAIL;
    }

    if ((CONFIG_LFS_EXT_FLASH_READ_SIZE == 0U) ||
        (CONFIG_LFS_EXT_FLASH_PROG_SIZE == 0U) ||
        (CONFIG_LFS_EXT_FLASH_CACHE_SIZE == 0U) ||
        (CONFIG_LFS_EXT_FLASH_LOOKAHEAD_SIZE == 0U) ||
        ((CONFIG_LFS_EXT_FLASH_CACHE_SIZE % CONFIG_LFS_EXT_FLASH_READ_SIZE) != 0U) ||
        ((CONFIG_LFS_EXT_FLASH_CACHE_SIZE % CONFIG_LFS_EXT_FLASH_PROG_SIZE) != 0U) ||
        ((CONFIG_LFS_EXT_FLASH_BLOCK_SIZE % CONFIG_LFS_EXT_FLASH_CACHE_SIZE) != 0U) ||
        ((CONFIG_LFS_EXT_FLASH_BLOCK_SIZE % CONFIG_LFS_EXT_FLASH_PROG_SIZE) != 0U) ||
        (CONFIG_LFS_EXT_FLASH_START_ADDR % CONFIG_LFS_EXT_FLASH_BLOCK_SIZE) != 0U ||
        (CONFIG_LFS_EXT_FLASH_SIZE == 0U) ||
        (CONFIG_LFS_EXT_FLASH_SIZE % CONFIG_LFS_EXT_FLASH_BLOCK_SIZE) != 0U ||
        (CONFIG_LFS_EXT_FLASH_BLOCK_SIZE != W25X_SECTOR_SIZE) ||
        ((CONFIG_LFS_EXT_FLASH_START_ADDR + CONFIG_LFS_EXT_FLASH_SIZE) > W25X_P0_SIZE)) {
        return ERRCODE_FAIL;
    }

    *start_addr = CONFIG_LFS_EXT_FLASH_START_ADDR;
    *block_count = CONFIG_LFS_EXT_FLASH_SIZE / CONFIG_LFS_EXT_FLASH_BLOCK_SIZE;
    return ERRCODE_SUCC;
}

int fs_adapt_mount(void)
{
    int ret;
    static struct lfs_config cfg = {
        .read  = littlefs_adapt_read,
        .prog  = littlefs_adapt_write,
        .erase = littlefs_adapt_erase,
        .sync  = littlefs_adapt_sync,
#ifdef LFS_THREADSAFE
        .lock = littlefs_adapt_lock,
        .unlock = littlefs_adapt_unlock,
#endif
        .read_size = CONFIG_LFS_EXT_FLASH_READ_SIZE,
        .prog_size = CONFIG_LFS_EXT_FLASH_PROG_SIZE,
        .block_size = CONFIG_LFS_EXT_FLASH_BLOCK_SIZE,
        .cache_size = CONFIG_LFS_EXT_FLASH_CACHE_SIZE,
        .lookahead_size = CONFIG_LFS_EXT_FLASH_LOOKAHEAD_SIZE,
        .block_cycles = CONFIG_LFS_EXT_FLASH_BLOCK_CYCLES,
        .name_max = LFS_NAME_LEN_MAX,
    };

    if (g_lfs_mounted != 0U) {
        return LFS_ERR_OK;
    }

    if (W25QXX_Init() != ERRCODE_SUCC) {
        lfs_debug_print_error("external flash init failed\r\n");
        return -1;
    }

    if (littlefs_adapt_get_block_info(&g_lfs_block_info.start_addr, &g_lfs_block_info.block_count) != ERRCODE_SUCC) {
        lfs_debug_print_error("invalid external flash layout\r\n");
        return -1;
    }

    cfg.block_count = g_lfs_block_info.block_count;
    (void)memset_s(&g_lfs, sizeof(g_lfs), 0, sizeof(g_lfs));

    ret = lfs_mount(&g_lfs, &cfg);
    if (ret < LFS_ERR_OK) {
        ret = lfs_format(&g_lfs, &cfg);
        if (ret < LFS_ERR_OK) {
            lfs_debug_print_error("lfs_format failed, ret = %d\r\n", ret);
            return ret;
        }

        ret = lfs_mount(&g_lfs, &cfg);
        if (ret < LFS_ERR_OK) {
            lfs_debug_print_error("lfs_mount failed, ret = %d\r\n", ret);
            return ret;
        }
    }

    g_lfs_mounted = 1;
    g_lfs_open_file_cnt = 0;
    return LFS_ERR_OK;
}

void fs_adapt_unmount(void)
{
    if (g_lfs_mounted == 0U) {
        return;
    }
    (void)lfs_unmount(&g_lfs);
    g_lfs_mounted = 0;
    g_lfs_open_file_cnt = 0;
}

lfs_t *fs_adapt_get_lfs(void)
{
    if (g_lfs_mounted == 0U) {
        return NULL;
    }
    return &g_lfs;
}

int fs_adapt_mkdir(const char *path)
{
    int ret;
    const char *name = littlefs_adapt_normalize_path(path);

    if ((name == NULL) || (name[0] == '\0') || ((name[0] == '.') && (name[1] == '\0'))) {
        return LFS_ERR_OK;
    }
    if (g_lfs_mounted == 0U) {
        return -1;
    }

    ret = lfs_mkdir(&g_lfs, name);
    if (ret == LFS_ERR_EXIST) {
        return LFS_ERR_OK;
    }
    if (ret < 0) {
        lfs_debug_print_error("lfs_mkdir failed, ret = %d, dir = %s\r\n", ret, name);
        return -1;
    }
    return ret;
}

static int fs_adapt_path_format(const char *path)
{
    char dir_str[LFS_NAME_LEN_MAX] = {0};
    char *cursor = NULL;
    char *slash = NULL;
    size_t len;
    int ret;

    if (path == NULL) {
        return -1;
    }

    cursor = strrchr(path, '/');
    if ((cursor == NULL) || (strlen(path) == strlen(cursor))) {
        return LFS_ERR_OK;
    }

    len = strlen(path) - strlen(cursor);
    if ((len == 0U) || (len >= sizeof(dir_str))) {
        return -1;
    }

    if (strncpy_s(dir_str, sizeof(dir_str), path, len) != EOK) {
        return -1;
    }

    cursor = dir_str;
    if (cursor[0] == '/') {
        cursor++;
    }

    while (cursor[0] != '\0') {
        slash = strchr(cursor, '/');
        if (slash != NULL) {
            *slash = '\0';
        }

        if (dir_str[0] != '\0') {
            ret = fs_adapt_mkdir(dir_str);
            if (ret < 0) {
                return ret;
            }
        }

        if (slash == NULL) {
            break;
        }

        *slash = '/';
        cursor = slash + 1;
    }

    return LFS_ERR_OK;
}

static int fs_adapt_flag_format(int oflag)
{
    uint32_t input_flag = (uint32_t)oflag;
    uint32_t ret_flag = 0;

    if (input_flag == O_RDONLY) {
        return LFS_O_RDONLY;
    }
    if ((input_flag & O_WRONLY) != 0U) {
        ret_flag |= LFS_O_RDWR;
    }
    if ((input_flag & O_RDWR) != 0U) {
        ret_flag |= LFS_O_RDWR;
    }
    if ((input_flag & O_CREAT) != 0U) {
        ret_flag |= LFS_O_CREAT;
    }
    if ((input_flag & O_EXCL) != 0U) {
        ret_flag |= LFS_O_EXCL;
    }
    if ((input_flag & O_TRUNC) != 0U) {
        ret_flag |= LFS_O_TRUNC;
    }
    if ((input_flag & O_APPEND) != 0U) {
        ret_flag |= LFS_O_APPEND;
    }
    return (int)ret_flag;
}

int fs_adapt_open(const char *path, int oflag)
{
    int ret;
    const char *name = littlefs_adapt_normalize_path(path);
    int real_flag = fs_adapt_flag_format(oflag);
    lfs_file_t *fp = NULL;

    if ((name == NULL) || (strlen(name) >= LFS_NAME_LEN_MAX) || (g_lfs_open_file_cnt >= LFS_OPEN_MAX)) {
        return -1;
    }
    if (g_lfs_mounted == 0U) {
        return -1;
    }

    fp = (lfs_file_t *)malloc(sizeof(lfs_file_t));
    if (fp == NULL) {
        lfs_debug_print_error("fp malloc failed\r\n");
        return -1;
    }

    ret = fs_adapt_path_format(path);
    if (ret != LFS_ERR_OK) {
        free(fp);
        return -1;
    }

    ret = lfs_file_open(&g_lfs, fp, name, real_flag);
    if (ret < LFS_ERR_OK) {
        lfs_debug_print_error("lfs_file_open failed, ret = %d, name = %s\r\n", ret, name);
        free(fp);
        return -1;
    }

    g_lfs_open_file_cnt++;
    return (int)(intptr_t)fp;
}

int fs_adapt_close(int fd)
{
    int ret;

    if (fd < LFS_RAM_BASE) {
        return -1;
    }
    if (g_lfs_mounted == 0U) {
        return -1;
    }

    ret = lfs_file_close(&g_lfs, (lfs_file_t *)(intptr_t)fd);
    if (ret < 0) {
        lfs_debug_print_error("lfs_file_close failed, ret = %d\r\n", ret);
        free((lfs_file_t *)(intptr_t)fd);
        return -1;
    }

    if (g_lfs_open_file_cnt > 0U) {
        g_lfs_open_file_cnt--;
    }
    free((lfs_file_t *)(intptr_t)fd);
    return ret;
}

int fs_adapt_read(int fd, char *buf, unsigned int len)
{
    int ret;

    if ((len == 0U) || (fd < LFS_RAM_BASE)) {
        return -1;
    }
    if (g_lfs_mounted == 0U) {
        return -1;
    }

    ret = lfs_file_read(&g_lfs, (lfs_file_t *)(intptr_t)fd, buf, len);
    if (ret < 0) {
        lfs_debug_print_error("lfs_file_read failed, ret = %d\r\n", ret);
        return -1;
    }
    return ret;
}

int fs_adapt_write(int fd, const char *buf, unsigned int len)
{
    int ret;

    if ((len == 0U) || (fd < LFS_RAM_BASE)) {
        return -1;
    }
    if (g_lfs_mounted == 0U) {
        return -1;
    }

    ret = lfs_file_write(&g_lfs, (lfs_file_t *)(intptr_t)fd, buf, len);
    if (ret < 0) {
        lfs_debug_print_error("lfs_file_write failed, ret = %d\r\n", ret);
        return -1;
    }
    return ret;
}

int fs_adapt_delete(const char *path)
{
    int ret;
    const char *name = littlefs_adapt_normalize_path(path);

    if (name == NULL) {
        return -1;
    }
    if (g_lfs_mounted == 0U) {
        return -1;
    }

    ret = lfs_remove(&g_lfs, name);
    if (ret < 0) {
        lfs_debug_print_error("lfs_remove failed, ret = %d\r\n", ret);
        return -1;
    }
    return ret;
}

int fs_adapt_rename(const char *oldpath, const char *newpath)
{
    const char *oldn = littlefs_adapt_normalize_path(oldpath);
    const char *newn = littlefs_adapt_normalize_path(newpath);

    if ((oldn == NULL) || (newn == NULL)) {
        return -1;
    }
    if (g_lfs_mounted == 0U) {
        return -1;
    }

    int ret = lfs_rename(&g_lfs, oldn, newn);
    if (ret < 0) {
        lfs_debug_print_error("lfs_rename failed, ret = %d\r\n", ret);
        return -1;
    }
    return 0;
}

int fs_adapt_stat(const char *path, unsigned int *file_size)
{
    struct lfs_info info = {0};
    int ret;
    const char *name = littlefs_adapt_normalize_path(path);

    if ((name == NULL) || (file_size == NULL)) {
        return -1;
    }
    if (g_lfs_mounted == 0U) {
        return -1;
    }

    ret = lfs_stat(&g_lfs, name, &info);
    if (ret < 0) {
        lfs_debug_print_error("lfs_stat failed, ret = %d\r\n", ret);
        return -1;
    }
    *file_size = info.size;
    return 0;
}

int fs_adapt_seek(int fd, int offset, unsigned int whence)
{
    int file_size;
    int file_pos;
    int next_pos;
    int ret;

    if (fd < LFS_RAM_BASE) {
        return -1;
    }
    if (g_lfs_mounted == 0U) {
        return -1;
    }

    file_size = lfs_file_size(&g_lfs, (lfs_file_t *)(intptr_t)fd);
    file_pos = lfs_file_tell(&g_lfs, (lfs_file_t *)(intptr_t)fd);
    if (whence == LFS_SEEK_SET) {
        next_pos = offset;
    } else if (whence == LFS_SEEK_CUR) {
        next_pos = file_pos + offset;
    } else if (whence == LFS_SEEK_END) {
        next_pos = file_size + offset;
    } else {
        return -1;
    }
    if ((next_pos < 0) || (next_pos > file_size)) {
        return -1;
    }

    ret = lfs_file_seek(&g_lfs, (lfs_file_t *)(intptr_t)fd, offset, (int)whence);
    if (ret < 0) {
        lfs_debug_print_error("lfs_file_seek failed, ret = %d\r\n", ret);
        return -1;
    }
    return ret;
}

int fs_adapt_tell(int fd)
{
    lfs_soff_t pos;

    if ((fd < LFS_RAM_BASE) || (g_lfs_mounted == 0U)) {
        return -1;
    }

    pos = lfs_file_tell(&g_lfs, (lfs_file_t *)(intptr_t)fd);
    if (pos < 0) {
        lfs_debug_print_error("lfs_file_tell failed, ret = %d\r\n", (int)pos);
        return -1;
    }
    return (int)pos;
}

int fs_adapt_sync(int fd)
{
    if ((fd < LFS_RAM_BASE) || (g_lfs_mounted == 0U)) {
        return -1;
    }

    int ret = lfs_file_sync(&g_lfs, (lfs_file_t *)(intptr_t)fd);

    if (ret < 0) {
        lfs_debug_print_error("fs_adapt_sync failed, ret = %d\r\n", ret);
        return -1;
    }
    return ret;
}

void lfs_test(void)
{
    const char *test_file = "/lfs_test.txt";
    const char *test_str = "LittleFS RISC-V Test Data:0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    char read_buf[128] = {0};
    int boot_count = 0;
    int ret;

    printf("\r\n--- Starting LittleFS Enhanced Test ---\r\n");

    // 1. 打开/创建文件
    int fp = fs_adapt_open(test_file, O_RDWR | O_CREAT);
    if (fp < 0) {
        printf("Error: Failed to open file (%d). Check Flash Driver!\r\n", fp);
        return;
    }

    // 2. 读取旧的启动计数 (测试跨次断电持久化)
    ret = fs_adapt_read(fp, (char *)&boot_count, sizeof(boot_count));
    if (ret < (int)sizeof(boot_count)) {
        printf("Info: No existing count, initializing...\r\n");
        boot_count = 0;
    } else {
        printf("Success: Previous Boot Count = %d\r\n", boot_count);
    }

    // 3. 测试 Seek 和 多字节字符串读写 (验证 Flash 缓冲区对齐)
    boot_count++;
    fs_adapt_seek(fp, 0, LFS_SEEK_SET); // 回到文件头
    
    // 写入计数 + 字符串
    fs_adapt_write(fp, (const char *)&boot_count, sizeof(boot_count));
    fs_adapt_write(fp, test_str, strlen(test_str));
    
    // 4. 立即读取校验 (测试 Cache/Lookahead 机制)
    fs_adapt_seek(fp, sizeof(boot_count), LFS_SEEK_SET); // 跳过计数读字符串
    ret = fs_adapt_read(fp, read_buf, strlen(test_str));
    
    printf("LFS Read Data: %s\r\n", read_buf);
    if (ret > 0 && strcmp(read_buf, test_str) == 0) {
        printf("Success: Data integrity check PASSED.\r\n");
    } else {
        printf("Error: Data mismatch!\r\n");
    }

    // 5. 模拟目录操作 (测试索引节点/元数据保存)
    // 注意：如果你的适配层没封装 mkdir，可以跳过此步
    /*
    ret = fs_adapt_mkdir("/test_dir");
    if (ret == 0 || ret == LFS_ERR_EXIST) {
        lfs_debug_print_info("Success: Directory operation OK.\r\n");
    }
    */

    // 6. 关闭文件并同步
    ret = fs_adapt_close(fp);
    if (ret == 0) {
        printf("--- Test Completed. Please Reset MCU to verify persistence ---\r\n\r\n");
    } else {
        printf("Error: Close failed (%d)\r\n", ret);
    }
}

#ifdef CONFIG_LFS_SUPPORT_POSIX
int open(const char *path, int oflags, ...)
{
    return fs_adapt_open(path, oflags);
}

int close(int fd)
{
    return fs_adapt_close(fd);
}

ssize_t read(int fd, void *buf, size_t nbytes)
{
    return fs_adapt_read(fd, buf, nbytes);
}

ssize_t write(int fd, const void *buf, size_t nbytes)
{
    return fs_adapt_write(fd, buf, nbytes);
}

off_t lseek(int fd, off_t offset, int whence)
{
    return fs_adapt_seek(fd, offset, whence);
}

int unlink(const char *pathname)
{
    return fs_adapt_delete(pathname);
}

int fsync(int fd)
{
    return fs_adapt_sync(fd);
}
#endif

int fs_adapt_dir_open(const char *path, void **dir_handle)
{
    if (path == NULL || dir_handle == NULL) {
        return -1;
    }

    lfs_dir_t *dir = (lfs_dir_t *)malloc(sizeof(lfs_dir_t));
    if (dir == NULL) {
        lfs_debug_print_error("dir malloc failed\r\n");
        return -1;
    }

    const char *name = path;
    if (name[0] == '/' && name[1] == 0) {
        name = lfs_root_path;
    } else if (name[0] == '/') {
        name++;
    }

    int ret = lfs_dir_open(&g_lfs, dir, name);
    if (ret < LFS_ERR_OK) {
        lfs_debug_print_error("lfs_dir_open failed, ret = %d, path = %s\r\n", ret, path);
        free(dir);
        return -1;
    }

    *dir_handle = dir;
    return 0;
}

int fs_adapt_dir_read(void *dir_handle, char *name, int name_size, int *type)
{
    if (dir_handle == NULL || name == NULL || type == NULL) {
        return -1;
    }

    struct lfs_info info;
    int ret = lfs_dir_read(&g_lfs, (lfs_dir_t *)dir_handle, &info);
    if (ret <= 0) {
        return ret;  // 0 = end of directory, <0 = error
    }

    // Copy name
    int len = strlen(info.name);
    if (len >= name_size) {
        len = name_size - 1;
    }
    strncpy_s(name, name_size, info.name, len);
    name[len] = '\0';

    // Set type: 1 = file, 2 = directory
    *type = (info.type == LFS_TYPE_DIR) ? 2 : 1;

    return 1;  // Success
}

int fs_adapt_dir_close(void *dir_handle)
{
    if (dir_handle == NULL) {
        return -1;
    }

    int ret = lfs_dir_close(&g_lfs, (lfs_dir_t *)dir_handle);
    free(dir_handle);

    if (ret < LFS_ERR_OK) {
        lfs_debug_print_error("lfs_dir_close failed, ret = %d\r\n", ret);
        return -1;
    }

    return 0;
}
/*
int fs_adapt_rename(const char *oldpath, const char *newpath)
{
    if (oldpath == NULL || newpath == NULL) {
        return -1;
    }

    int ret = lfs_rename(&g_lfs, oldpath, newpath);
    if (ret < LFS_ERR_OK) {
        lfs_debug_print_error("lfs_rename failed, ret = %d\r\n", ret);
        return -1;
    }

    return 0;
}
*/