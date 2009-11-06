@ECHO OFF
SETLOCAL && CLS
TITLE Compiling PeerBlock...

REM Check if Windows DDK 6.1 is present in PATH
IF NOT DEFINED PB_DDK_DIR (
ECHO:Windows DDK 6.1 path NOT FOUND!!!&&(GOTO :END)
)

REM Compile PeerBlock with Microsoft Visual Studio
FOR %%A IN ("Win32" "x64"
) DO (
CALL :SubMSVS "Release" %%A
CALL :SubMSVS "Release (Vista)" %%A
)

ECHO.
ECHO.


REM Sign drivers and programs
IF NOT DEFINED PB_CERT (
ECHO:Certificate not found!!! Driver and program will not be signed.&&(GOTO :BuildInstaller)
)

FOR %%F IN (
"Win32\Release" "Win32\Release (Vista)" "x64\Release" "x64\Release (Vista)"
) DO (
PUSHD %%F
CALL ..\..\tools\sign_driver.cmd pbfilter.sys
CALL ..\..\tools\sign_driver.cmd peerblock.exe
POPD
)


:BuildInstaller
REM Detect if we are running on 64bit WIN and use Wow6432Node, set the path
REM of Inno Setup accordingly and compile installer
IF "%PROGRAMFILES(x86)%zzz"=="zzz" (SET "U_=HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"
) ELSE (
SET "U_=HKLM\SOFTWARE\Wow6432Node\Microsoft\Windows\CurrentVersion\Uninstall"
)

SET "I_=Inno Setup"
SET "A_=%I_% 5"
SET "M_=Inno Setup IS NOT INSTALLED!!!"
FOR /f "delims=" %%a IN (
	'REG QUERY "%U_%\%A_%_is1" /v "%I_%: App Path"2^>Nul^|FIND "REG_"') DO (
	SET "InnoSetupPath=%%a"&Call :SubISPath %%InnoSetupPath:*Z=%%)

PUSHD setup
ECHO:Compiling installer...
ECHO.
IF DEFINED InnoSetupPath ("%InnoSetupPath%\iscc.exe" /Q "setup.iss"&&(
ECHO:Installer compiled successfully!)) ELSE (ECHO:%M_%)
POPD


REM Sign installer
IF NOT DEFINED PB_CERT (
ECHO:Certificate not found!!! Installer will not be signed.&&(GOTO :CreateZips)
)

PUSHD "Distribution"
FOR %%J IN (*.exe) DO CALL ..\tools\sign_driver.cmd %%J
POPD


REM Create all the zip files ready for distribution
:CreateZips
MD "Distribution" >NUL 2>&1
DEL "tools\buildnum_parsed.txt" >NUL 2>&1
ECHO.
"tools\SubWCRev.exe" "." "tools\buildnum.txt" "tools\buildnum_parsed.txt"

FOR /f "delims=" %%K IN (
	'FINDSTR ".*" "tools\buildnum_parsed.txt"') DO (
	SET "buildnum=%%K"&Call :SubRevNumber %%buildnum:*Z=%%)
ECHO.

FOR %%L IN (
"Release" "Release (Vista)"
) DO (
CALL :SubZipFiles Win32 %%L
CALL :SubZipFiles x64 %%L
)

GOTO :AllOK

:AllOK
REN "Distribution\PeerBlock_r*_Release (Vista).zip"^
 "PeerBlock_r*_Release_(Vista).zip" >NUL 2>&1
GOTO :END

:ErrorDetected
ECHO:Compilation FAILED!!!
GOTO :END

:SubMSVS
"%WINDIR%\Microsoft.NET\Framework\v3.5\MSBuild.exe" PeerGuardian2.sln^
 /t:Rebuild /p:Configuration=%1 /p:Platform=%2
IF %ERRORLEVEL% NEQ 0 GOTO :ErrorDetected
GOTO :EOF

:SubZipFiles
MD "temp_zip" >NUL 2>&1
COPY "%1\%~2\peerblock.exe" "temp_zip\" /Y
COPY "%1\%~2\pbfilter.sys" "temp_zip\" /Y
COPY "license.txt" "temp_zip\" /Y
COPY "setup\readme.rtf" "temp_zip\" /Y

PUSHD "temp_zip"
START "" /B /WAIT "..\tools\7za\7za.exe" a -tzip -mx=9^
 "PeerBlock_r%buildnum%__%1_%~2.zip" "peerblock.exe" "pbfilter.sys"^
 "license.txt" "readme.rtf" >NUL

ECHO:PeerBlock_r%buildnum%__%1_^%~2.zip created successfully!
MOVE /Y "PeerBlock_r%buildnum%__%1_%~2.zip" "..\Distribution" >NUL 2>&1
POPD
RD /S /Q "temp_zip" >NUL 2>&1
ECHO.
GOTO :EOF

:SubISPath
SET InnoSetupPath=%*
GOTO :EOF

:SubRevNumber
SET buildnum=%*
GOTO :EOF

:END
ECHO.
ECHO.
ENDLOCAL && PAUSE
EXIT