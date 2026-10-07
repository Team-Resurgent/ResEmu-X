@echo off
rem Sets up the xemu .xbe and folder launchers. With no arguments a menu asks
rem what to do; any arguments are passed to register-xbe.ps1.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0register-xbe.ps1" %*
if "%~1"=="" pause
