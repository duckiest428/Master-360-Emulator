@ECHO OFF
REM One-click build for Xenia with the Xbox 360 dashboard.
REM Usage: double-click, or run "build.bat release" for a release build.
SETLOCAL
CD /D "%~dp0"

SET "CONFIG=debug"
IF /I "%~1"=="release" SET "CONFIG=release"

REM Locate the newest Vulkan SDK (needed to compile shaders).
IF NOT DEFINED VULKAN_SDK (
  FOR /D %%D IN ("C:\VulkanSDK\*") DO SET "VULKAN_SDK=%%D"
)
IF NOT DEFINED VULKAN_SDK (
  ECHO Vulkan SDK not found in C:\VulkanSDK. Install it with:
  ECHO   winget install KhronosGroup.VulkanSDK
  GOTO :fail
)
SET "PATH=%VULKAN_SDK%\Bin;%PATH%"

REM First run only: fetch submodules and configure CMake.
IF NOT EXIST "build\build.ninja" IF NOT EXIST "build\CMakeCache.txt" (
  ECHO Running first-time setup...
  python xenia-build.py setup || GOTO :fail
)

ECHO Building %CONFIG%...
python xenia-build.py build --config=%CONFIG% || GOTO :fail

REM DashX360 (by ZivvoZ) is Xenia's home screen when present next to it.
SET "SHELL_OUT=%~dp0build\bin\Windows\%CONFIG%\DashX360"
IF EXIST "dashx360-main\XboxMetroLauncher.csproj" (
  WHERE dotnet >NUL 2>&1
  IF ERRORLEVEL 1 (
    ECHO .NET 8 SDK not found - skipping DashX360. Install it with:
    ECHO   winget install Microsoft.DotNet.SDK.8
  ) ELSE (
    ECHO Building DashX360 dashboard...
    dotnet build "dashx360-main\XboxMetroLauncher.csproj" -c Release -o "%SHELL_OUT%" -v quiet -nologo -clp:ErrorsOnly || GOTO :fail
  )
)

SET "EXE=%~dp0build\bin\Windows\%CONFIG%\xenia_canary.exe"
ECHO.
ECHO Build succeeded: %EXE%
CHOICE /C YN /T 15 /D N /M "Launch Xenia now"
IF ERRORLEVEL 2 GOTO :done
START "" "%EXE%"
GOTO :done

:fail
ECHO.
ECHO Build FAILED. Scroll up for the error.
PAUSE
EXIT /B 1

:done
ENDLOCAL
