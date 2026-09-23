@echo off
setlocal
REM Builds the client zip and the server zip. Run from anywhere.
REM   SF4E_VCVARS      path to vcvars32.bat if it is not in the default place
REM   SF4E_CERT_SHA1   thumbprint of a code-signing certificate in the user store (no password needed)
REM   SF4E_CERT_PASS   password for deploy\codesign.pfx, used only when SF4E_CERT_SHA1 is unset
if not defined SF4E_VCVARS set "SF4E_VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat"
if not exist "%SF4E_VCVARS%" ( echo vcvars32.bat not found at "%SF4E_VCVARS%"; set SF4E_VCVARS & exit /b 1 )
call "%SF4E_VCVARS%" >nul
cd /d "%~dp0.."
set BAKED=
if exist "%~dp0server.private" ( for /f "usebackq delims=" %%L in ("%~dp0server.private") do ( if not defined BAKED set BAKED=%%L ) )
cmake --preset default -DSF4E_BAKED_SERVER=%BAKED%
if errorlevel 1 ( echo CONFIGURE_FAILED & exit /b 1 )
cmake --build msvc-build\default
if errorlevel 1 ( echo BUILD_FAILED & exit /b 1 )
cd msvc-build\default

REM Code signing (optional). A certificate in the Windows certificate store is
REM preferred: no password on the command line.
if defined SF4E_CERT_SHA1 (
  echo Signing with certificate %SF4E_CERT_SHA1% from the store ...
  signtool sign /fd SHA256 /sha1 %SF4E_CERT_SHA1% /tr http://timestamp.digicert.com /td SHA256 SF4Enhanced.exe Sidecar.dll
  if errorlevel 1 ( echo SIGN_FAILED & exit /b 1 )
) else if exist "%~dp0codesign.pfx" (
  echo Signing with deploy\codesign.pfx ...
  signtool sign /fd SHA256 /f "%~dp0codesign.pfx" /p "%SF4E_CERT_PASS%" /tr http://timestamp.digicert.com /td SHA256 SF4Enhanced.exe Sidecar.dll
  if errorlevel 1 ( echo SIGN_FAILED & exit /b 1 )
) else (
  echo Code signing: no certificate configured, skipping. Players will see the SmartScreen warning.
)

cpack -G ZIP
if errorlevel 1 ( echo CPACK_FAILED & exit /b 1 )

REM Server package: the lobby server, its DLLs, the run scripts and the guide.
set SRV=sf4e-server
if exist %SRV% rmdir /s /q %SRV%
mkdir %SRV%
copy /y LobbyServer.exe %SRV%\ >nul
copy /y *.dll %SRV%\ >nul
for %%F in (run-server.cmd update-server.cmd test-server.cmd) do copy /y "%~dp0%%F" %SRV%\ >nul
copy /y "%~dp0..\SERVER.md" %SRV%\ >nul
if exist sf4e-server.zip del sf4e-server.zip
powershell -NoProfile -Command "Compress-Archive -Path '%SRV%' -DestinationPath 'sf4e-server.zip' -Force"
echo PACK_ALL_EXIT=%ERRORLEVEL%
