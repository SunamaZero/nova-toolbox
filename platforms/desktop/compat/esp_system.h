// 桌面桩：esp_system.h
#pragma once
#include <cstdio>
#include <cstdlib>
#include "esp_err.h"
inline void esp_restart(void) { fprintf(stderr, "[stub] esp_restart ignored\n"); }
inline const char* esp_get_idf_version(void) { return "desktop"; }
