@ECHO OFF
REM SETLOCAL && CLS
SETLOCAL
TITLE Signing pbfilter.sys driver...

REM Check if Windows DDK 6.1 is present in PATH
IF NOT EXIST %PB_DDK_DIR% (
ECHO:Windows DDK 6.1 path NOT FOUND!!!&&(GOTO END)
)

IF NOT EXIST %PB_CERT% (
ECHO:Certificate not found!!&&(GOTO END)
)

"%PB_DDK_DIR%\bin\SelfSign\signtool.exe" sign /v /ac %PB_CERT% /s my /n "PeerBlock, LLC" /t http://timestamp.globalsign.com/scripts/timstamp.dll %1

:END
ECHO.
ECHO.
ECHO:Done!
ECHO.
ECHO.
REM ENDLOCAL && PAUSE
ENDLOCAL

