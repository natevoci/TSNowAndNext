@echo off

setlocal

REM Edit this to point to your installation of ProjectX
set PROJECTX=C:\Apps\ProjectX\ProjectX-v0.90.04.00.b25-20080928\ProjectX.jar
set PROJECTXINIFILE=C:\Apps\ProjectX\X.ini

REM Edit this to specify where the ProjectX output goes
set PROJECTXOUTPATH=F:\Convert\Temp

call :expandPath TSNOWANDNEXT .\TSNowAndNext.exe
popd

if "%~1" == "" goto usage

:nowAndNext
echo ---- Running Now and Next ----
call :start %TSNOWANDNEXT% "%~dpf1"
if not %ERRORLEVEL%==0 exit /B %ERRORLEVEL%

:projectX
echo ---- Running ProjectX ----
call :start java.exe -jar "%PROJECTX%" -gui -ini "%PROJECTXINIFILE%" -out "%PROJECTXOUTPATH%" -cut "%~dpf1.Xcl" "%~dpf1"

exit /B 0

:usage
echo Usage:
echo %~f0 [input.ts]
exit /B 1

:start
call :exec start "%~n1" /BELOWNORMAL /B /WAIT %*
exit /B %ERRORLEVEL%

:exec
pushd %CD%
if "%~x1" == ".bat" (
	echo call %*
	call %*
) else (
	echo %*
	%*
)
popd

set RETURNCODE=%ERRORLEVEL%
if not %RETURNCODE%==0 echo Exited with code %RETURNCODE%
echo _ %RETURNCODE%

exit /B %RETURNCODE%

:expandPath
pushd %~dp0
set %~1=%~dpf2
popd
exit /B 0

