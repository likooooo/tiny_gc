echo off
if not exist build md build
cd build 
if "debug" equ "%1" (
    set BuildType=Debug
) else (
    set BuildType=Release
)

cmake .. -A x64 -DCMAKE_BUILD_TYPE=%BuildType%

cmake --build . --config %BuildType%

cd ../