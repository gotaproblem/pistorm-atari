@echo off
REM
REM cryptodad
REM Jan 2023
REM 
REM Argument added for PiStorm latch type
REM L373A and L374
REM BLITTER
REM Syntax: makepi4 <CPLD> <revision>
REM
REM

set quartus_bin_path=C:\intelFPGA_lite\20.1\quartus\bin64
set WIFI=192.168.0.99
REM set LAN=192.168.0.98
REM set CPLD=EPM240
REM set CPLD=EPM570
set piaddress=%WIFI%
REM set STREAMHOME=dev/ATARI/pistorm-4atari
set STREAMHOME=dev/ATARI/tmp/pistorm-atari

REM 
REM
REM check for command line Argument
if "%~1"=="" goto :SYNTAX
set CPLD=%1

if "%CPLD%"=="EPM240" (
%quartus_bin_path%\quartus_map pistormsxb_dev%CPLD%
goto :compile
)
if "%CPLD%"=="EPM570" (
%quartus_bin_path%\quartus_map pistormsxb_dev%CPLD%
goto :compile
)

REM error check
goto :SYNTAX

:compile
set BITSTREAM=%CPLD%_bitstream_dev
set dstBITSTREAM=%CPLD%_bitstream.svf
%quartus_bin_path%\quartus_sh --flow compile pistormsxb_dev%CPLD%
if %errorlevel% neq 0 GOTO ERRORCOMPILE

%quartus_bin_path%\quartus_cpf -c -q 100KHz -g 3.3 -n p output_files/pistormsxb_dev%CPLD%.pof %BITSTREAM%.svf
if %errorlevel% neq 0 GOTO ERRORSVF

echo y | pscp -l steve -pw Mut1ara48;. -P 22 %BITSTREAM%.svf %piaddress%:%STREAMHOME%/rtl/%dstBITSTREAM%
if %errorlevel% neq 0 GOTO ERRORSCP

echo y | plink -l steve -pw Mut1ara48;. -P 22 %piaddress% "cd %STREAMHOME% && ./flash.sh"
if %errorlevel% neq 0 GOTO ERRORPROG

goto done

:ERRORCOMPILE
echo "ERROR COMPILE"
goto done

:ERRORSVF
echo "ERROR SVF"
goto done

:SYNTAX
echo "Missing argument: expected EPM240 or EPM570"
goto done

:ERRORSCP
echo "ERROR SCP"
goto done

:ERRORPROG
echo "ERROR PROGRAM"

:DONE
