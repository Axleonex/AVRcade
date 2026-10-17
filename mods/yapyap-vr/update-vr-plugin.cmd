@echo off
rem Rebuilds the AVRcade YAPYAP VR bridge and swaps it into the installed game.
rem Close YAPYAP first. Optional first argument: the YAPYAP game folder.
setlocal
set "HERE=%~dp0"
set "GAMEDIR=%~1"
if "%GAMEDIR%"=="" set "GAMEDIR=W:\SteamLibrary\steamapps\common\YAPYAP"
set "DOTNET=dotnet"
where dotnet >nul 2>nul || set "DOTNET=G:\AIStorage\dotnet\dotnet.exe"
"%DOTNET%" build "%HERE%YapyapVrControls.csproj" -c Release -p:TargetGameDir="%GAMEDIR%"
if errorlevel 1 goto failed
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%HERE%prepare.ps1" -Action UpdatePlugin -GameDir "%GAMEDIR%"
if errorlevel 1 goto failed
echo.
echo YAPYAP VR plugin updated. You can close this window.
pause
exit /b 0
:failed
echo.
echo The update did not finish. Nothing was changed unless a line above says UPDATED.
pause
exit /b 1
