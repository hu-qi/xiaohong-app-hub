#!/bin/bash

# IMPORTANT NOTE: 这里的路径修改为你本地的 esp-idf/export.sh 的路径
source ~/.espressif/v5.5.5/esp-idf/export.sh
if [[ -z "${IDF_PATH}" ]]; then
    echo -e "\033[31m[ERROR] IDF_PATH is empty; esp-idf export.sh failed or path is wrong\033[0m" >&2
    exit 1
fi
echo "esp-idf path: $IDF_PATH"

SCRIPT_PATH=$(realpath "$0")
SCRIPT_DIRP=$(dirname "${SCRIPT_PATH}")
OHOS_ROOT="${SCRIPT_PATH}"

echo "SCRIPT_PATH=$SCRIPT_PATH"
echo "SCRIPT_DIRP=$SCRIPT_DIRP"

while [[ ! -f "${OHOS_ROOT}/.gn" ]]; do
    OHOS_ROOT=$(dirname "${OHOS_ROOT}")
    if [[ "${OHOS_ROOT}" == "/" ]]; then
        echo -e "\033[31m[OHOS ERROR] Cannot find ohos source tree root with .gn file\033[0m"
        exit 1
    fi
done
echo "OHOS_ROOT=${OHOS_ROOT}"
export OHOS_ROOT

# 改为[[ 1 == 1]] 则在每次编译p4项目时，都会生成新的libopenharmony_liteos_m.a并链接到固件中
if [[ 0 == 1 ]]; then
REBUILT_LIB="${SCRIPT_DIRP}/components/openharmony_liteos/lib/libkernel_liteos_m_rebuilt.a"
TARGET_LIB="${SCRIPT_DIRP}/components/openharmony_liteos/lib/libopenharmony_liteos_m.a"
rm -f "${REBUILT_LIB}"
rm -f "${TARGET_LIB}"

mkdir -p ./build

# 收集 LiteOS-M 内核 .o；排除与 ESP-IDF 冲突的 POSIX/VFS/signal/newlib porting/backtrace 对象：
#   kal/posix/*            posix.time/pipe 等与 newlib(clock_gettime/usleep/sleep)、shims(OsPipeInit) 重复定义
#   components/fs/vfs/*    vfs_fs 与 esp-idf/vfs(opendir/readdir/mkdir/ioctl/pread...) 重复定义
#   components/backtrace/* los_backtrace 引用 __except_stack_top/__start_and_irq_stack_top 等汇编符号，链接缺定义
#   components/signal/*、kal/libc/*   旧库未包含，避免与 newlib/网络栈冲突
find "${OHOS_ROOT}/out/xiaohong-p4/xiaohong-p4/obj/kernel/liteos_m" -name "*.o" \
    | grep -v -E "/kal/posix/|/components/fs/vfs/|/components/backtrace/|/components/signal/|/kal/libc/" \
    | sort > ./build/liteos_m_objects.txt
find "${OHOS_ROOT}/out/xiaohong-p4/xiaohong-p4/obj/third_party/bounds_checking_function" -name "*.o" | sort >> ./build/liteos_m_objects.txt
wc -l ./build/liteos_m_objects.txt

AR=$(command -v riscv32-esp-elf-ar || command -v llvm-ar || command -v ar)

while read f; do
  "$AR" rcs "${REBUILT_LIB}" "$f"
done < ./build/liteos_m_objects.txt

# 用新打包的 libkernel_liteos_m_rebuilt.a 重新生成固件链接的 libopenharmony_liteos_m.a
# 必须放在 idf.py build 之前执行，确保固件链接到最新生成的静态库
if ! bash "${SCRIPT_DIRP}/tools/repack_openharmony_liteos_m.sh" "${REBUILT_LIB}"; then
    echo -e "\033[31m[ERROR] repack libopenharmony_liteos_m.a failed\033[0m" >&2
    exit 1
fi
fi


# ./build_esp32p4.sh -b  直接编译
# ./build_esp32p4.sh -c  删除上一次的编译产物
while getopts "bc" opt; do
    case $opt in
        c)
            rm -rf ./build
            ;;
        b)
            idf.py build
            ;;
        *)
            echo "Usage: $0 [-b] [-c]" >&2
            exit 1
            ;;
    esac
done


mkdir -p out

for f in \
    build/bootloader/bootloader.bin \
    build/partition_table/partition-table.bin \
    build/flash_args \
    build/ohos_liteos_p4_app.bin; do
    if [ -f "${f}" ]; then
        cp -f "${f}" out/
    else
        echo "[WARN] skip copy (not found): ${f}"
    fi
done
