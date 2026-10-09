# Applies lib-patches/ to the submodules without configuring the project, for
# the step that has to come first: building N64Recomp, which needs its patches
# (lib-patches/N64ModernRuntime/N64Recomp/) before it can recompile this game.
#   cmake -P cmake/apply_lib_patches.cmake      (from the repo root)
# Configuring the project applies the same patches; each applies only once.
set(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
include("${CMAKE_CURRENT_LIST_DIR}/ApplyLibPatches.cmake")
bz_apply_lib_patches(rt64)
bz_apply_lib_patches(N64ModernRuntime)
bz_apply_lib_patches(RecompFrontend)
bz_apply_lib_patches(N64ModernRuntime/N64Recomp)
