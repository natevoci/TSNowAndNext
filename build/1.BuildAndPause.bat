@echo off
setlocal
set BUILD=%~dp01.Build.bat
shift
call "%BUILD%" %*
pause
