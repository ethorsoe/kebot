#pragma once

#include <sqlite3.h>
#include <inttypes.h>

#define KEBOT_TMPFILE 0x1

int open_db(const char *path, uint32_t flags);
int bind_db(const char *path, int fd);

extern sqlite3_vfs kebotVfs;
