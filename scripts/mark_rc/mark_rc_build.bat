@echo off
REM ============================================================================
REM mark_rc_build.bat - one-click wrapper around mark_rc_build.ps1
REM ============================================================================
REM Tags (or untags) a local test build as an "RC" build, so the splash
REM screen and the About dialog show e.g. "1.2.1 rc3" instead of just
REM "1.2.1". It hardcodes the suffix into
REM GUI_App::format_display_version() (src\slic3r\GUI\GUI_App.cpp).
REM
REM HOW TO USE:
REM   1. Edit the two variables below (RC_ACTION and RC_NUMBER).
REM   2. Double-click this file, or run it from a terminal.
REM   3. Build as usual, test.
REM
REM IMPORTANT: only run this on a throwaway branch cut for a single test
REM build. Never merge the change it makes back into resin/mainline -- once
REM the build is done, run `git tag <name>` on the commit if you want a
REM permanent record, then discard the branch. See the comment above
REM GUI_App::format_display_version() for the full explanation.
REM
REM PARAMETERS (edit these two lines, nothing else):
REM
REM   RC_ACTION  TAG   - apply/replace the RC suffix using RC_NUMBER below
REM              UNDO  - remove the RC suffix, restore the normal version
REM                      string (RC_NUMBER is ignored)
REM
REM   RC_NUMBER  the RC number to stamp, e.g. 3 -> "1.2.1 rc3".
REM              Only used when RC_ACTION=TAG. Re-running with a different
REM              number retags directly, no need to UNDO first.
REM ============================================================================

set RC_ACTION=TAG
set RC_NUMBER=3

REM ---------------------------------------------------------------------------
REM No need to edit anything below this line.
REM ---------------------------------------------------------------------------

setlocal

if /I "%RC_ACTION%"=="TAG" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0mark_rc_build.ps1" -Rc %RC_NUMBER%
) else if /I "%RC_ACTION%"=="UNDO" (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0mark_rc_build.ps1" -Undo
) else (
    echo Unknown RC_ACTION "%RC_ACTION%" - must be TAG or UNDO.
)

endlocal
echo.
pause
