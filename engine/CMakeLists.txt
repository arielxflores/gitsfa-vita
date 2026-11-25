cmake_minimum_required(VERSION 3.5)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()
project(gitsfa C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_APP_NAME "GITS First Assault")
set(VITA_TITLEID  "GITS00001")
set(VITA_VERSION  "01.00")
set(BUILD "b2" CACHE STRING "")
# perf (b45): GITSFA_OPT = the game code's optimization flags (default -O2; -O3 / -flto compared in b45), GITSFA_PGO = extra flags
# for a profile run or a profile build (-fprofile-generate=... / -fprofile-use=...; see main.c GITSFA_PGO_GEN). vitaGL and the
# other libraries are prebuilt and keep their own flags.
set(GITSFA_OPT "-O2" CACHE STRING "optimization flags for the game code")
set(GITSFA_PGO "" CACHE STRING "profile flags (profile run / profile use)")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${GITSFA_OPT} -mfpu=neon -mcpu=cortex-a9 -ffast-math -Wall -Wno-misleading-indentation ${GITSFA_PGO}")
set(PAD "16" CACHE STRING "")
add_executable(gitsfa src/main.c src/pad.c)
set_source_files_properties(src/pad.c PROPERTIES COMPILE_DEFINITIONS GITSFA_PAD=${PAD})   # b87: only pad.c (a pad change no longer rebuilds everything)
option(RELEASE "publishing build: no per-second log block, no texture canary verification (render, alog.h)" OFF)
if(RELEASE)
  target_compile_definitions(gitsfa PRIVATE GITSFA_RELEASE=1)
endif()
target_link_libraries(gitsfa vitaGL vitashark SceShaccCgExt mathneon taihen_stub SceShaccCg_stub stdc++ m
  SceKernelDmacMgr_stub SceGxm_stub SceDisplay_stub SceCtrl_stub SceAudio_stub ScePower_stub SceAppMgr_stub SceCommonDialog_stub SceSysmodule_stub SceIofilemgr_stub SceLibKernel_stub SceAvPlayer_stub)   # + movies (tut_video.h)
target_link_libraries(gitsfa SceTouch_stub)   # menus: front panel touch (menu_ui.h)
target_link_libraries(gitsfa SceNet_stub SceNetCtl_stub ScePspnetAdhoc_stub SceAppUtil_stub)   # net: sockets, Wi-Fi state, ad-hoc PDP, the user name (net_tr.h, net_lobby.h)
target_link_libraries(gitsfa "-Wl,--wrap=sceIoRead,--wrap=sceIoOpen,--wrap=fopen")   # perf (b45): ldprof.h counts the main thread's file reads per load phase; fopen: 64 KB stdio buffers for loading
vita_create_self(eboot.bin gitsfa UNSAFE)
vita_create_vpk(gitsfa-${BUILD}.vpk ${VITA_TITLEID} eboot.bin VERSION ${VITA_VERSION} NAME ${VITA_APP_NAME}
  FILE ${CMAKE_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
       ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
       ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
       ${CMAKE_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml)
