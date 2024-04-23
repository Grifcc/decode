#!/bin/bash

# 检查参数是否为空
if [ -z "$1" ]; then
    echo "Missing build type argument. default to Release."
fi

# 创建 build 目录
mkdir -p build
cd build

# 运行 CMake 构建，传入构建类型参数
cmake .. -DCMAKE_BUILD_TYPE="$1" -DCMAKE_TOOLCHAIN_FILE=../toolchains/orin_native.toolchain.cmake -DCMAKE_INSTALL_PREFIX=../install

# 使用多线程进行编译和安装
make install -j$(nproc)

# 检查 make 命令的执行结果
if [ $? != 0 ]; then
    echo "Error occurs during compilation."
    exit 1
fi
