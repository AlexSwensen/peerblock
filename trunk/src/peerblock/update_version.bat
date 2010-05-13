ECHO OFF
SubWCRev "..\.." "version.h" "version_parsed.h"
IF %ERRORLEVEL% NEQ 0 GOTO :NoSubWCRev
GOTO :EOF

:NoSubWCRev
ECHO:#define PB_VER_MAJOR 1 > version_parsed.h
ECHO:#define PB_VER_MINOR 1 >> version_parsed.h
ECHO:#define PB_VER_BUGFIX 1 >> version_parsed.h
ECHO:#define PB_VER_BUILDNUM 9999 >> version_parsed.h
