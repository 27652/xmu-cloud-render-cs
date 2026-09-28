#!/bin/bash
# Copyright Epic Games, Inc. All Rights Reserved.

# Fix Mono and engine dependencies if needed
START_DIR=`pwd`
cd "$1"

export HOST_ARCH=x86_64-unknown-linux-gnu

bash FixMonoFiles.sh
bash FixDependencyFiles.sh
IS_MONO_INSTALLED=0
IS_MS_BUILD_AVAILABLE=0
MONO_VERSION_PATH=$(command -v mono) || true
CUR_DIR=`pwd`
UE_MONO_DIR="$CUR_DIR/../../../Binaries/ThirdParty/Mono/Linux"

if [ "$UE_USE_SYSTEM_MONO" == "1" ] && [ ! $MONO_VERSION_PATH == "" ] && [ -f $MONO_VERSION_PATH ]; then
	# If Mono is installed, check if it's 5.0 or higher
	MONO_VERSION_PREFIX="Mono JIT compiler version "
	MONO_VERSION_PREFIX_LEN=${#MONO_VERSION_PREFIX}
	MONO_VERSION=`"${MONO_VERSION_PATH}" --version |grep "$MONO_VERSION_PREFIX"`
	MONO_VERSION=(`echo ${MONO_VERSION:MONO_VERSION_PREFIX_LEN} |tr '.' ' '`)
	if [ ${MONO_VERSION[0]} -ge 5 ]; then
		if [ ${MONO_VERSION[1]} -ge 0 ] || [ ${MONO_VERSION[2]} -ge 2 ]; then
			IS_MONO_INSTALLED=1

			# see if msbuild is installed
			MS_BUILD_PATH=`which msbuild` || true
			if [ -f "$MS_BUILD_PATH" ]; then
				IS_MS_BUILD_AVAILABLE=1
				echo "Running system mono/msbuild, version: ${MONO_VERSION}"
			else
				echo "Running system mono, version: ${MONO_VERSION}"
			fi
		fi
	fi
fi

# Use the bundled runtime only when its executable is present. Some source
# distributions contain its assemblies but not mono-boehm, which would make
# the system runtime load the incompatible bundled mscorlib.dll.
if [ $IS_MONO_INSTALLED -eq 0 ] && [ -x "$UE_MONO_DIR/bin/mono" ] && [ "$UE_USE_SYSTEM_MONO" != "1" ]; then
	echo Setting up Mono
	export PATH=$UE_MONO_DIR/bin:$PATH
	export MONO_PATH=$UE_MONO_DIR/lib/mono/4.5:$MONO_PATH
	export MONO_CFG_DIR=$UE_MONO_DIR/etc
	export LD_LIBRARY_PATH=$UE_MONO_DIR/$HOST_ARCH/lib:$LD_LIBRARY_PATH
elif [ ! "$MONO_VERSION_PATH" == "" ] && [ -f "$MONO_VERSION_PATH" ]; then
	echo "Using system mono: $MONO_VERSION_PATH"
	unset MONO_PATH MONO_CFG_DIR UE_MONO_DIR
	export IS_MONO_INSTALLED=$IS_MONO_INSTALLED
	export IS_MS_BUILD_AVAILABLE=$IS_MS_BUILD_AVAILABLE
else
	echo "Unable to find a usable Mono runtime." >&2
	exit 1
fi

cd "$START_DIR"
