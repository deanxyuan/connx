#!/bin/bash

# Default values
cleanup=0
dir_name="build"
num_of_jobs=2
build_static=0
build_tests=0
build_examples=0
build_type="RelWithDebInfo"

usage() {
    echo "usage: build.sh [options]"
    echo
    echo "Build connx library."
    echo
    echo "options:"
    echo "  -c              clean before building"
    echo "  -d <dir>        build directory (default: build)"
    echo "  -j <n>          number of parallel jobs (default: 2)"
    echo "  -o <dir>        output directory for tar.gz package (default: output)"
    echo "  -s              build static library"
    echo "  -t <type>       build type: Debug / Release / RelWithDebInfo (default: RelWithDebInfo)"
    echo "  --tests         build unit tests"
    echo "  --examples      build example programs"
    echo "  -h              show this help"
    echo
    echo "examples:"
    echo "  ./build.sh"
    echo "  ./build.sh -c -j 4 -t Release"
    echo "  ./build.sh -s --tests --examples"
    exit 0
}

# Parse command line arguments
while [ $# -gt 0 ]; do
    case "$1" in
        -h) usage;;
        -c) cleanup=1 ;;
        -d) dir_name="$2"; shift ;;
        -j) num_of_jobs="$2"; shift ;;
        -o) output_dir="$2"; shift ;;
        -s) build_static=1 ;;
        -t) build_type="$2"; shift ;;
        --tests) build_tests=1 ;;
        --examples) build_examples=1 ;;
        *) echo "unknown option: $1"; exit 1 ;;
    esac
    shift
done

# Get project directories
project_source_dir=$(cd "$(dirname "$0")" && pwd)
current_build_path="$project_source_dir/$dir_name"
version=$(cat "$project_source_dir/VERSION" 2>/dev/null)

echo "project_source_dir: $project_source_dir"
echo "current_build_path: $current_build_path"
echo "version: $version"
echo "build_type: $build_type"
echo "parallel jobs: $num_of_jobs"

# Clean
if [ $cleanup -eq 1 ] && [ -d "$current_build_path" ]; then
    echo "Cleaning build directory..."
    rm -rf "$current_build_path"
fi

# Create build directory
mkdir -p "$current_build_path"

# Configure
cd "$current_build_path" || exit 1
cmake_args="$project_source_dir -DCMAKE_BUILD_TYPE=$build_type -DBUILD_WITH_VERSION_SUFFIX=ON"

if [ $build_static -eq 1 ]; then
    cmake_args="$cmake_args -DBUILD_STATIC=ON"
fi
if [ $build_tests -eq 1 ]; then
    cmake_args="$cmake_args -DBUILD_TESTS=ON"
fi
if [ $build_examples -eq 1 ]; then
    cmake_args="$cmake_args -DBUILD_EXAMPLES=ON"
fi

echo "cmake $cmake_args"
cmake $cmake_args || {
    echo "CMake configuration failed"
    exit 1
}

# Build
echo "cmake --build . --config $build_type --parallel $num_of_jobs"
cmake --build . --config "$build_type" --parallel "$num_of_jobs" || {
    echo "Build failed"
    exit 1
}

# Package
if [ -n "$version" ]; then
    # Detect platform
    os_name=$(uname -s)
    case "$os_name" in
        Darwin)  platform="darwin" ;;
        Linux)   platform="linux" ;;
        *)       platform=$(echo "$os_name" | tr '[:upper:]' '[:lower:]') ;;
    esac

    # Create package directories
    mkdir -p "$current_build_path/lib"
    mkdir -p "$current_build_path/include"

    # Copy headers
    cp -r "$project_source_dir/include/connx" "$current_build_path/include/"

    # Copy libraries (handle both .so and .dylib)
    if [ "$platform" = "darwin" ]; then
        real_lib=$(find "$current_build_path" -maxdepth 1 -name 'libconnx.*.dylib' ! -type l -print -quit)
        if [ -n "$real_lib" ]; then
            lib_basename=$(basename "$real_lib")
            cp "$real_lib" "$current_build_path/lib/"
            major=$(echo "$version" | cut -d. -f1)
            ln -sf "$lib_basename" "$current_build_path/lib/libconnx.$major.dylib"
            ln -sf "$lib_basename" "$current_build_path/lib/libconnx.dylib"
        fi
    else
        real_lib=$(find "$current_build_path" -maxdepth 1 -name 'libconnx.so.*.*' ! -type l -print -quit)
        if [ -n "$real_lib" ]; then
            so_basename=$(basename "$real_lib")
            cp "$real_lib" "$current_build_path/lib/"
            major=$(echo "$version" | cut -d. -f1)
            ln -sf "$so_basename" "$current_build_path/lib/libconnx.so.$major"
            ln -sf "$so_basename" "$current_build_path/lib/libconnx.so"
        fi
    fi

    # Fallback: static library
    if [ -z "$(ls -A "$current_build_path/lib/" 2>/dev/null)" ]; then
        cp "$current_build_path"/libconnx.a "$current_build_path/lib/" 2>/dev/null
    fi

    # Create archive
    cd "$current_build_path" || exit 1
    arch=$(uname -m)
    case "$arch" in
        aarch64|arm64)
            package_name="connx_${platform}_arm64_v${version}.tar.gz"
            ;;
        x86_64|amd64)
            package_name="connx_${platform}_amd64_v${version}.tar.gz"
            ;;
        *)
            package_name="connx_${platform}_${arch}_v${version}.tar.gz"
            ;;
    esac

    tar -zcf "$package_name" include lib

    # Determine output directory
    output_dir="${output_dir:-$project_source_dir/output}"
    [[ "$output_dir" != /* ]] && output_dir="$project_source_dir/$output_dir"
    mkdir -p "$output_dir"

    cp "$package_name" "$output_dir/"
    echo "packed: $output_dir/$package_name"

    # Cleanup temporary files in build directory
    rm -f "$package_name"
    rm -rf "$current_build_path/include" "$current_build_path/lib"
    echo "---- finish ----"
fi
