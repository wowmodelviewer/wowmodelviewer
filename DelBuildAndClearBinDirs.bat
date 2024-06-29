rmdir /s /q build

cd bin

set "EXCEPTION_FILE=COPYING"
set "EXCEPTION_DIR=userSettings"

for %%F in (*) do (
    if /I not "%%F"=="%EXCEPTION_FILE%" (
        del "%%F"
    )
)

for /D %%D in (*) do (
    if /I not "%%D"=="%EXCEPTION_DIR%" (
        rd /s /q "%%D"
    )
)
