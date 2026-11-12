#pragma once

// The fork installs beside the stock VitaPad (VPAD00001): own title ID and data directory.
// APP_TITLE_ID comes from VITA_TITLEID in Server/CMakeLists.txt so the two can't disagree.
#ifndef APP_TITLE_ID
#error "APP_TITLE_ID must be defined by Server/CMakeLists.txt"
#endif
#define APP_DATA_DIR "ux0:data/VitaPadNG"
