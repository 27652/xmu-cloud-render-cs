#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# UE4.27 may be checked out below another repository. Use that repository's
# config so the configured UE upstream remote is available to GitDependencies.
GIT_ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
UPSTREAM_URL="$(git -C "$GIT_ROOT" config --get remote.ue.url || true)"
if [[ "$UPSTREAM_URL" =~ ^https://github\.com:([^/]+)/(.+)$ ]]; then
	UPSTREAM_URL="https://github.com/${BASH_REMATCH[1]}/${BASH_REMATCH[2]}"
	git -C "$GIT_ROOT" config --local remote.ue.url "$UPSTREAM_URL"
fi

if [ ! -f Engine/Binaries/DotNET/GitDependencies.exe ]; then
	echo "GitSetup ERROR: This script does not appear to be located \
       in the root UE4 directory and must be run from there."
	exit 1
fi 

if [ "$(uname)" = "Darwin" ]; then
	# Setup the git hooks
	if [ -d "$GIT_ROOT/.git/hooks" ]; then
		echo "Registering git hooks... (this will override existing ones!)"
		rm -f "$GIT_ROOT/.git/hooks/post-checkout"
		rm -f "$GIT_ROOT/.git/hooks/post-merge"
		ln -s "$SCRIPT_DIR/Engine/Build/BatchFiles/Mac/GitDependenciesHook.sh" "$GIT_ROOT/.git/hooks/post-checkout"
		ln -s "$SCRIPT_DIR/Engine/Build/BatchFiles/Mac/GitDependenciesHook.sh" "$GIT_ROOT/.git/hooks/post-merge"
	fi

	# Get the dependencies for the first time
	Engine/Build/BatchFiles/Mac/GitDependencies.sh --prompt "$@"
else
	# Setup the git hooks
	if [ -d "$GIT_ROOT/.git/hooks" ]; then
		echo "Registering git hooks... (this will override existing ones!)"
		printf '%s\n' '#!/bin/sh' "cd \"$SCRIPT_DIR\" && Engine/Build/BatchFiles/Linux/GitDependencies.sh" >"$GIT_ROOT/.git/hooks/post-checkout"
		chmod +x "$GIT_ROOT/.git/hooks/post-checkout"

		printf '%s\n' '#!/bin/sh' "cd \"$SCRIPT_DIR\" && Engine/Build/BatchFiles/Linux/GitDependencies.sh" >"$GIT_ROOT/.git/hooks/post-merge"
		chmod +x "$GIT_ROOT/.git/hooks/post-merge"
	fi

	# Get the dependencies for the first time
	Engine/Build/BatchFiles/Linux/GitDependencies.sh --prompt "$@"

	echo Register the engine installation...
	if [ -f Engine/Binaries/Linux/UnrealVersionSelector-Linux-Shipping ]; then
		pushd Engine/Binaries/Linux > /dev/null
		./UnrealVersionSelector-Linux-Shipping -register > /dev/null &
		popd > /dev/null
	fi

	pushd Engine/Build/BatchFiles/Linux > /dev/null
	./Setup.sh "$@"
	popd > /dev/null
fi
