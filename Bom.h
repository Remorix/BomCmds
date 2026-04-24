#ifndef BOM_H_
#define BOM_H_

#include <CoreFoundation/CoreFoundation.h>
#include <mach/machine.h>
#include <stdint.h>

typedef struct BOMBom *BOMBom;
typedef struct BOMFSObject *BOMFSObject; /* 168 */
typedef struct BOMCopier *BOMCopier;
typedef uint8_t BOMFSOType;
enum {
    BOMFSOTypeBad = 0,
    BOMFSOTypeFile = 1,
    BOMFSOTypeDirectory = 2,
    BOMFSOTypeSymlink = 3,
    BOMFSOTypeDevice = 4,
    BOMFSOTypeSocket = 5,
    BOMFSOTypeBad2 = 6,
};

typedef struct BOMBomEnumerator *BOMBomEnumerator;

typedef struct BOMStorage *BOMStorage; /* 1128 */
typedef uint32_t BOMBlockID;
typedef uint16_t BOMFSOFlags;

typedef struct BOMTree *BOMTree;                 /* 360 */
typedef struct BOMTreeIterator *BOMTreeIterator; /* 72 */

typedef struct BomSys *BomSys;
typedef uint64_t BOMBomFilterOptions;

typedef enum {
    BOMStorageDumpMemory,
    BOMStorageDumpPath,
    BOMStorageDumpPathID,
    BOMStorageDumpPathKey
} BOMStorageDumpOption;

typedef enum {
    BOMCopierObjectTypeFile = 1,
    BOMCopierObjectTypeDirectory = 2,
    BOMCopierObjectTypeSymlink = 3,
    BOMCopierObjectTypeDevice = 4,
} BOMCopierObjectType;

typedef enum {
    BOMCopierDispositionCopied = 0,
    BOMCopierDispositionSkipped = 1,
    BOMCopierDispositionHardLinked = 2,
} BOMCopierDisposition;

enum {
    BOMBomFilterByArch = 0x1,
    BOMBomFilterByLanguage = 0x2,
    BOMBomFilterByArchAndLanguage = BOMBomFilterByArch | BOMBomFilterByLanguage,
};

typedef int (*BOMCopierPKZipPasswordRequester)(BOMCopier copier, const char *path, char **password_out);
typedef int (*BOMCopierCopyFileStartedHandler)(BOMCopier copier, const char *path, BOMCopierObjectType object_type);
typedef int (*BOMCopierCopyFileFinishedHandler)(
    BOMCopier copier,
    const char *path,
    BOMCopierObjectType object_type,
    long long bytes_copied,
    BOMCopierDisposition disposition);
typedef int (*BOMCopierCopyFileUpdateHandler)(BOMCopier copier, const char *path, long long bytes_copied);
typedef void *(*BOMCopierFatalErrorHandler)(BOMCopier copier, const char *message);
typedef void *(*BOMCopierFatalFileErrorHandler)(BOMCopier copier, const char *path, int errnum);
typedef int (*BOMCopierFileErrorHandler)(BOMCopier copier, const char *path, int errnum);

__BEGIN_DECLS
size_t BOMMemoryDump(const void *data, size_t size, const char *prefix);
extern const BOMFSOType B_BadType;
extern const BOMFSOType B_FileType;
extern const BOMFSOType B_DirectoryType;
extern const BOMFSOType B_SymlinkType;
extern const BOMFSOType B_DeviceType;
extern const BOMFSOType B_SocketType;
extern const BOMFSOType B_BadType2;
extern const BOMFSOFlags B_STAT;
extern const BOMFSOFlags B_CKSUMS;
extern const BOMFSOFlags B_ARCH;
extern const BOMFSOFlags B_FINDER;
extern const BOMFSOFlags B_OPAQUEDATA;
extern const BOMFSOFlags B_PATHONLY;
extern const BOMFSOFlags B_ALLDATA;
extern const BOMFSOFlags B_INFOMASK;
extern const BOMFSOFlags B_ARCHMASK;
extern const uint16_t B_ARCHOFFT;

/* BOMBom */
BOMBom BOMBomOpen(const char *path, BomSys sys);
BOMBom BOMBomOpenWithSys(const char *path, Boolean forWriting, BomSys sys);
BOMBom BOMBomNewWithSys(const char *newBom, BomSys sys);
BOMBom BOMBomNewFromPath(const char *newBom, const char *existingBom);
BOMBom BOMBomNewFromBomWithOptions(
    const char *newBomPath,
    BOMBom sourceBom,
    BOMBomFilterOptions options,
    const char *const *architectures,
    const char *const *languages);
