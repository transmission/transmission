@echo off
setlocal EnableExtensions EnableDelayedExpansion

set __argc=0
for %%i in (%*) do (
    set /a __argc+=1
    set "__argv[!__argc!]=%%~i"
)

set "result_path=!__argv[1]!"
set "test_action=!__argv[2]!"

set "temp_result_path=%result_path%.tmp"
>"%temp_result_path%" <nul set /p=

if "%test_action%" == "--dump-args" goto dump_args
if "%test_action%" == "--dump-env" goto dump_env
if "%test_action%" == "--dump-cwd" goto dump_cwd

exit /b 1

:dump_args
    for /l %%i in (3,1,%__argc%) do (
        >>"%temp_result_path%" echo.!__argv[%%i]!
    )
    goto finish

:dump_env
    for /l %%i in (3,1,%__argc%) do (
        >>"%temp_result_path%" call :dump_env_var "!__argv[%%i]!"
    )
    goto finish

:dump_env_var
    if defined %~1 (
        echo.!%~1!
    ) else (
        echo.^<null^>
    )
    exit /b 0

:dump_cwd
    >>"%temp_result_path%" echo.%CD%
    goto finish

:finish
    >nul move /y "%temp_result_path%" "%result_path%"
    exit /b 0
