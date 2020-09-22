/*=============================================================================
	Public LZMA header for XC_Core and other extensions
=============================================================================*/

#include "XC_CoreGlobals.h"
#include "Cacus/Atomics.h"
#define COMPRESSED_EXTENSION TEXT(".lzma")
#undef GetCompressedFileSize

XC_CORE_API UBOOL LzmaCompress( const TCHAR* Src, const TCHAR* Dest, TCHAR* Error); //Define at least 128 chars for Error
XC_CORE_API UBOOL LzmaDecompress( const TCHAR* Src, const TCHAR* Dest, TCHAR* Error); 
XC_CORE_API UBOOL LzmaDecompress( FArchive* SrcFile, const TCHAR* Dest, TCHAR* Error); //OLDVER

class XC_CORE_API ULZMACompressCommandlet : public UCommandlet
{
	DECLARE_CLASS(ULZMACompressCommandlet,UCommandlet,CLASS_Transient,XC_Core);
	NO_DEFAULT_CONSTRUCTOR(ULZMACompressCommandlet)
	INT Main( const TCHAR* Parms );
};

class XC_CORE_API ULZMADecompressCommandlet : public UCommandlet
{
	DECLARE_CLASS(ULZMADecompressCommandlet,UCommandlet,CLASS_Transient,XC_Core);
	NO_DEFAULT_CONSTRUCTOR(ULZMADecompressCommandlet)
	INT Main( const TCHAR* Parms );
};


//
// Compressed source info
//
enum ECompressedSourceState
{
	CS_STATE_Waiting,
	CS_STATE_Initializing,
	CS_STATE_Compressing,
	CS_STATE_Ready,
	CS_STATE_NoSource
};

class FLZMASourceBase
{
public:
	FGuid   Guid;
	FString Filename;
	INT     OriginalSize;
	INT     CompressedSize;
	ECompressedSourceState State;
	INT     Priority;
	INT     ActiveRequests;

	FLZMASourceBase( const FPackageInfo& Info);
	virtual ~FLZMASourceBase()	{}

	virtual FArchive* CreateReader()=0;
	virtual void*     GetMemory()             { return nullptr;}
	virtual INT       GetMemorySize()         { return 0; };
	virtual FString   GetCompressedFile()     { return TEXT(""); }
	virtual INT       GetCompressedFileSize() { return 0; };
};

//
// LZMA file subsystem
//
class XC_CORE_API ULZMAServer : public USubsystem
{
	DECLARE_CLASS(ULZMAServer,USubsystem,CLASS_Transient,XC_Core);
	NO_DEFAULT_CONSTRUCTOR(ULZMAServer);

	// Internal
	TArray<FLZMASourceBase*> Sources;
	void* ThreadPtr;
	CAtomicLock    AsyncLock;
	volatile INT   AsyncSource; // Main thread uses this as directive for worker.
	void*          AsyncCompressedData;
	size_t         AsyncCompressedSize;
	FArchive*      AsyncReader;
	volatile INT*  AsyncKey; // Worker thread uses this to verify Main still available.

	// Status
	UBOOL bPendingRelocation;
	UBOOL bProcessingMap;
	FLOAT LastUpdated;
	TCHAR LastError[256];

	// Config
	INT MaxMemCacheMegs;
	INT MaxFileCacheMegs;
	INT ForceSourceToFileMegs;

	void StaticConstructor();

	// UObject interface
	void Destroy();

	// USubsystem interface
	void Tick( FLOAT DeltaTime);

	// FExec interface
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar );

	// ULZMAServer interface
	virtual void Init();
	virtual void UpdatePackageMap( UPackageMap* NewPackageMap);
	virtual void RelocateSources( UBOOL bCleanupDisk=0);
	virtual void AddFileCacheEntry( const TCHAR* CmpFilename, const TCHAR* SrcFilename);

	FLZMASourceBase* GetSource( const FPackageInfo& Info);
	INT GetCompressedSize( const FPackageInfo& Info, UBOOL AddPriority);
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
