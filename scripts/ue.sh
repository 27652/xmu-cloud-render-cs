#!/usr/bin/env bash

set -euo pipefail

# ============================================================
# Configuration
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Defaults match this repository. Override UE_ROOT, PROJECT_DIR, PROJECT_NAME,
# or PS_ROOT when using a project or engine outside this checkout.
UE_ROOT="${UE_ROOT:-${PROJECT_ROOT}/UE4.27}"
PROJECT_NAME="${PROJECT_NAME:-Testdemo}"
PROJECT_DIR="${PROJECT_DIR:-${PROJECT_ROOT}/${PROJECT_NAME}}"

UPROJECT="${UPROJECT:-${PROJECT_DIR}/${PROJECT_NAME}.uproject}"

EDITOR_TARGET="${EDITOR_TARGET:-${PROJECT_NAME}Editor}"
GAME_TARGET="${GAME_TARGET:-${PROJECT_NAME}}"

BUILD_SH="${UE_ROOT}/Engine/Build/BatchFiles/Linux/Build.sh"
SETUP_TOOLCHAIN_SH="${UE_ROOT}/Engine/Build/BatchFiles/Linux/SetupToolchain.sh"
LINUX_TOOLCHAIN_CLANG="${UE_ROOT}/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v19_clang-11.0.1-centos7/x86_64-unknown-linux-gnu/bin/clang++"
RUNUAT_SH="${UE_ROOT}/Engine/Build/BatchFiles/RunUAT.sh"

UNREAL_EDITOR="${UNREAL_EDITOR:-${UE_ROOT}/Engine/Binaries/Linux/UE4Editor}"
UNREAL_EDITOR_CMD="${UNREAL_EDITOR_CMD:-${UE_ROOT}/Engine/Binaries/Linux/UE4Editor-Cmd}"

PACKAGE_DIR="${PROJECT_ROOT}/Packaged"

# Pixel Streaming
PIXEL_STREAMING_URL="${PIXEL_STREAMING_URL:-ws://127.0.0.1:8888}"

PS_ROOT="${PS_ROOT:-${PROJECT_ROOT}/PixelStreamingInfrastructure-UE4.27}"
PS_SIGNAL_DIR="${PS_ROOT}/SignallingWebServer/platform_scripts/bash"

LOG_DIR="${PROJECT_ROOT}/Saved/ScriptLogs"
PID_DIR="${PROJECT_ROOT}/Saved/ScriptPids"

mkdir -p "${LOG_DIR}" "${PID_DIR}"


# ============================================================
# Helpers
# ============================================================

die()
{
    echo "[ERROR] $*" >&2
    exit 1
}

info()
{
    echo
    echo "============================================================"
    echo "$*"
    echo "============================================================"
}

check_file()
{
    [[ -f "$1" ]] || die "File not found: $1"
}

check_exec()
{
    [[ -x "$1" ]] || die "Executable not found: $1"
}

ensure_linux_toolchain()
{
    if [[ -x "${LINUX_TOOLCHAIN_CLANG}" ]]; then
        return 0
    fi

    check_exec "${SETUP_TOOLCHAIN_SH}"
    echo "Linux toolchain is missing; running:"
    echo "  ${SETUP_TOOLCHAIN_SH}"
    "${SETUP_TOOLCHAIN_SH}"

    [[ -x "${LINUX_TOOLCHAIN_CLANG}" ]] || die \
        "Linux toolchain setup completed without clang++: ${LINUX_TOOLCHAIN_CLANG}"
}


# Check
# ============================================================

cmd_check()
{
    info "Checking Unreal Engine / project"

    echo "UE_ROOT      = ${UE_ROOT}"
    echo "PROJECT_ROOT = ${PROJECT_ROOT}"
    echo "UPROJECT     = ${UPROJECT}"
    echo

    check_file "${UPROJECT}"
    check_exec "${BUILD_SH}"

    echo "Build.sh:"
    echo "  ${BUILD_SH}"

    echo
    if [[ -x "${UNREAL_EDITOR}" ]]; then
        echo "UnrealEditor:"
        echo "  ${UNREAL_EDITOR}"
    else
        echo "UnrealEditor: not built yet"
        echo "  ${UNREAL_EDITOR}"
        echo "  Build it with: $0 build"
    fi

    echo
    echo "Pixel Streaming directory:"
    echo "  ${PS_ROOT}"

    echo
    echo "[OK] Environment looks good."
}


# ============================================================
# Generate project files
# ============================================================

cmd_generate()
{
    info "Generating project files"

    if [[ -x "${UE_ROOT}/GenerateProjectFiles.sh" ]]; then

        "${UE_ROOT}/GenerateProjectFiles.sh" \
            -project="${UPROJECT}" \
            -game \
            -engine

    elif [[ -x "${UE_ROOT}/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh" ]]; then

        "${UE_ROOT}/Engine/Build/BatchFiles/Linux/GenerateProjectFiles.sh" \
            -project="${UPROJECT}" \
            -game \
            -engine

    else
        die "GenerateProjectFiles.sh not found."
    fi
}


