#!/usr/bin/env bash
# Copy the MinGW build to the Windows dev PC and run commands there.
#   tools/win-test.sh                 -> run duckling_tests.exe
#   tools/win-test.sh version         -> run "duckling.exe version"
# Needs the "desktop-carlos" SSH alias (docs/dev-setup-windows-ssh.md) and a
# prior `cmake --build --preset windows-mingw-release`.
set -euo pipefail
cd "$(dirname "$0")/.."
B=build/windows-mingw-release
H=${DUCKLING_WIN_HOST:-desktop-carlos}
ssh "$H" 'New-Item -ItemType Directory -Force -Path "$env:USERPROFILE\duckling-test" | Out-Null'
scp -q "$B/duckling.exe" "$B/tests/duckling_tests.exe" "$H:duckling-test/"
if [ $# -eq 0 ]; then
  ssh "$H" 'cd $env:USERPROFILE\duckling-test; .\duckling_tests.exe; exit $LASTEXITCODE'
else
  ssh "$H" "cd \$env:USERPROFILE\\duckling-test; cmd /c 'duckling.exe $* 2>&1'; exit \$LASTEXITCODE"
fi
