<#
.SYNOPSIS
  Tags (or untags) a local test build as an "RC" build, so the splash screen
  and About dialog show e.g. "1.2.1 rc3" instead of just "1.2.1".

.DESCRIPTION
  This hardcodes an "rc<N>" suffix into GUI_App::format_display_version()
  (src/slic3r/GUI/GUI_App.cpp), which is the single source of truth for the
  version text on both the splash screen and the About dialog.

  IMPORTANT: run this ONLY on a throwaway branch cut just for a single test
  build. Never merge the change it makes back into the resin/mainline
  branch -- once the build is done, `git tag` the commit if you want a
  permanent record, then discard the branch. This keeps RC tagging out of
  the branch history that actually gets merged, so it can never cause a
  merge conflict.

.PARAMETER Rc
  RC number to stamp, e.g. 3 -> "1.2.1 rc3". Mutually exclusive with -Undo.
  Running it again with a different number retags (no need to -Undo first).

.PARAMETER Undo
  Removes the RC suffix and restores the original line.

.EXAMPLE
  ./scripts/mark_rc/mark_rc_build.ps1 -Rc 3
  # ... build, test ...
  git tag rc3
  ./scripts/mark_rc/mark_rc_build.ps1 -Undo   # optional, only if you plan to keep building on this branch

.EXAMPLE
  ./scripts/mark_rc/mark_rc_build.ps1 -Undo
#>
param(
    [int]$Rc,
    [switch]$Undo
)

$ErrorActionPreference = 'Stop'

# This script lives at PhrozenOrca/scripts/mark_rc/, so the repo root is two
# levels up.
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$target = Join-Path $repoRoot 'src/slic3r/GUI/GUI_App.cpp'

if (-not (Test-Path $target)) {
    throw "Could not find $target. Run this script from PhrozenOrca/scripts/mark_rc/, or the file moved -- see the comment above GUI_App::format_display_version()."
}

if (-not $Rc -and -not $Undo) {
    throw "Specify -Rc <number> to tag a build, or -Undo to revert."
}
if ($Rc -and $Undo) {
    throw "-Rc and -Undo are mutually exclusive."
}

$text = [System.IO.File]::ReadAllText($target)

$plainLine   = 'version_display = Phrozen_VERSION;'
$taggedRegex = 'version_display = std::string\(Phrozen_VERSION\) \+ " rc(\d+)";'
$notFoundMsg = "Could not find the expected line in GUI_App::format_display_version(). " +
               "It may have changed upstream -- edit it by hand instead (see the comment above that function in GUI_App.cpp)."

if ($Undo) {
    if ($text -match $taggedRegex) {
        $text = $text -replace $taggedRegex, $plainLine
        [System.IO.File]::WriteAllText($target, $text, (New-Object System.Text.UTF8Encoding($false)))
        Write-Host "Reverted: format_display_version() no longer tags an RC build."
    } elseif ($text -match [regex]::Escape($plainLine)) {
        Write-Host "Already plain -- nothing to undo."
    } else {
        throw $notFoundMsg
    }
    return
}

$replacement = "version_display = std::string(Phrozen_VERSION) + `" rc$Rc`";"

if ($text -match $taggedRegex) {
    $previous = $Matches[1]
    $text = $text -replace $taggedRegex, $replacement
    Write-Host "Retagged: was rc$previous, now rc$Rc."
} elseif ($text -match [regex]::Escape($plainLine)) {
    $text = $text -replace [regex]::Escape($plainLine), $replacement
    Write-Host "Tagged as rc$Rc."
} else {
    throw $notFoundMsg
}

[System.IO.File]::WriteAllText($target, $text, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Reminder: never merge this change back into resin/mainline. git tag the commit for a permanent record, then discard this branch."