# ============================================================
# Build Editor
# ============================================================

cmd_build()
{
    info "Building ${EDITOR_TARGET}"

    check_exec "${BUILD_SH}"
    check_file "${UPROJECT}"
    ensure_linux_toolchain

    "${BUILD_SH}" \
        "${EDITOR_TARGET}" \
        Linux \
        Development \
        "${UPROJECT}"

    echo
    echo "[OK] Editor build finished."
}


# ============================================================
# Build standalone Game target
# ============================================================

cmd_build_game()
{
    info "Building ${GAME_TARGET}"

    check_exec "${BUILD_SH}"
    ensure_linux_toolchain

    "${BUILD_SH}" \
        "${GAME_TARGET}" \
        Linux \
        Development \
        "${UPROJECT}"

    echo
    echo "[OK] Game build finished."
}


# ============================================================
# Start Unreal Editor
# ============================================================

cmd_editor()
{
    info "Starting Unreal Editor"

    check_exec "${UNREAL_EDITOR}"

    exec "${UNREAL_EDITOR}" "${UPROJECT}"
}


# ============================================================
# Pixel Streaming infrastructure
# ============================================================

cmd_pixel_setup()
{
    info "Setting up Pixel Streaming infrastructure"

    if [[ -x "${PS_SIGNAL_DIR}/setup.sh" ]]; then
        (
            cd "${PS_SIGNAL_DIR}"
            ./setup.sh
        )
    else
        die "Pixel Streaming setup script not found: ${PS_SIGNAL_DIR}/setup.sh"
    fi

    echo
    echo "[OK] Pixel Streaming infrastructure ready."
}


# ============================================================
# Find signalling server start script
# ============================================================

find_signal_script()
{
    local candidates=(
        "${PS_SIGNAL_DIR}/start_with_stun.sh"
        "${PS_SIGNAL_DIR}/start.sh"
        "${PS_SIGNAL_DIR}/Start_SignallingServer.sh"
    )

    local file

    for file in "${candidates[@]}"; do
        if [[ -f "${file}" ]]; then
            echo "${file}"
            return 0
        fi
    done

    return 1
}


# ============================================================
# Start Pixel Streaming signalling server
# ============================================================

cmd_pixel_server()
{
    info "Starting Pixel Streaming signalling server"

    local start_script

    start_script="$(find_signal_script)" || {
        echo "No signalling start script found in:"
        echo "  ${PS_SIGNAL_DIR}"
        echo
        echo "Run first:"
        echo "  $0 pixel-setup"
        exit 1
    }

    echo "Using:"
    echo "  ${start_script}"
    echo

    cd "${PS_SIGNAL_DIR}"

    NO_SUDO=1 exec  bash "${start_script}"
}


# ============================================================
# Run editor with Pixel Streaming
# ============================================================

cmd_pixel_editor()
{
    info "Starting Unreal Editor with Pixel Streaming"

    check_exec "${UNREAL_EDITOR}"

    echo "Pixel Streaming URL:"
    echo "  ${PIXEL_STREAMING_URL}"

    exec "${UNREAL_EDITOR}" \
        "${UPROJECT}" \
        -RenderOffScreen \
        -EditorPixelStreamingStartOnLaunch=true \
        -EditorPixelStreamingUseRemoteSignallingServer=true \
        -PixelStreamingURL="${PIXEL_STREAMING_URL}"
}


# ============================================================
# Package Linux build
# ============================================================

cmd_package()
{
    info "Packaging Linux build"

    check_exec "${RUNUAT_SH}"

    mkdir -p "${PACKAGE_DIR}"

    "${RUNUAT_SH}" \
        BuildCookRun \
        -project="${UPROJECT}" \
        -platform=Linux \
        -clientconfig=Development \
        -build \
        -cook \
        -stage \
        -package \
        -pak \
        -archive \
        -archivedirectory="${PACKAGE_DIR}" \
        -noP4 \
        -utf8output

    echo
    echo "[OK] Package output:"
    echo "  ${PACKAGE_DIR}"
}


# ============================================================
# Find packaged executable
# ============================================================

find_packaged_app()
{
    local launcher=""

    launcher="$(find "${PACKAGE_DIR}" \
        -type f \
        -name "${PROJECT_NAME}.sh" \
        -print \
        -quit 2>/dev/null || true)"

    if [[ -n "${launcher}" ]]; then
        echo "${launcher}"
        return 0
    fi

    launcher="$(find "${PACKAGE_DIR}" \
        -type f \
        -name "${PROJECT_NAME}" \
        -perm -u+x \
        -print \
        -quit 2>/dev/null || true)"

    if [[ -n "${launcher}" ]]; then
        echo "${launcher}"
        return 0
    fi

    return 1
}


