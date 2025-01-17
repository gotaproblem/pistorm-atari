@echo off
REM
REM cryptodad
REM Nov 2024
REM 
REM Syntax: makepi4 <CPLD> <update revision>
REM where <update revision> is a flag (0 or 1) to indicate to update build revision
REM

set quartus_bin_path=C:\intelFPGA_lite\20.1\quartus\bin64
set WIFI=192.168.0.99
REM set LAN=192.168.0.98
set piaddress=%WIFI%
set STREAMHOME=dev/ATARI/pistorm-atari

REM 
REM
REM check for command line Argument
if "%~1"=="" goto :SYNTAX

set CPLD=%1
set UPDATE_REV="1"

if "%2"=="" (
set UPDATE_REV="0"
)

if %UPDATE_REV%=="1" (
   if "%CPLD%"=="EPM240" (
      quartus_sh -t update_version.tcl %CPLD%
	  timeout /t 2 /nobreak > NUL
	  set /p NEW_REV=<".\tmp_rev.txt"
	  echo %NEW_REV%
	  REM timeout /t 1 /nobreak > NUL
	  copy ".\pistormSXB_dev%CPLD%.v" ".\pistormSXB_dev%CPLD%_%NEW_REV%.v"
	  del ".\tmp_rev.txt"
   )
)

if "%CPLD%"=="EPM240" (
quartus_map pistormsxb_dev%CPLD%
goto :compile
)
if "%CPLD%"=="EPM570" (
quartus_map pistormsxb_dev%CPLD%
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

if "%CPLD%"=="EPM240" (
echo y | pscp -l steve -pw Mut1ara48;. -P 22 %BITSTREAM%.svf %piaddress%:%STREAMHOME%/rtl/%dstBITSTREAM%
if %errorlevel% neq 0 GOTO ERRORSCP

rem echo y | plink -l steve -pw Mut1ara48;. -P 22 %piaddress% "cd %STREAMHOME%/rtl && ./pistormflash -s %dstBITSTREAM%"
echo y | plink -l steve -pw Mut1ara48;. -P 22 %piaddress% "cd %STREAMHOME% && ./flash.sh"
if %errorlevel% neq 0 GOTO ERRORPROG
)
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
