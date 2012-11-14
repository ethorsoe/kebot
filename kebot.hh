#pragma once

#include <sqlite3.h>

int open_db(const char *path);
extern sqlite3_vfs kebotVfs;