BOMBom BOMBomNewFromDirectoryWithSys(const char *bomPath, const char *directoryPath, BOMFSOFlags flags, BomSys sys);
BOMFSObject BOMBomGetFSObjectAtPath(BOMBom bom, const char *path);
BOMFSObject BOMBomGetRootFSObject(BOMBom bom);
BOMTree BOMBomPathsTree(BOMBom bom);
int BOMBomInsertFSObject(BOMBom bom, BOMFSObject object, int insertMode);
int BOMBomRemoveFSObject(BOMBom bom, BOMFSObject object);
int BOMBomFree(BOMBom bom);

/* BOMCopier */
BOMCopier BOMCopierNew(void);
void BOMCopierFree(BOMCopier copier);
int BOMCopierCopyWithOptions(BOMCopier copier, const char *src, const char *dst, CFDictionaryRef options);
void BOMCopierSetCopyFileFinishedHandler(BOMCopier copier, BOMCopierCopyFileFinishedHandler handler);
void BOMCopierSetCopyFileStartedHandler(BOMCopier copier, BOMCopierCopyFileStartedHandler handler);
void BOMCopierSetCopyFileUpdateHandler(BOMCopier copier, BOMCopierCopyFileUpdateHandler handler);
void BOMCopierSetFatalErrorHandler(BOMCopier copier, BOMCopierFatalErrorHandler handler);
void BOMCopierSetFatalFileErrorHandler(BOMCopier copier, BOMCopierFatalFileErrorHandler handler);
void BOMCopierSetFileErrorHandler(BOMCopier copier, BOMCopierFileErrorHandler handler);
void BOMCopierSetPKZipPasswordRequester(BOMCopier copier, BOMCopierPKZipPasswordRequester handler);

/* BOMFSObject */
const char *BOMFSObjectPathName(BOMFSObject object);
CFStringRef BOMFSObjectShortNameString(BOMFSObject object);
const char *BOMFSObjectShortName(BOMFSObject object);
Boolean BOMFSObjectIsBinaryObject(BOMFSObject object);
Boolean BOMFSObjectContainsArchitecture(BOMFSObject object, cpu_type_t cputype);
BOMFSOType BOMFSObjectType(BOMFSObject object);
BOMFSObject BOMFSObjectNewWithSys(BOMFSOType type, BomSys sys);
BOMFSObject BOMFSObjectParseSummary(const char *summary);
uint16_t BOMFSObjectMode(BOMFSObject object);
void *BOMFSObjectOpaqueData(BOMFSObject object);
size_t BOMFSObjectOpaqueDataSize(BOMFSObject object);
const char *BOMFSObjectSummary(BOMFSObject object, Boolean showModTime, Boolean supressModes, cpu_type_t cputype);
const char *BOMFSObjectSummaryWithFormat(BOMFSObject object, const char *format, cpu_type_t cputype);
void BOMFSObjectSetFlags(BOMFSObject object, BOMFSOFlags flags);
void BOMFSObjectSetPathName(BOMFSObject object, const char *path, Boolean copyString);
void BOMFSObjectSetShortName(BOMFSObject object, const char *shortName, Boolean copyString);
void BOMFSObjectFree(BOMFSObject object);

/* BOMBomEnumerator */
BOMBomEnumerator BOMBomEnumeratorNewWithOptions(BOMBom bom, BOMFSObject object, int options /* BOMBomEnumeratorOption */);
BOMFSObject BOMBomEnumeratorNext(BOMBomEnumerator enumurator);
void BOMBomEnumeratorFree(BOMBomEnumerator enumurator);

/* BOMStorage */
Boolean BOMStorageIsStorageFileWithSys(const char *path, BomSys sys);
size_t BOMStorageSizeOfBlock(BOMStorage storage, BOMBlockID bid);
int BOMStorageCopyFromBlock(BOMStorage storage, BOMBlockID bid, void *data);
int BOMStorageDump(BOMStorage storage, BOMStorageDumpOption option);

/* BOMTree */
BOMTree BOMTreeOpenWithName(BOMStorage storage, const char *name, Boolean forWriting);
int BOMTreeCount(BOMTree tree);
BOMStorage BOMTreeStorage(BOMTree tree);
int BOMTreeFree(BOMTree tree);

/* BOMTreeIterator */
BOMTreeIterator BOMTreeIteratorNew(BOMTree tree, void *startKey, void *endKey, void *context);
void *BOMTreeIteratorKey(BOMTreeIterator iterator);
void *BOMTreeIteratorValue(BOMTreeIterator iterator);
void BOMTreeIteratorNext(BOMTreeIterator iterator);
Boolean BOMTreeIteratorIsAtEnd(BOMTreeIterator iterator);
void BOMTreeIteratorFree(BOMTreeIterator iterator);

__END_DECLS

#endif
