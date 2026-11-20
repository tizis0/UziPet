@echo off
setlocal
cd /d "%~dp0"
if not defined VCINSTALLDIR call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if not exist bin mkdir bin
if not exist obj mkdir obj
rem ассеты и расширение внутрь exe: один файл и программа, и установщик, и обновление
python tools\pack_assets.py || exit /b 1
rc /nologo /fo obj\uzi.res obj\uzi.rc || exit /b 1
rem исходники по папкам: core (окно, графика, мир, обновление), pet (она сама), speech (речь, моменты, разговоры),
rem desk (стол, файлы, уголок), senses (что ты делаешь: игры, приложения), web (браузер). Инклуды без путей: /I на каждую папку
set SRC=src\*.cpp src\core\*.cpp src\pet\*.cpp src\speech\*.cpp src\desk\*.cpp src\senses\*.cpp src\web\*.cpp
set INC=/Isrc /Isrc\core /Isrc\pet /Isrc\speech /Isrc\desk /Isrc\senses /Isrc\web
cl /nologo /std:c++20 /O2 /EHsc /W3 /utf-8 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS %INC% /Fo:obj\ %SRC% obj\uzi.res /Fe:bin\UziPet.exe ^
  /link /SUBSYSTEM:WINDOWS /MANIFEST:NO user32.lib gdi32.lib d2d1.lib dwrite.lib windowscodecs.lib ole32.lib oleaut32.lib ^
  shell32.lib shlwapi.lib dwmapi.lib advapi32.lib uuid.lib secur32.lib ws2_32.lib d3d11.lib
if errorlevel 1 exit /b 1
if not exist bin\assets mkdir bin\assets
copy /y assets\* bin\assets >nul
if not exist bin\assets\fonts mkdir bin\assets\fonts
copy /y assets\fonts\* bin\assets\fonts >nul
rem фразы по стадиям отношений: папка целиком и заново (старые файлы тем не должны оставаться); старый плоский файл не нужен
if exist bin\assets\phrases rmdir /s /q bin\assets\phrases
xcopy /s /i /q /y assets\phrases bin\assets\phrases >nul
if exist bin\assets\phrases.json del bin\assets\phrases.json
if not exist bin\extension mkdir bin\extension
copy /y extension\* bin\extension >nul
echo OK
