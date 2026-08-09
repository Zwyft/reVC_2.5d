# ==================================================================================================
# 📱 reVC – Mobile Edition (Android) – Root CMake Configuration
#
# Description:
# This file defines the root CMake configuration for the reVC – Mobile Edition,
# targeting the Android platform via the Android NDK. It initializes the build system
# for the mobile game, integrates shared cross-platform sources, configures compiler settings,
# and handles dependency resolution.
# ==================================================================================================

cmake_minimum_required(VERSION 3.22.1 FATAL_ERROR)

message(" ")
message("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
message(" *  🌐  reVC – Build Configuration Initialized")
message(" *  🔧  Preparing Android platform modules and environment configuration...")
message("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
message(" ")

# ────────────────────────────────────────────────────────────────────────────────
# 🔖 Project Metadata Setup
# ────────────────────────────────────────────────────────────────────────────────
set(REVC_PROJECT_NAME "reVC")
set(REVC_VERSION 0.0.1)
set(REVC_DESCRIPTION "reverted ${REVC_PROJECT_NAME}")
set(REVC_LIB_NAME revc)

project(${REVC_PROJECT_NAME}
        VERSION ${REVC_VERSION}
        LANGUAGES C CXX
        DESCRIPTION ${REVC_DESCRIPTION}
)

# ────────────────────────────────────────────────────────────────────────────────
# 📂 Define Important Directory Paths
# ────────────────────────────────────────────────────────────────────────────────
get_filename_component(REVC_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
set(REVC_GAME_DIR "${REVC_ROOT_DIR}/../../../")
message(" *  📂 Game source directory set to: ${REVC_GAME_DIR}")

if(ANDROID)
    set(SDL2_INCLUDE_DIR "${REVC_GAME_DIR}/vendor/sdl2/include" CACHE PATH "Bundled SDL2 include directory" FORCE)
    set(SDL2_LIBRARY "${REVC_GAME_DIR}/vendor/sdl2/libs/Android/${ANDROID_ABI}/libSDL2.so" CACHE FILEPATH "Bundled SDL2 Android library" FORCE)
endif()

# ────────────────────────────────────────────────────────────────────────────────
# ➕ Add Game Subdirectory If Available
# ────────────────────────────────────────────────────────────────────────────────
if(IS_DIRECTORY "${REVC_GAME_DIR}" AND EXISTS "${REVC_GAME_DIR}/CMakeLists.txt")
    message(" *  ➕ Including Game module from: ${REVC_GAME_DIR}")
    message(" ")
    message("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
    message(" *  🚀 Initiating Game module build configuration...")
    message("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━")
    message(" ")
    set(EXECUTABLE ${REVC_LIB_NAME} CACHE STRING "Native reVC library target name" FORCE)
    set(REVC_BAKER_BUILD OFF CACHE BOOL "Do not build host-only sprite baker from Android" FORCE)
    add_subdirectory(${REVC_GAME_DIR} ${CMAKE_BINARY_DIR}/game_build)
else()
    message(" *  ⚠️ Game source directory or CMakeLists.txt not found at: ${REVC_GAME_DIR}")
    message(FATAL_ERROR " *  ❗ Please ensure the game module sources are available for the build.")
endif()