#!/bin/bash

BIN_DIR="cmake_targets/ran_build/build"
# XRAN_LIB_PATH="phy_v5.1.4/fhi_lib/lib"
XRAN_LIB_PATH="$(readlink -f phy_v5.1.4/fhi_lib/lib)"  # <-- Absolute path here
BUILD_SCRIPT="./cmake_targets/build_oai"
BUILD_TARGET="oran_fhlib_5g"
TMP_LOG="/tmp/aria_build.log"

run_build() {
    MODE=$1
    echo "Running cmake for ARIA in ${MODE} mode..."

    CMD="sudo ${BUILD_SCRIPT} --ninja -t ${BUILD_TARGET} --cmake-opt -Dxran_LOCATION=${XRAN_LIB_PATH} --aria_${MODE}_mode --gNB "
    echo "[INFO] Command: $CMD"

    # Run and stream output to terminal and temp file
    $CMD 2>&1 | tee $TMP_LOG
    STATUS=${PIPESTATUS[0]}

    # Check for CMake cache mismatch
    if grep -q "CMake Error: The current CMakeCache.txt directory" $TMP_LOG; then
        echo "[WARN] Detected CMakeCache mismatch. Cleaning up..."
        echo "[INFO] Retrying build cleanly..."
        echo "[INFO] Command: $CMD -C"

        $CMD -C 2>&1 | tee $TMP_LOG
        STATUS=${PIPESTATUS[0]}
    fi

    return $STATUS
}

build() {
    MODE=$1
    NEW_NAME=$2

    echo "Building in ${MODE} mode..."
    run_build $MODE

    if [ -f "${BIN_DIR}/aria" ]; then
        sudo mv ${BIN_DIR}/aria ${BIN_DIR}/${NEW_NAME}
    else
        echo "Error: aria binary not found. Build might have failed."
        exit 1
    fi
}

build "du" "aria_l2_app"
build "cu" "aria_l3_app"

echo "QRAN ARIA Build process completed successfully!"
