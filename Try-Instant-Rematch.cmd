@echo off
setlocal
set "SF4E_INSTANT_REMATCH=1"
pushd "%~dp0"
start "" "%~dp0SF4Enhanced.exe" %*
popd
endlocal
