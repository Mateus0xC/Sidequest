# Compila dentro do ambiente do MSVC. Uso: .\build.ps1 [-Test]
param([switch]$Test)
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d `"$PSScriptRoot`" && cmake --preset release >nul && cmake --build build" 
if ($Test -and $LASTEXITCODE -eq 0) { & "$PSScriptRoot\build\sq_tests.exe" }
