#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

UE_ROOT="${UE_ROOT:-${PROJECT_ROOT}/UE4.27}"
PROJECT_NAME="${PROJECT_NAME:-Testdemo}"
PROJECT_DIR="${PROJECT_DIR:-${PROJECT_ROOT}/${PROJECT_NAME}}"
UPROJECT="${UPROJECT:-${PROJECT_DIR}/${PROJECT_NAME}.uproject}"

SETUP_SH="${UE_ROOT}/Setup.sh"
GENERATE_PROJECT_FILES_SH="${UE_ROOT}/GenerateProjectFiles.sh"
BUILD_SH="${UE_ROOT}/Engine/Build/BatchFiles/Linux/Build.sh"

die()
{
    echo "[ERROR] $*" >&2
    exit 1
}

check_file()
{
    [[ -f "$1" ]] || die "File not found: $1"
}

check_exec()
{
    [[ -x "$1" ]] || die "Executable not found: $1"
}

cmd_env()
{
    cat <<EOF
UE_ROOT=${UE_ROOT}
PROJECT_ROOT=${PROJECT_ROOT}
PROJECT_DIR=${PROJECT_DIR}
UPROJECT=${UPROJECT}
SETUP_SH=${SETUP_SH}
GENERATE_PROJECT_FILES_SH=${GENERATE_PROJECT_FILES_SH}
BUILD_SH=${BUILD_SH}
EOF
}

cmd_setup()
{
    check_exec "${SETUP_SH}"
    (
        cd "${UE_ROOT}"
        ./Setup.sh "$@"
    )
}

cmd_generate()
{
    check_file "${UPROJECT}"

    if [[ -x "${GENERATE_PROJECT_FILES_SH}" ]]; then
        "${GENERATE_PROJECT_FILES_SH}" \
            -project="${UPROJECT}" \
            -game \
            -engine
    elif [[ -x "${UE_ROOT}/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh" ]]; then
        "${UE_ROOT}/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh" \
            -project="${UPROJECT}" \
            -game \
            -engine
    else
        die "GenerateProjectFiles.sh not found under ${UE_ROOT}"
    fi
}

cmd_check()
{
    check_file "${UPROJECT}"
    check_exec "${SETUP_SH}"
    check_exec "${GENERATE_PROJECT_FILES_SH}"
    check_exec "${BUILD_SH}"
    echo "[OK] Unreal Engine environment is ready."
    cmd_env
}

usage()
{
    cat <<EOF
Usage: $0 COMMAND [OPTIONS]

Commands:
  env                         Print configured paths
  check                       Check UE and project paths
  setup [OPTIONS]             Run UE4.27/Setup.sh
  generate                    Run GenerateProjectFiles.sh
  generate-project-files      Alias for generate

Examples:
  $0 check
  $0 setup
  $0 setup --dry-run --threads=1
  $0 generate
EOF
}

COMMAND="${1:-help}"
shift || true

case "${COMMAND}" in
    env)
        cmd_env "$@"
        ;;
    check)
        cmd_check "$@"
        ;;
    setup)
        cmd_setup "$@"
        ;;
    generate|generate-project-files)
        cmd_generate "$@"
        ;;
    help|--help|-h)
        usage
        ;;
    *)
        echo "Unknown command: ${COMMAND}" >&2
        usage >&2
        exit 1
        ;;
esac
