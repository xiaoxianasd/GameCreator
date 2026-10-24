@echo off
rem Build script (uses local toolchain at F:\qt-toolchain)
setlocal
set PATH=F:\qt-toolchain\Tools\mingw1310_64\bin;F:\qt-toolchain\Tools\Ninja;F:\qt-toolchain\Tools\CMake_64\bin;%PATH%

cd /d "%~dp0"

if not exist build\CMakeCache.txt (
    cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=F:/qt-toolchain/6.7.3/mingw_64 || exit /b 1
)
cmake --build build -j || exit /b 1

echo.
echo Build OK: build\GameCreator.exe
endlocal
