@echo off
echo [LAUNCHER] Fetching latest market data...
python scripts/data.py

if %ERRORLEVEL% NEQ 0 (
    echo [LAUNCHER] WARNING: data.py failed. Using existing market_data1.csv
)

echo.
echo [LAUNCHER] Starting C++ Risk Engine...
echo ============================================================
build\server.exe
