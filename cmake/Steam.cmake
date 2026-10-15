# Steam Workshop downloads via PxSteamDL. The root CMakeLists includes this file
# OPTIONALLY: without it the build has no Steam support and downloads/steam.cpp
# is compiled out (PX_HAS_STEAM stays undefined).

# PxSteamDL builds all of its dependencies from source. Its cacert dependency
# uses a date as VERSION, which CPM rejects as a find_package() name when local
# packages are enabled, so local lookup is switched off for this fetch only.
set(CPM_USE_LOCAL_PACKAGES OFF)
CPMAddPackage("gh:PyXiion/PxSteamDL@0.3.0")
set(CPM_USE_LOCAL_PACKAGES ON)

set(PX_STEAM_LIBS PxSteamDL::pxsteamdl)
