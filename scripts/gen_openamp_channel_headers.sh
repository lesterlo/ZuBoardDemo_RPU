#!/usr/bin/env bash
# Generate amd_platform_info.h (OpenAMP channel config) for both R5 cores.
#
# Runs the Yocto-built lopper openamp assist (--openamp_header_only) over the
# per-domain device trees that gen-machineconf produced from the domain YAML.
# The generated header carries the channel macros consumed by platform_info.h
# (SHARED_MEM_PA, SHARED_MEM_SIZE, SHARED_BUF_OFFSET, IPI_IRQ_VECT_ID,
# POLL_BASE_ADDR, IPI_CHN_BITMASK, ...), derived from the same source of truth
# as the Linux device tree instead of hand-maintained constants.
#
# Prerequisites:
#   - gen-machineconf has been run (yocto-build/build/conf/dts/<machine>/ exists)
#   - esw-conf-native has been built (its sysroot provides lopper + python)
#   - lopper carries BOTH local fixes from meta-zynqmp-addon/recipes-kernel/lopper
#     (0001 split-mode dual-R5, 0002 header-only dual-R5 + SHARED_MEM_SIZE).
#     If the sysroot assist still has the 0002 bug, this script shadow-patches
#     a temporary copy so it works before the recipe is rebuilt.
#
# Output: <workspace>/runtime-generated/openamp_gen/psu_cortexr5_{0,1}/amd_platform_info.h
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"   # zudemo workspace root
MACHINE="${MACHINE:-zudemo}"
DTS_DIR="${PROJECT_ROOT}/yocto-build/build/conf/dts/${MACHINE}"
OUT_ROOT="${PROJECT_ROOT}/runtime-generated/openamp_gen"

# Locate the esw-conf-native sysroot (lopper + nativepython3 + dtc).
SYSROOT="${LOPPER_SYSROOT:-}"
if [ -z "${SYSROOT}" ]; then
    for d in "${PROJECT_ROOT}"/yocto-build/build/tmp/work/x86_64-linux/esw-conf-native/*/recipe-sysroot-native; do
        [ -x "${d}/usr/bin/lopper" ] && SYSROOT="${d}" && break
    done
fi
if [ -z "${SYSROOT}" ] || [ ! -x "${SYSROOT}/usr/bin/lopper" ]; then
    echo "ERROR: no lopper found. Build esw-conf-native first, or set LOPPER_SYSROOT." >&2
    exit 1
fi
export PATH="${SYSROOT}/usr/bin:${PATH}"
export LOPPER_DTC_FLAGS="-b 0 -@"

# Shadow-patch the assist if the sysroot copy predates the header-only fix.
ASSIST_FILE="$(find "${SYSROOT}/usr/lib" -name openamp_xlnx.py -path '*/lopper/assists/*' -print -quit)"
if [ -z "${ASSIST_FILE}" ]; then
    echo "ERROR: openamp_xlnx.py not found below ${SYSROOT}/usr/lib" >&2
    exit 1
fi
ASSIST_DIR="$(dirname "${ASSIST_FILE}")"
if grep -q "vdev0buffer' in n.name" "${ASSIST_DIR}/openamp_xlnx.py"; then
    echo "NOTE: sysroot lopper lacks the 0002 header-only fix; using a shadow-patched copy."
    echo "      Rebuild for a permanent fix: bitbake -c cleansstate lopper-native esw-conf-native"
    SHADOW="$(mktemp -d)"
    trap 'rm -rf "${SHADOW}"' EXIT
    cp "${ASSIST_DIR}"/openamp.py "${ASSIST_DIR}"/openamp_xlnx.py "${ASSIST_DIR}"/openamp_xlnx_common.py "${SHADOW}/"
    python3 - "${SHADOW}/openamp_xlnx.py" <<'EOF'
import sys
p = sys.argv[1]
src = open(p).read()
old = """    shbuf_sz = hex([n.propval("reg")[1] for n in memory_region_nodes if 'vdev0buffer' in n.name][0])"""
new = """    shbuf_sz = hex(sum([n.propval("reg")[3] for n in vrings]) + [n.propval("reg")[3] for n in memory_region_nodes if 'buffer' in n.name][0])"""
assert src.count(old) == 1, "assist source drifted; update this script/patch 0002"
open(p, 'w').write(src.replace(old, new))
EOF
    export PYTHONPATH="${SHADOW}${PYTHONPATH:+:${PYTHONPATH}}"
    ASSIST_SEARCH="${SHADOW}"
else
    ASSIST_SEARCH="${ASSIST_DIR}"
fi

STATUS=0
for N in 0 1; do
    ESW_MACHINE="psu_cortexr5_${N}"
    DTS="${DTS_DIR}/${MACHINE}-cortexr5-${N}-freertos.dts"
    OUT_DIR="${OUT_ROOT}/${ESW_MACHINE}"
    HDR="${OUT_DIR}/amd_platform_info.h"
    if [ ! -f "${DTS}" ]; then
        echo "ERROR: ${DTS} not found (run gen-machineconf first)" >&2
        STATUS=1
        continue
    fi
    mkdir -p "${OUT_DIR}"
    rm -f "${HDR}"
    ( cd "${OUT_DIR}" && lopper -f --enhanced --permissive -O "${ASSIST_SEARCH}" \
        "${DTS}" -- openamp --openamp_header_only \
        --openamp_output_filename="${HDR}" \
        --openamp_remote="${ESW_MACHINE}" > lopper.log 2>&1 ) || true
    if [ -s "${HDR}" ]; then
        echo "OK  ${HDR}"
        grep -E '#define (IPI_IRQ_VECT_ID|POLL_BASE_ADDR|IPI_CHN_BITMASK|SHARED_MEM_PA|SHARED_MEM_SIZE|SHARED_BUF_OFFSET)' "${HDR}" | sed 's/^/      /'
    else
        echo "FAIL ${ESW_MACHINE}: header not generated -- see ${OUT_DIR}/lopper.log" >&2
        STATUS=1
    fi
done
exit ${STATUS}
