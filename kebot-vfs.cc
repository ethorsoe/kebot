#include <cstring>
#include <sys/stat.h>
#include <sys/file.h>
#include <unistd.h>
#include <ctime>

#include <sqlite3.h>

#include "kebot.hh"

int db_fd;

#define MAXPATHNAME 512

typedef struct {
	sqlite3_file base;
	int fd;
} KebotFile;

int open_db(const char *path){
	return db_fd = open(path,O_RDWR|O_CREAT|O_CLOEXEC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
}

static int kebotVfsClose(sqlite3_file *pFile){
	KebotFile *p = (KebotFile*)pFile;
	close(p->fd);
	return SQLITE_OK;
}

static int kebotVfsRead(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite_int64 iOfst) {
	KebotFile *p = (KebotFile*)pFile;
	int ret;
	if ((ret=pread(p->fd,zBuf,iAmt,iOfst)) == iAmt)
		return SQLITE_OK;
	if (0 <= ret)
		return SQLITE_IOERR_SHORT_READ;
	return SQLITE_IOERR_READ;
}

static int kebotVfsWrite(sqlite3_file *pFile, const void *zBuf,	int iAmt, sqlite_int64 iOfst) {
	if (pwrite(((KebotFile*)pFile)->fd, zBuf, iAmt, iOfst) == iAmt)
		return SQLITE_OK;
	return SQLITE_IOERR_WRITE;
}

static int kebotVfsTruncate(sqlite3_file *pFile, sqlite_int64 size){
	if( ftruncate(((KebotFile *)pFile)->fd, size) ) return SQLITE_IOERR_TRUNCATE;
	return SQLITE_OK;
}

static int kebotVfsSync(sqlite3_file *pFile, int) {
	KebotFile *p = (KebotFile*)pFile;
	int rc = fsync(p->fd);
	return (rc==0 ? SQLITE_OK : SQLITE_IOERR_FSYNC);
}

static int kebotVfsFileSize(sqlite3_file *pFile, sqlite_int64 *pSize){
	KebotFile *p = (KebotFile*)pFile;
	struct stat sStat;

	int rc = fstat(p->fd, &sStat);
	if( rc!=0 ) return SQLITE_IOERR_FSTAT;
	*pSize = sStat.st_size;
	return SQLITE_OK;
}

static int kebotVfsLock(sqlite3_file*, int){
	return SQLITE_OK;
}
static int kebotVfsUnlock(sqlite3_file*, int){
	return SQLITE_OK;
}
static int kebotVfsCheckReservedLock(sqlite3_file*, int* pResOut){
	*pResOut = 0;
	return SQLITE_OK;
}
static int kebotVfsFileControl(sqlite3_file*, int, void*){
	return SQLITE_OK;
}
static int kebotVfsSectorSize(sqlite3_file*){
	return 0;
}
static int kebotVfsDeviceCharacteristics(sqlite3_file*){
	return 0;
}

static int kebotVfsOpen(sqlite3_vfs*, const char*, sqlite3_file *pFile, int flags, int *pOutFlags) {
	KebotFile *p = (KebotFile*)pFile;
	static const sqlite3_io_methods kebotVfsIO = {
		1,	/* iVersion */
		kebotVfsClose,	/* xClose */
		kebotVfsRead,	/* xRead */
		kebotVfsWrite,	/* xWrite */
		kebotVfsTruncate,	/* xTruncate */
		kebotVfsSync,	/* xSync */
		kebotVfsFileSize,	/* xFileSize */
		kebotVfsLock,	/* xLock */
		kebotVfsUnlock,	/* xUnlock */
		kebotVfsCheckReservedLock,	/* xCheckReservedLock */
		kebotVfsFileControl,	/* xFileControl */
		kebotVfsSectorSize,	/* xSectorSize */
		kebotVfsDeviceCharacteristics,	/* xDeviceCharacteristics */
		NULL,	/* xShmMap */
		NULL,	/* xShmLock */
		NULL,	/* xShmBarrier */
		NULL	/* xShmUnmap */
	};

	if( pOutFlags ){
		*pOutFlags = flags;
	}
	memset(p, 0, sizeof(KebotFile));
	p->fd=db_fd;
	p->base.pMethods = &kebotVfsIO;
	return SQLITE_OK;
}

#ifndef F_OK
# define F_OK 0
#endif
#ifndef R_OK
# define R_OK 4
#endif
#ifndef W_OK
# define W_OK 2
#endif

static int kebotVfsAccess(sqlite3_vfs*, const char*, int, int *pResOut) {
	*pResOut = 0;
	return SQLITE_OK;
}

static int kebotVfsFullPathname(sqlite3_vfs*, const char *zPath, int nPathOut, char *zPathOut) {
	strncpy(zPathOut,zPath,nPathOut);
	return SQLITE_OK;
}

static int kebotVfsCurrentTime(sqlite3_vfs*, double *pTime){
	time_t t = time(0);
	*pTime = t/86400.0 + 2440587.5;
	return SQLITE_OK;
}

static void *kebotVfsDlOpen(sqlite3_vfs*, const char *){
	return 0;
}
static void kebotVfsDlError(sqlite3_vfs*, int nByte, char *zErrMsg){
	sqlite3_snprintf(nByte, zErrMsg, "Loadable extensions are not supported");
	zErrMsg[nByte-1] = '\0';
}
static void (*kebotVfsDlSym(sqlite3_vfs*, void*, const char*))(void){
	return 0;
}
static void kebotVfsDlClose(sqlite3_vfs*, void *){
	 return;
}
int kebotVfsDelete(sqlite3_vfs*, const char*, int) {
	return 0;
}
int kebotVfsRandomness(sqlite3_vfs*, int, char*) {
	return 0;
}
int kebotVfsSleep(sqlite3_vfs*, int) {
	return 0;
}


sqlite3_vfs kebotVfs = {
	1,	/* iVersion */
	sizeof(KebotFile),	/* szOsFile */
	MAXPATHNAME,	/* mxPathname */
	0,	/* pNext */
	"kebot",	/* zName */
	0,	/* pAppData */
	kebotVfsOpen,	/* xOpen */
	kebotVfsDelete,	/* xDelete */
	kebotVfsAccess,	/* xAccess */
	kebotVfsFullPathname,	/* xFullPathname */
	kebotVfsDlOpen,	/* xDlOpen */
	kebotVfsDlError,	/* xDlError */
	kebotVfsDlSym,	/* xDlSym */
	kebotVfsDlClose,	/* xDlClose */
	kebotVfsRandomness,	/* xRandomness */
	kebotVfsSleep,	/* xSleep */
	kebotVfsCurrentTime,	/* xCurrentTime */
	NULL,	/* xGetLastError */
	NULL,	/* xCurrentTimeInt64 */
	NULL,	/* xSetSystemCall */
	NULL,	/* xGetSystemCall */
	NULL,	/* xNextSystemCall */
};

