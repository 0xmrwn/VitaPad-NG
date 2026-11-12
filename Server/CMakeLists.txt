cmake_minimum_required(VERSION 3.5)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(VitaPad C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaPad NG")
set(VITA_TITLEID "VPNG00001")
set(VITA_VERSION "02.00")

# Kernel module: reads the PS button (in its own directory: vitasdk's macros leak variables between calls)
add_subdirectory(kernel)

# App
add_executable(VitaPad
  source/main.c
  source/remap.c
  source/psbutton.c
  source/usbmode.c
  source/inputrate.c
  source/fastinput.c
  source/zones.c
  source/mirror.c
  source/screen.c
  source/stats.c
  source/uiplan.c
  source/touchzones.c
  source/ui.c
)
target_include_directories(VitaPad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../Common ${CMAKE_CURRENT_SOURCE_DIR}/kernel)
target_compile_definitions(VitaPad PRIVATE "APP_TITLE_ID=\"${VITA_TITLEID}\"")
target_compile_options(VitaPad PRIVATE -Wall -O3)
set_target_properties(VitaPad PROPERTIES LINK_FLAGS "-Wl,-q")
add_dependencies(VitaPad vitapad_kernel_stubs)
target_link_libraries(VitaPad
  # Weak: the app still starts when the kernel module isn't loaded
  ${CMAKE_CURRENT_BINARY_DIR}/kernel/libVitaPadKernel5_stub_weak.a
  vita2d
  SceLibKernel_stub SceSysmodule_stub SceCtrl_stub SceTouch_stub SceMotion_stub
  m SceNet_stub SceNetCtl_stub SceAppUtil_stub ScePgf_stub
  jpeg freetype c ScePower_stub SceCommonDialog_stub png16 z
  # libpng needs pow(), and the linker only looks back, so libm has to come after png16
  m
  SceGxm_stub SceDisplay_stub SceAppMgr_stub
  SceShellSvc_stub SceVshBridge_stub taihen_stub
)

# Unsafe: needed to load the kernel module
vita_create_self(eboot.bin VitaPad UNSAFE)
vita_create_vpk(VitaPad.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE assets/ui/atlas.png ui/atlas.png
  FILE build/sce_sys/icon0.png sce_sys/icon0.png
  FILE build/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE build/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE build/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE ${CMAKE_CURRENT_BINARY_DIR}/kernel/vitapad_kernel.skprx module/vitapad_kernel.skprx
)
# The kernel module must be built before it's packed (target names differ before/after CMake 3.20)
if(TARGET VitaPad.vpk-vpk)
  add_dependencies(VitaPad.vpk-vpk vitapad_kernel.skprx-self)
else()
  add_dependencies(VitaPad.vpk vitapad_kernel.skprx)
endif()
