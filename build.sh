#!/usr/bin/env bash
set -euo pipefail

# -------------------------
# Project metadata
# -------------------------
APP_NAME="Snake"
APP_TITLE="Snake"
APP_AUTHOR="GodTierGamers"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
DIST_DIR="${ROOT_DIR}/dist"
ASSETS_DIR="${ROOT_DIR}/assets"

ICON_TEX="${ASSETS_DIR}/iconTex.tga"
BOOT_TV_TEX="${ASSETS_DIR}/bootTvTex.tga"
BOOT_DRC_TEX="${ASSETS_DIR}/bootDrcTex.tga"

# allow parallel build, default to number of cores
JOBS="${JOBS:-}"
if [[ -z "${JOBS}" ]]; then
  if command -v nproc >/dev/null 2>&1; then JOBS="-j$(nproc)"
  elif command -v sysctl >/dev/null 2>&1; then JOBS="-j$(sysctl -n hw.ncpu)"
  else JOBS="-j"; fi
fi

# -------------------------
# devkitPro paths & tools
# -------------------------
# MSYS maps C:\devkitPro -> /opt/devkitpro
if [[ -d "/opt/devkitpro" ]]; then
  DKP="/opt/devkitpro"
elif [[ -d "/c/devkitPro" ]]; then
  DKP="/c/devkitPro"
elif [[ -n "${DEVKITPRO:-}" && -d "${DEVKITPRO}" ]]; then
  DKP="${DEVKITPRO}"
else
  echo "ERROR: devkitPro not found. Install to C:\\devkitPro or set DEVKITPRO." >&2; exit 1
fi
export DEVKITPRO="${DKP}"

# Wrapper cmake & pkg-config
CMAKE_WRAPPER="${DKP}/portlibs/wiiu/bin/powerpc-eabi-cmake"
[[ -x "${CMAKE_WRAPPER}" ]] || { echo "ERROR: powerpc-eabi-cmake not found (install wiiu-cmake)." >&2; exit 1; }

if [[ -x "${DKP}/portlibs/wiiu/bin/powerpc-eabi-pkg-config" ]]; then
  PKGCONF="${DKP}/portlibs/wiiu/bin/powerpc-eabi-pkg-config"
elif command -v powerpc-eabi-pkg-config >/dev/null 2>&1; then
  PKGCONF="$(command -v powerpc-eabi-pkg-config)"
else
  echo "ERROR: powerpc-eabi-pkg-config not found (install wiiu-pkg-config/ppc-pkg-config)." >&2; exit 1
fi

# Ensure pkg-config sees Wii U .pc files
export PKG_CONFIG_LIBDIR="${DKP}/portlibs/wiiu/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="${DKP}"

# Optional toolchain hint (wrapper sets this anyway)
TOOLCHAIN=""
if [[ -f "${DKP}/wut/share/wut.toolchain.cmake" ]]; then
  TOOLCHAIN="${DKP}/wut/share/wut.toolchain.cmake"
elif [[ -f "${DKP}/wut/share/wut/toolchain.cmake" ]]; then
  TOOLCHAIN="${DKP}/wut/share/wut/toolchain.cmake"
fi

# -------------------------
# Sanity
# -------------------------
echo "==> DEVKITPRO: ${DEVKITPRO}"
[[ -f "${PKG_CONFIG_LIBDIR}/sdl2.pc" ]] || echo "WARNING: sdl2.pc missing (install wiiu-sdl2)."
if [[ -f "${DKP}/wut/lib/libwut.a" || -f "${DKP}/wut/lib/powerpc-eabi/libwut.a" ]]; then
  echo "==> Linking against libwut (monolithic WUT)."
else
  echo "ERROR: libwut.a not found (install 'wut')." >&2; exit 1
fi

# -------------------------
# Configure & build
# -------------------------
echo "==> Configuring"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

CFG_ARGS=(-S "${ROOT_DIR}" -B "${BUILD_DIR}" -DPKG_CONFIG_EXECUTABLE="${PKGCONF}")
[[ -n "${TOOLCHAIN}" ]] && CFG_ARGS+=( -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" )

"${CMAKE_WRAPPER}" "${CFG_ARGS[@]}"

echo "==> Building"
cmake --build "${BUILD_DIR}" ${JOBS}

# Grab the RPX from build/
RPX="$(find "${BUILD_DIR}" -maxdepth 3 -type f -name '*.rpx' | head -n1 || true)"
[[ -n "${RPX}" ]] || { echo "ERROR: No .rpx found. Did the target name change?" >&2; exit 1; }

# -------------------------
# Output to dist/ only
# -------------------------
mkdir -p "${DIST_DIR}"
OUT_RPX="${DIST_DIR}/${APP_NAME}.rpx"
cp -f "${RPX}" "${OUT_RPX}"
echo "==> RPX: ${OUT_RPX}"

# -------------------------
# Optional: pack a .wuhb into dist/
# -------------------------
OUT_WUHB="${DIST_DIR}/${APP_NAME}.wuhb"

pack_with_wuhbtool() {
  local TOOL="$1"
  echo "==> Packaging WUHB with: ${TOOL}"
  # Build args array with only existing assets
  local args=("${OUT_RPX}" "${OUT_WUHB}" --name="${APP_TITLE}" --author="${APP_AUTHOR}")
  [[ -f "${ICON_TEX}"     ]] && args+=( --icon="${ICON_TEX}" )
  [[ -f "${BOOT_TV_TEX}"  ]] && args+=( --tv-image="${BOOT_TV_TEX}" )
  [[ -f "${BOOT_DRC_TEX}" ]] && args+=( --drc-image="${BOOT_DRC_TEX}" )
  "${TOOL}" "${args[@]}"
}

if command -v wuhbtool >/dev/null 2>&1; then
  if pack_with_wuhbtool "$(command -v wuhbtool)"; then
    echo "==> WUHB: ${OUT_WUHB}"
  else
    echo "WUHB pack (wuhbtool) failed; RPX is still available in dist/."
  fi
elif command -v wut-create-wuhb >/dev/null 2>&1; then
  # Fallback if a wrapper exists on your setup (same semantics)
  echo "==> Packaging WUHB with: wut-create-wuhb"
  if wut-create-wuhb --input "${OUT_RPX}" --output "${OUT_WUHB}" \
       --name "${APP_TITLE}" --author "${APP_AUTHOR}" \
       $( [[ -f "${ICON_TEX}"     ]] && printf -- '--icon "%s" '     "${ICON_TEX}" ) \
       $( [[ -f "${BOOT_TV_TEX}"  ]] && printf -- '--tv-image "%s" ' "${BOOT_TV_TEX}" ) \
       $( [[ -f "${BOOT_DRC_TEX}" ]] && printf -- '--drc-image "%s" ' "${BOOT_DRC_TEX}" ); then
    echo "==> WUHB: ${OUT_WUHB}"
  else
    echo "WUHB pack (wut-create-wuhb) failed; RPX is still available in dist/."
  fi
else
  echo "==> No WUHB packer found (wuhbtool/wut-create-wuhb). Skipping WUHB."
fi

echo "✅ Done. Artifacts in: ${DIST_DIR}"
