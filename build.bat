@echo off
setlocal enabledelayedexpansion

set cleanup=0
set num_of_jobs=2
set dir_name=build
set build_type=RelWithDebInfo
set build_static=0
set build_tests=0
set build_examples=0
set arch=x64

:parse_args
if "%~1"=="" goto end_parse
if "%~1"=="-c" (set cleanup=1 & shift & goto parse_args)
if "%~1"=="-h" (call :show_help & exit /b 0)
if "%~1"=="-d" (set dir_name=%~2 & shift & shift & goto parse_args)
if "%~1"=="-j" (set num_of_jobs=%~2 & shift & shift & goto parse_args)
if "%~1"=="-s" (set build_static=1 & shift & goto parse_args)
if "%~1"=="-t" (set build_type=%~2 & shift & shift & goto parse_args)
if "%~1"=="--tests" (set build_tests=1 & shift & goto parse_args)
if "%~1"=="--examples" (set build_examples=1 & shift & goto parse_args)
if "%~1"=="-x" (set arch=Win32 & shift & goto parse_args)
echo Unknown option: %~1
exit /b 1
:end_parse

:: Read version from VERSION file
set /p version=<"%~dp0VERSION"

:: Auto-detect Visual Studio version via vswhere
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "generator="

if exist "%vswhere%" (
    for /f "usebackq delims=. tokens=1" %%v in (`"%vswhere%" -latest -property installationVersion 2^>nul`) do (
        if "%%v"=="17" set "generator=Visual Studio 17 2022"
        if "%%v"=="16" set "generator=Visual Studio 16 2019"
        if "%%v"=="15" set "generator=Visual Studio 15 2017"
    )
)

if not defined generator (
    echo WARNING: could not detect Visual Studio, defaulting to Visual Studio 17 2022
    set "generator=Visual Studio 17 2022"
)

echo generator: !generator!
echo arch: %arch%
echo build_type: %build_type%
echo version: %version%
set "project_source_dir=%~dp0"
set "current_build_path=%project_source_dir%%dir_name%"

:: Clean
if %cleanup% equ 1 (
    if exist "%current_build_path%\" rd /s /q "%current_build_path%\"
)

:: Create build directory
if not exist "%current_build_path%\" mkdir "%current_build_path%"
cd /d "%current_build_path%"

:: Configure
set cmake_args=.. -G "!generator!" -A %arch% -DBUILD_WITH_VERSION_SUFFIX=ON
if %build_static% equ 1 set cmake_args=!cmake_args! -DBUILD_STATIC=ON
if %build_tests% equ 1 set cmake_args=!cmake_args! -DBUILD_TESTS=ON
if %build_examples% equ 1 set cmake_args=!cmake_args! -DBUILD_EXAMPLES=ON
echo cmake !cmake_args!
cmake !cmake_args! || (
    echo ERROR: CMake configuration failed
    cd /d "%project_source_dir%"
    exit /b 1
)

:: Build
echo cmake --build . --config %build_type% --parallel %num_of_jobs%
cmake --build . --config %build_type% --parallel %num_of_jobs% || (
    echo ERROR: Build failed
    cd /d "%project_source_dir%"
    exit /b 1
)

:: Package
if defined version (
    mkdir "%current_build_path%\lib" 2>nul
    mkdir "%current_build_path%\include" 2>nul
    xcopy /E /I /Y /Q "%project_source_dir%include\connx" "%current_build_path%\include\connx"
    copy /Y "%current_build_path%\%build_type%\*" "%current_build_path%\lib\" >nul

    cd /d "%current_build_path%"
    tar -zcf "connx_win_v%version%_%arch%.tar.gz" include lib
    rd /s /q "%current_build_path%\include"
    rd /s /q "%current_build_path%\lib"
    echo packed: connx_win_v%version%_%arch%.tar.gz
    echo ----- finish ----
)

cd /d "%project_source_dir%"
exit /b 0

:show_help
echo usage: build.bat [options]
echo.
echo Build connx library.
echo.
echo options:
echo -c             clean before building
echo -d ^<dir^>     build directory (default: build)
echo -j ^<n^>       parallel jobs (default: 2)
echo -s             build static library
echo -t ^<type^>    build type: Debug / Release / RelWithDebInfo (default: RelWithDebInfo)
echo -x             build for x86 (default:x64)
echo --tests        build unit tests
echo --examples     build example programs
echo -h             show this help
echo.
echo examples:
echo build.bat
echo build.bat -c -t Release
echo build.bat -s --tests --examples
exit /b 0
