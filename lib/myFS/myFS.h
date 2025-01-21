#ifndef MYFS_H
#define MYFS_H
#include "FS.h"
#include <LittleFS.h>
#include <time.h>
#include "Arduino.h"
// #include "conf.h"

#define FORMAT_LITTLEFS_IF_FAILED true



//std::vector<String> split(const String &s, char delimiter);
String readFile(fs::FS &fs, const char *path, bool debug);
void writeFile(fs::FS &fs, const char *path, const char *message);
#endif // MYFS_H