// Stub: the plugin never touches an SD card. Just enough types for the engine headers to parse.
#pragma once
#include <cstdint>
typedef unsigned int UINT;
typedef int FRESULT;
enum { FR_OK = 0 };
struct FATFS {};
struct FIL {};
struct DIR {};
struct FILINFO { char fname[256]; };
inline FRESULT f_opendir(DIR*, const char*) { return 1; }
inline FRESULT f_readdir(DIR*, FILINFO*) { return 1; }
inline FRESULT f_closedir(DIR*) { return 0; }
