@echo off
where g++ >nul 2>nul || set "PATH=C:\MinGW\bin;%PATH%"
g++ -std=c++14 -O2 -Wall -Wextra -static -o cachesim.exe src\main.cpp src\cache.cpp src\trace.cpp || exit /b 1
echo Built cachesim.exe
