param(
    [switch]$c,         # clean before building
    [switch]$h,         # show help
    [string]$d,         # build directory
    [int]$j = 2,        # parallel jobs
    [switch]$s,         # build static library
    [string]$t,         # build type
    [switch]$x,         # x86 instead of x64
    [string]$o,         # output directory
    [switch]$tests,     # build unit tests
    [switch]$examples   # build example programs
)

function usage {
    Write-Host "usage: build.ps1 [options]"
    Write-Host ""
    Write-Host "Build connx library."
    Write-Host ""
    Write-Host "options:"
    Write-Host "  -c             clean before building"
    Write-Host "  -d <dir>       build directory (default: build)"
    Write-Host "  -j <n>         parallel jobs (default: 2)"
    Write-Host "  -s             build static library"
    Write-Host "  -t <type>      build type: Debug / Release / RelWithDebInfo"
    Write-Host "  -x             build for x86 (default: x64)"
    Write-Host "  -o <dir>       output directory for tar.gz package (default: output)"
    Write-Host "  -tests         build unit tests"
    Write-Host "  -examples      build example programs"
    Write-Host "  -h             show this help"
    Write-Host ""
    Write-Host "examples:"
    Write-Host "  .\build.ps1"
    Write-Host "  .\build.ps1 -c -t Release"
    Write-Host "  .\build.ps1 -s -tests -examples"
}

if ($h) { usage; exit 0 }

$dir_name = if ($d) { $d } else { "build" }
$build_type = if ($t) { $t } else { "RelWithDebInfo" }
$arch = if ($x) { "Win32" } else { "x64" }

$project_source_dir = $PSScriptRoot
$version = Get-Content "$project_source_dir\VERSION" -ErrorAction SilentlyContinue
$current_build_path = Join-Path $project_source_dir $dir_name

# Auto-detect Visual Studio version via vswhere
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$generator = $null
if (Test-Path $vswhere) {
    $vsVersion = & $vswhere -latest -property installationVersion 2>$null
    if ($vsVersion) {
        $major = $vsVersion.Split('.')[0]
        switch ($major) {
            "17" { $generator = "Visual Studio 17 2022" }
            "16" { $generator = "Visual Studio 16 2019" }
            "15" { $generator = "Visual Studio 15 2017" }
        }
    }
}

if (-not $generator) {
    Write-Host "WARNING: could not detect Visual Studio, defaulting to VS 17 2022"
    $generator = "Visual Studio 17 2022"
}

Write-Host "generator: $generator"
Write-Host "arch: $arch"
Write-Host "build_type: $build_type"
Write-Host "version: $version"

# Clean
if ($c) {
    if (Test-Path $current_build_path) {
        Remove-Item $current_build_path -Recurse -Force
        Write-Host "Cleaned build directory: $current_build_path"
    }
}

# Create build directory
if (-not (Test-Path $current_build_path)) {
    New-Item -ItemType Directory -Path $current_build_path | Out-Null
}

# Configure
Push-Location $current_build_path
try {
    $cmake_args = @(
        "..",
        "-G", $generator,
        "-A", $arch,
        "-DBUILD_WITH_VERSION_SUFFIX=ON"
    )
    
    if ($s) { $cmake_args += "-DBUILD_STATIC=ON" }
    if ($tests) { $cmake_args += "-DBUILD_TESTS=ON" }
    if ($examples) { $cmake_args += "-DBUILD_EXAMPLES=ON" }
    
    Write-Host "cmake $($cmake_args -join ' ')"
    & cmake $cmake_args
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed with exit code $LASTEXITCODE"
    }
    
    # Build
    Write-Host "cmake --build . --config $build_type --parallel $j"
    & cmake --build . --config $build_type --parallel $j
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with exit code $LASTEXITCODE"
    }
    
    # Package
    if ($version) {
        $lib_path = Join-Path $current_build_path "lib"
        $include_path = Join-Path $current_build_path "include"
        
        New-Item -ItemType Directory -Path $lib_path -Force | Out-Null
        New-Item -ItemType Directory -Path $include_path -Force | Out-Null
        
        Copy-Item -Path "$project_source_dir\include\connx" -Destination $include_path -Recurse -Force
        Copy-Item -Path "$current_build_path\$build_type\connx.dll" -Destination $lib_path -Force -ErrorAction SilentlyContinue
        Copy-Item -Path "$current_build_path\$build_type\connx.lib" -Destination $lib_path -Force -ErrorAction SilentlyContinue
        
        Push-Location $current_build_path
        try {
            $package_name = "connx_win_v${version}_${arch}.tar.gz"
            tar -zcf $package_name include lib
            if ($LASTEXITCODE -eq 0) {
                # Determine output directory
                if (-not $o) { $o = "output" }
                $output_dir = if ([System.IO.Path]::IsPathRooted($o)) {
                    $o
                } else {
                    Join-Path $project_source_dir $o
                }
                if (-not (Test-Path $output_dir)) {
                    New-Item -ItemType Directory -Path $output_dir -Force | Out-Null
                }
                Copy-Item $package_name $output_dir -Force
                Write-Host "packed: $(Join-Path $output_dir $package_name)"
            } else {
                Write-Warning "Failed to create package"
            }
        } finally {
            Remove-Item $package_name -Force -ErrorAction SilentlyContinue
            Remove-Item $include_path -Recurse -Force -ErrorAction SilentlyContinue
            Remove-Item $lib_path -Recurse -Force -ErrorAction SilentlyContinue
            Pop-Location
        }
    }
    
    Write-Host "---- finish ----"
} finally {
    Pop-Location
}
