#!/usr/bin/env bash
set -euo pipefail

# Directory containing this script:
#   <repo>/scripts
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

# Repository root:
#   <repo>
ROOT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"

UE_ROOT="$ROOT_DIR/UE4.27"
PROJECT="$ROOT_DIR/Testdemo/Testdemo.uproject"
ARCHIVE="$ROOT_DIR/Testdemo/Packaged_SourceUE_Linux"
RUN_UAT="$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"

echo "============================================================"
echo "Packaging Testdemo for Linux"
echo "============================================================"
echo "Repository : $ROOT_DIR"
echo "UE root    : $UE_ROOT"
echo "Project    : $PROJECT"
echo "Archive    : $ARCHIVE"
echo

# Basic sanity checks
if [[ ! -f "$RUN_UAT" ]]; then
    echo "ERROR: RunUAT.sh not found:"
    echo "  $RUN_UAT"
    exit 1
fi

if [[ ! -f "$PROJECT" ]]; then
    echo "ERROR: Project file not found:"
    echo "  $PROJECT"
    exit 1
fi

"$RUN_UAT" BuildCookRun \
  -project="$PROJECT" \
  -noP4 \
  -platform=Linux \
  -clientconfig=Development \
  -build \
  -cook \
  -stage \
  -pak \
  -archive \
  -archivedirectory="$ARCHIVE" \
  -utf8output