# ============================================================
# Run packaged Pixel Streaming application
# ============================================================

cmd_pixel_run()
{
    info "Starting packaged TestDemo with Pixel Streaming"

    local app

    app="$(find_packaged_app)" || {
        echo "Packaged application not found."
        echo
        echo "Run:"
        echo "  $0 package"
        exit 1
    }

    echo "Application:"
    echo "  ${app}"

    echo
    echo "Pixel Streaming:"
    echo "  ${PIXEL_STREAMING_URL}"
    echo

    exec "${app}" \
        -RenderOffScreen \
        -PixelStreamingURL="${PIXEL_STREAMING_URL}"
}


# ============================================================
# Start signalling server in background
# ============================================================

start_pixel_server_background()
{
    local start_script
    local log_file="${LOG_DIR}/pixel-server.log"
    local pid_file="${PID_DIR}/pixel-server.pid"

    start_script="$(find_signal_script)" || die \
        "Pixel Streaming server not installed. Run '$0 pixel-setup'."

    info "Starting Pixel Streaming server in background"

    (
        cd "${PS_SIGNAL_DIR}"

        if command -v setsid >/dev/null 2>&1; then
            exec setsid bash "${start_script}"
        else
            exec bash "${start_script}"
        fi
    ) >"${log_file}" 2>&1 &

    local pid=$!

    echo "${pid}" > "${pid_file}"

    echo "PID:"
    echo "  ${pid}"

    echo
    echo "Log:"
    echo "  ${log_file}"

    sleep 2
}


# ============================================================
# Stop signalling server
# ============================================================

cmd_pixel_stop()
{
    local pid_file="${PID_DIR}/pixel-server.pid"

    if [[ ! -f "${pid_file}" ]]; then
        echo "No Pixel Streaming PID file found."
        return 0
    fi

    local pid
    pid="$(cat "${pid_file}")"

    if kill -0 "${pid}" 2>/dev/null; then

        # Try killing entire process group first.
        kill -- "-${pid}" 2>/dev/null || kill "${pid}" 2>/dev/null || true

        echo "Stopped Pixel Streaming server: ${pid}"
    else
        echo "Pixel Streaming server already stopped."
    fi

    rm -f "${pid_file}"
}


# ============================================================
# Build + package
# ============================================================

cmd_release()
{
    cmd_build
    cmd_package
}


# ============================================================
# Pixel Streaming all-in-one
# ============================================================

cmd_pixel()
{
    local started_server=0

    start_pixel_server_background
    started_server=1

    cleanup()
    {
        if [[ "${started_server}" == "1" ]]; then
            cmd_pixel_stop || true
        fi
    }

    trap cleanup EXIT INT TERM

    cmd_pixel_run
}


# ============================================================
# Usage
# ============================================================

usage()
{
    cat <<EOF

Usage:

  $0 COMMAND


Commands:

  check
      Check UE and TestDemo paths.

  generate
      Generate project files.

  build
      Build TestDemoEditor.

  build-game
      Build TestDemo standalone Game target.

  editor
      Open TestDemo in Unreal Editor.

  package
      Build + Cook + Package Linux application.

  pixel-setup
      Download/setup Pixel Streaming Infrastructure.

  pixel-server
      Start Pixel Streaming signalling server.

  pixel-editor
      Stream Unreal Editor directly.

  pixel-run
      Run packaged TestDemo with Pixel Streaming.

  pixel
      Start signalling server + packaged TestDemo.

  pixel-stop
      Stop background Pixel Streaming signalling server.

  release
      Build Editor + package Linux build.


Examples:

  $0 check

  $0 build

  $0 editor

  $0 pixel-setup

  $0 pixel-server

  $0 pixel-editor

  $0 package

  $0 pixel-run

  $0 pixel


Environment overrides:

  UE_ROOT=/opt/UnrealEngine $0 build

  PIXEL_STREAMING_URL=ws://192.168.1.10:8888 $0 pixel-run

EOF
}


# ============================================================
# Main
# ============================================================

COMMAND="${1:-help}"

case "${COMMAND}" in

    check)
        cmd_check
        ;;

    generate)
        cmd_generate
        ;;

    build)
        cmd_build
        ;;

    build-game)
        cmd_build_game
        ;;

    editor)
        cmd_editor
        ;;

    package)
        cmd_package
        ;;

    pixel-setup)
        cmd_pixel_setup
        ;;

    pixel-server)
        cmd_pixel_server
        ;;

    pixel-editor)
        cmd_pixel_editor
        ;;

    pixel-run)
        cmd_pixel_run
        ;;

    pixel)
        cmd_pixel
        ;;

    pixel-stop)
        cmd_pixel_stop
        ;;

    release)
        cmd_release
        ;;

    help|--help|-h)
        usage
        ;;

    *)
        echo "Unknown command: ${COMMAND}"
        usage
        exit 1
        ;;

esac
