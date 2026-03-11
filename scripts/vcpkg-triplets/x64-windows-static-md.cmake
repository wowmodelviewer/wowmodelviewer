# Release-only override for CI. Skips debug builds to halve vcpkg build time.
# Used automatically when VCPKG_OVERLAY_TRIPLETS points here (CI only).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_BUILD_TYPE release)
