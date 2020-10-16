// XC_LZMA.cpp
// This code loads the LZMA library and prepares the compression/decompression environments
// By Higor

#include "time.h"

#ifdef __UNIX__

#endif


#include "XC_Core.h"
#include "UnLinker.h"
#include "FConfigCacheIni.h"
#include "XC_LZMA.h"

#include "Cacus/CacusBase.h"
#include "Cacus/Atomics.h"
#include "Cacus/CacusThread.h"
#include "Cacus/DynamicLinking.h"
#include "Cacus/CacusString.h"


#ifdef __LINUX_X86__
	#include <stddef.h>
#endif

typedef size_t SizeT;
typedef int (STDCALL *XCFN_LZMA_Compress)(unsigned char *dest, size_t *destLen, const unsigned char *src, size_t srcLen,
  unsigned char *outProps, size_t *outPropsSize, /* *outPropsSize must be = 5 */
  int level,      /* 0 <= level <= 9, default = 5 */
  unsigned dictSize,  /* default = (1 << 24) */
  int lc,        /* 0 <= lc <= 8, default = 3  */
  int lp,        /* 0 <= lp <= 4, default = 0  */
  int pb,        /* 0 <= pb <= 4, default = 2  */
  int fb,        /* 5 <= fb <= 273, default = 32 */
  int numThreads /* 1 or 2, default = 2 */
  );

//Dictionary size changed from 16mb to 4mb to ease memory usage, besides we're only compressing small files
static INT lzma_threads = 1;
#define DEFAULT_LZMA_PARMS 5, (1<<22), 3, 0, 2, 32, lzma_threads
  
typedef int (STDCALL *XCFN_LZMA_Uncompress) (unsigned char *dest, size_t *destLen, const unsigned char *src, SizeT *srcLen,
  const unsigned char *props, size_t propsSize);

static CScopedLibrary LZMA;
static XCFN_LZMA_Compress LzmaCompressFunc = 0;
static XCFN_LZMA_Uncompress LzmaDecompressFunc = 0;

#define LZMA_CACHE_PATH TEXT("../LzmaCache/")
#define LZMA_CACHE_INI  LZMA_CACHE_PATH TEXT("LzmaCache.ini")

/*-----------------------------------------------------------------------------
	Utils
-----------------------------------------------------------------------------*/

//Load LZMA
static bool GetHandles()
{
	if ( !LZMA )
	{
#ifdef _MSC_VER
		CScopedLibrary NewLZMA("LZMA.dll");
#elif __LINUX_X86__
		CScopedLibrary NewLZMA("LZMA.so");
#endif
		if ( NewLZMA )
		{
			LzmaCompressFunc = NewLZMA.Get<XCFN_LZMA_Compress>("LzmaCompress");
			LzmaDecompressFunc = NewLZMA.Get<XCFN_LZMA_Uncompress>("LzmaUncompress");
			ExchangeRaw( LZMA, NewLZMA); // Ugly
		}
	}
	return LZMA && LzmaCompressFunc && LzmaDecompressFunc;
}



#if __UNIX__
#include "sys/types.h"
#include "sys/stat.h"
static double GetFileAge( const TCHAR* Filename )
{
	struct stat Buf;
	if( stat(appToAnsi(Filename),&Buf)==0 )
	{
		time_t CurrentTime, FileTime;
		FileTime = Buf.st_mtime;
		time( &CurrentTime );
		return difftime( CurrentTime, FileTime );
	}
	return 0;
}

#elif _WINDOWS
static double GetFileAge( const TCHAR* Filename )
{
	struct _stat Buf;
	if( _wstat(Filename,&Buf)==0 )
	{
		time_t CurrentTime, FileTime;
		FileTime = Buf.st_mtime;
		time( &CurrentTime );
		return difftime(CurrentTime,FileTime);
	}
	return 0;
}

#endif

/*-----------------------------------------------------------------------------
	ULZMACompressCommandlet.
-----------------------------------------------------------------------------*/
INT ULZMACompressCommandlet::Main( const TCHAR* Parms )
{
	FString Wildcard;
	TCHAR Error[256] = {0};
	if( !ParseToken(Parms,Wildcard,0) )
		appErrorf(TEXT("Source file(s) not specified"));
#ifdef _MSC_VER
	lzma_threads = 2;
#endif
	OSpath(Parms);
	OSpath(*Wildcard);
	do
	{
        // skip "-nohomedir", etc... --ryan.
        if ((Wildcard.Len() > 0) && ( (*Wildcard)[0] == '-'))
            continue;

		FString Dir;
		INT i = Max( Wildcard.InStr( TEXT("\\"), 1), Wildcard.InStr( TEXT("/"), 1));
		if( i != -1 )
			Dir = Wildcard.Left( i+1);
		TArray<FString> Files = GFileManager->FindFiles( *Wildcard, 1, 0 );
		if( !Files.Num() )
			appErrorf(TEXT("Source %s not found"), *Wildcard);
		for( INT j=0;j<Files.Num();j++)
		{
			FString Src = Dir + Files(j);
			FString End = Src + COMPRESSED_EXTENSION;
			FTime StartTime = appSeconds();
			LzmaCompress( *Src, *End, Error);
			
			if ( Error[0] )
				warnf( Error);
			else
			{
				INT SrcSize = GFileManager->FileSize(*Src);
				INT DstSize = GFileManager->FileSize(*(Src+COMPRESSED_EXTENSION));
				warnf(TEXT("Compressed %s -> %s%s (%d%%). Time: %03.1f"), *Src, *Src, COMPRESSED_EXTENSION, 100*DstSize / SrcSize, appSeconds() - StartTime);
			}
		}
	}
	while( ParseToken(Parms,Wildcard,0) );
	return 0;
}
IMPLEMENT_CLASS(ULZMACompressCommandlet)
/*-----------------------------------------------------------------------------
	ULZMADecompressCommandlet.
-----------------------------------------------------------------------------*/

INT ULZMADecompressCommandlet::Main( const TCHAR* Parms )
{
	FString Src;
	TCHAR Error[256] = { 0 };
	if( !ParseToken(Parms,Src,0) )
		appErrorf(TEXT("Compressed file not specified"));
	FString Dest;
    if( Src.Right(appStrlen(COMPRESSED_EXTENSION)) == COMPRESSED_EXTENSION )
		Dest = Src.Left( Src.Len() - appStrlen(COMPRESSED_EXTENSION) );
	else
		appErrorf(TEXT("Compressed files must end in %s"), COMPRESSED_EXTENSION);

	OSpath(*Src);
	OSpath(*Dest);
	if ( LzmaDecompress( *Src, *Dest, Error) )
		warnf(TEXT("Decompressed %s -> %s"), *Src, *Dest);
	else
		appErrorf( Error );
	return 0;
}
IMPLEMENT_CLASS(ULZMADecompressCommandlet)

/*-----------------------------------------------------------------------------
	LZMA internals
-----------------------------------------------------------------------------*/

#define LZMA_PROPS_SIZE 5

static const TCHAR* TranslateLzmaError( int32 ErrorCode)
{
	switch ( ErrorCode )
	{
		case 0:		return NULL;
		case 1:		return TEXT("Data error");
		case 2:		return TEXT("Memory allocation error");
		case 4:		return TEXT("Unsupported properties");
		case 5:		return TEXT("Incorrect parameter");
		case 6:		return TEXT("Insufficient bytes in input buffer");
		case 7:		return TEXT("Output buffer overflow");
		case 12:	return TEXT("Errors in multithreading functions");
		default:	return TEXT("Undocumented error code");
	}
}

#define lzPrintError( a) { appSprintf( Error, a); return 0; }
#define lzPrintErrorD( a, b) { appSprintf( Error, a, b); return 0; }


static void LzmaCompress( FArchive* Reader, void*& CompressedData, size_t& CompressedSize, TCHAR* Error)
{
	CompressedData = nullptr;
	CompressedSize = 0;

	// Required parameters
	if ( !Reader || !Error )
		return;

	if ( GetHandles() )
	{
		int32 SourceSize = Reader->TotalSize();
		void* SourceData = malloc((size_t)SourceSize);
		if ( SourceData )
		{
			Reader->Serialize( SourceData, SourceSize);
			if ( !Reader->GetError() )
			{
				size_t DestSize = (size_t)(SourceSize + SourceSize / 128 + 1024);
				void*  DestData = malloc( DestSize);
				if ( DestData )
				{
					//Compress and free source data
					uint8  Header[LZMA_PROPS_SIZE + 8];
					size_t OutPropSize = LZMA_PROPS_SIZE;
					int32 Ret = (*LzmaCompressFunc)( (uint8*)DestData + sizeof(Header), &DestSize, (uint8*)SourceData, SourceSize, Header, &OutPropSize, DEFAULT_LZMA_PARMS);
					if ( !Ret )
					{
						// Deallocate source
						free(SourceData);

						// Copy header to start of memory
						for ( uint32 i=0; i<8; i++)
							Header[OutPropSize++] = (uint8)((uint64)SourceSize >> (8 * i));
						memcpy( DestData, Header, sizeof(Header));
						DestSize += sizeof(Header);

						// Reallocate
						CompressedSize = DestSize;
						CompressedData = malloc(DestSize);
						memcpy( CompressedData, DestData, DestSize);
						free(DestData);
						return;
					}
					const TCHAR* LzmaError = TranslateLzmaError( Ret);
					if ( LzmaError ) //Got error
						appStrcpy( Error, LzmaError);
					free(DestData);
				}
				else appStrcpy( Error, TEXT("Unable to allocate compression buffer"));
			}
			else appStrcpy( Error, TEXT("Unable to read from file"));
			free(SourceData);
		}
		else appSprintf( Error, TEXT("Unable to allocate %i bytes for reading file"), SourceSize);
	}
	else appStrcpy( Error, TEXT("Unable to load LZMA library") );
}

static FString CreateFilename( const FGuid& Guid)
{
	for ( int32 i=0; i<65536; i++)	
	{
		FString TryName = Guid.String();
		if ( i > 0 )
		{
			TCHAR Buf[16];
			appSprintf( Buf, TEXT("_%i"), i);
			TryName += Buf;
		}
		TryName += TEXT(".lzma");
		if ( GetFileAge(*(FString(LZMA_CACHE_PATH)+TryName)) == 0 )
			return TryName;
	}
	return TEXT("ERROR.lzma");
}

static bool SaveDataToFile( const TCHAR* Filename, void* Data, INT Size)
{
	bool Result = false;
	FArchive* File = GFileManager->CreateFileWriter(Filename);
	if ( File )
	{
		if ( !File->GetError() )
		{
			File->Serialize( Data, Size);
			Result = !File->GetError();
			File->Close();
		}
		delete File;
	}
	return Result;
}

/*-----------------------------------------------------------------------------
	LZMA Server
-----------------------------------------------------------------------------*/

class FArchiveProxyLock : public FArchive
{
public:
	FArchive* Ar;
	INT&      Lock;

	FArchiveProxyLock( FArchive* InAr, INT& InLock)
		: Ar(InAr), Lock(InLock)
	{
		Lock++;
	}
	~FArchiveProxyLock()
	{
		Lock--;
		delete Ar;
	}

	void Serialize( void* V, INT Length )  { Ar->Serialize( V, Length); }
	virtual INT Tell()                     { return Ar->Tell(); }
	virtual INT TotalSize()                { return Ar->TotalSize(); }
	virtual void Seek( INT InPos )         { Ar->Seek(InPos); }
	virtual UBOOL Close()                  { return Ar->Close(); }
	virtual UBOOL GetError()               { return Ar->GetError(); }
};

static FArchiveProxyLock* CreateLock( FArchive* Archive, INT& Lock)
{
	if ( Archive )
	{
		if ( !Archive->GetError() )
			return new FArchiveProxyLock( Archive, Lock);
		delete Archive;
	}
	return nullptr;
}

#define NAME_LZMAServer (EName)GetClass()->GetFName().GetIndex()

FLZMASourceBase::FLZMASourceBase( const FPackageInfo& Info)
	: Guid(Info.Guid)
	, Filename(Info.Linker->Filename)
	, OriginalSize(Info.FileSize)
	, CompressedSize(0)
	, State(CS_STATE_Waiting)
	, Priority(0)
	, ActiveRequests(0)
{}


//
// Temporary stub
//
class FLZMASourceInitializator : public FLZMASourceBase
{
public:
	using FLZMASourceBase::FLZMASourceBase;

	FArchive* CreateReader()
	{
		return nullptr;
	}
};

//
// Send data from file
//
class FLZMASourceFile : public FLZMASourceBase
{
public:
	FString CmpFilename;

	FLZMASourceFile( const FLZMASourceBase& Base, void* InData)
		: FLZMASourceBase(Base)
		, CmpFilename( CreateFilename(Base.Guid) )
	{
		if ( SaveDataToFile( *(FString(LZMA_CACHE_PATH)+CmpFilename), InData, CompressedSize) )
			State = CS_STATE_Ready;
		else
			State = CS_STATE_NoSource;
	}

	FLZMASourceFile( const FPackageInfo& Info, const TCHAR* InFilename)
		: FLZMASourceBase(Info)
		, CmpFilename(InFilename)
	{
		CompressedSize = GFileManager->FileSize(InFilename);
		State = CS_STATE_Ready;
	}

	FArchive* CreateReader()
	{
		return CreateLock( GFileManager->CreateFileReader(*(FString(LZMA_CACHE_PATH)+CmpFilename)), ActiveRequests);
	}

	FString GetCompressedFile()
	{
		return CmpFilename;
	}

	INT GetCompressedFileSize()
	{
		return GFileManager->FileSize( *(FString(LZMA_CACHE_PATH)+CmpFilename) );
	};
};

//
// Send data from memory
// Note: the memory is allocated by an asynchronous compressor using malloc
//
class FLZMASourceMemory : public FLZMASourceBase
{
public:
	struct FArrayMod
	{
		void* Data;
		int32 Size;
		int32 MaxSize;
	};
	FArrayMod Data;

	FLZMASourceMemory( const FLZMASourceBase& Base, void* InData)
		: FLZMASourceBase(Base)
	{
		State = CS_STATE_Ready;
		Data.Data = InData;
		Data.Size = CompressedSize;
		Data.MaxSize = CompressedSize;
	}

	~FLZMASourceMemory()
	{
		free(Data.Data);
		Data = {nullptr,0,0};
	}

	FArchive* CreateReader()    { return CreateLock( new FBufferReader(*(TArray<BYTE>*)&Data), ActiveRequests); }
	void* GetMemory()           { return Data.Data;}
	INT   GetMemorySize()       { return CompressedSize; };
};

//
// Is this a package we shouldn't serve?
//
static UBOOL IsDefaultPackage( const TCHAR* Pkg)
{
	//Get rid of paths...
	const TCHAR* Filename;
	for ( Filename=Pkg ; *Pkg ; Pkg++ )
		if ( *Pkg == '\\' || *Pkg == '/' )
			Filename = Pkg + 1;

	//Save as ANSI text
	static const TCHAR* DefaultList[] = 
	{	TEXT("Botpack.u")
	,	TEXT("Engine.u")
	,	TEXT("Core.u")
	,	TEXT("Unreali.u")
	,	TEXT("UnrealShare.u")
	,	TEXT("Editor.u")
	,	TEXT("Fire.u")
	,	TEXT("IpServer.u")
	,	TEXT("Credits.utx")
	,	TEXT("LadderFonts.utx")
	,	TEXT("LadrStatic.utx")
	,	TEXT("LadrArrow.utx")
	,	TEXT("UMenu.u")
	,	TEXT("UTMenu.u")
	,	TEXT("UBrowser.u")
	,	TEXT("UTBrowser.u")
	,	TEXT("UTServerAdmin.u")
	,	TEXT("UWindow.u")
	,	TEXT("UWindowFonts.utx")	};

	// Compare
	const int max = ARRAY_COUNT( DefaultList);
	for ( int i=0 ; i<max ; i++ )
		if ( !appStricmp( Filename, DefaultList[i]) )
			return 1;

	return 0;
}

//
// Locates compressed files generated via old bAutoCompressLZMA (or commandlets)
//
static FString GetOldCompressedFile( const FPackageInfo& Info, const TCHAR* CompressedExt)
{
	guard(GetOldCompressedFile);
	check(Info.Linker);

	FString CompressedFilename = Info.Linker->Filename + CompressedExt;
	double FileAge    = GetFileAge(*Info.Linker->Filename);
	double CmpFileAge = GetFileAge(*CompressedFilename);
	if ( CmpFileAge > 0 && CmpFileAge < FileAge )
		return CompressedFilename;
	return FString();

	unguard;
}

//
// Initialize LZMA defaults
//
void ULZMAServer::StaticConstructor()
{
	UClass* Class = GetClass();
	ULZMAServer* Defaults = (ULZMAServer*)&Class->Defaults(0);

	Defaults->Silent                =   1;
	Defaults->MaxMemCacheMegs       =  16;
	Defaults->MaxFileCacheMegs      = 256;
	Defaults->ForceSourceToFileMegs =   8;

	// Get these to LzmaCache.ini
	new(Class,TEXT("Silent")               , RF_Public) UBoolProperty( CPP_PROPERTY(Silent)              , TEXT("Settings"), CPF_Native|CPF_Edit);
	new(Class,TEXT("MaxMemCacheMegs")      , RF_Public) UIntProperty( CPP_PROPERTY(MaxMemCacheMegs)      , TEXT("Settings"), CPF_Native|CPF_Edit);
	new(Class,TEXT("MaxFileCacheMegs")     , RF_Public) UIntProperty( CPP_PROPERTY(MaxFileCacheMegs)     , TEXT("Settings"), CPF_Native|CPF_Edit);
	new(Class,TEXT("ForceSourceToFileMegs"), RF_Public) UIntProperty( CPP_PROPERTY(ForceSourceToFileMegs), TEXT("Settings"), CPF_Native|CPF_Edit);

	// Status
	new(Class,TEXT("bPendingRelocation"),RF_Public) UBoolProperty( CPP_PROPERTY(bPendingRelocation),TEXT("LZMAServer"), CPF_Transient|CPF_Edit);
	new(Class,TEXT("bProcessingMap")    ,RF_Public) UBoolProperty( CPP_PROPERTY(bProcessingMap)    ,TEXT("LZMAServer"), CPF_Transient|CPF_Const|CPF_EditConst);
	new(Class,TEXT("LastUpdated"),RF_Public) UFloatProperty( CPP_PROPERTY(LastUpdated),TEXT("LZMAServer"), CPF_Transient|CPF_Edit);
}

//
// Safely destroy this object
//
void ULZMAServer::Destroy()
{
	guard(ULZMAServer::Destroy);

	// Wait a bit to ensure the other thread gets to read the RF_Destroyed flag change
	if ( ThreadPtr )
	{
		AsyncLock.Acquire();
		if ( AsyncKey )
			*AsyncKey = 0;
		appSleep(0.05);
		AsyncLock.Release();
	}

	// RF_Destroyed for object being destroyed!!
	for ( TArray<FLZMASourceBase*>::TIterator It(Sources); It; ++It)
		if ( *It )
			delete *It;
	Sources.Empty();
	Super::Destroy();
	unguard;
}

//
// Update the LZMA subsystem
//
void ULZMAServer::Tick(FLOAT DeltaTime)
{
	guard(ULZMAServer::Tick);

	LastUpdated += DeltaTime;

	if ( LastError[0] != '\0' )
	{
		GWarn->Log( NAME_LZMAServer, LastError);
		LastError[0] = '\0';
	}

	CThread*& Thread = (CThread*&)ThreadPtr;

	// Cleanup finished thread
	if ( Thread && Thread->IsEnded() )
	{
		AsyncKey = nullptr;
		AsyncLock.Release();
		Thread->Detach();
		delete Thread;
		Thread = nullptr;
	}


	if ( !bProcessingMap && !bPendingRelocation )
		return;

	// Worker thread still operating
	if ( AsyncLock.IsActive() && (LastUpdated < 30) )
		return;

	// Worker thread queued for start
	if ( !AsyncLock.IsActive() && Thread && (LastUpdated < 2) )
		return;

	// Delete lingering thread handler if something went wrong
	if ( Thread )
	{
		debugf( NAME_LZMAServer, TEXT("Deleted lingering compressor thread") );
		Thread->Detach();
		delete Thread;
	}

	// Setup
	Thread   = nullptr;
	AsyncKey = nullptr;
	AsyncLock.Release();

	// Lock
	CAtomicLock::CScope ScopedLock(AsyncLock);

	// This compressor just finished, evaluate what type of source to use
	if ( AsyncCompressedData && AsyncCompressedSize && Sources.IsValidIndex(AsyncSource) )
	{
		int32 i = AsyncSource;
		if ( Sources(i)->State == CS_STATE_Compressing )
		{
			// Set Size
			Sources(i)->CompressedSize = (int32)AsyncCompressedSize;

			if ( (ForceSourceToFileMegs > 0) && (Sources(i)->OriginalSize / (1024*1024) >= ForceSourceToFileMegs) )
			{
				// Directly save to file
				if ( !Silent )
					debugf( NAME_LZMAServer, TEXT("Pushing package to file cache (size limit)") );
				FLZMASourceFile* SourceFile = new FLZMASourceFile( *Sources(i), AsyncCompressedData);
				if ( SourceFile->State == CS_STATE_Ready )
					AddFileCacheEntry( *SourceFile->CmpFilename, *SourceFile->Filename);
				delete Sources(i);
				Sources(i) = SourceFile;
			}
			else
			{
				// Claim and keep in memory
				FLZMASourceMemory* SourceMem = new FLZMASourceMemory( *Sources(i), AsyncCompressedData);
				AsyncCompressedData = nullptr;
				delete Sources(i);
				Sources(i) = SourceMem;
			}
		}
	}

	// Data wasn't claimed, free it
	if ( AsyncCompressedData )
		free(AsyncCompressedData);
	if ( AsyncReader )
	{
		delete AsyncReader;
		AsyncReader = nullptr;
	}
	AsyncCompressedData = nullptr;
	AsyncCompressedSize = 0;
	AsyncSource = INDEX_NONE;

	// Clean up sources and select highest priority one
	int32 PriorityMax = -1;
	int32 PrioritySelect = INDEX_NONE;
	for ( int32 i=0; i<Sources.Num(); i++)
	{
		FLZMASourceBase* Source = Sources(i);

		// Leftover from previous level, make sure it's gone
		if ( Source->Priority < 0 )
			Source->State = CS_STATE_NoSource;

		switch ( Source->State ) 
		{
		case CS_STATE_Initializing:
		case CS_STATE_Compressing:
		case CS_STATE_NoSource:
			delete Source;
			Sources.Remove(i--);
			break;
		case CS_STATE_Waiting:
			if ( Source->Priority > PriorityMax )
			{
				PriorityMax = Source->Priority;
				PrioritySelect = i;
			}
			break;
		default:
			break;
		}
	}

	RelocateSources( bPendingRelocation);
	bPendingRelocation = false;

	// Nothing was selected, no more updates need to be pushed
	if ( !Sources.IsValidIndex(PrioritySelect) )
	{
		bProcessingMap = false;
		if ( Sources.Num() && !Silent )
			debugf( NAME_LZMAServer, TEXT("All sources processed (%i)"), Sources.Num() );
		return;
	}

	// Try to open file
	AsyncReader = GFileManager->CreateFileReader(*Sources(PrioritySelect)->Filename);
	if ( !AsyncReader || AsyncReader->GetError() )
	{
		if ( AsyncReader )
			delete AsyncReader;
		AsyncReader = nullptr;
		delete Sources(PrioritySelect);
		Sources.Remove(PrioritySelect);
		return;
	}

	// Select source
	Sources(PrioritySelect)->State = CS_STATE_Initializing;
	AsyncSource         = PrioritySelect;
	AsyncCompressedData = nullptr;
	AsyncCompressedSize = 0;
	LastUpdated = 0;
	if ( !Silent )
		debugf( NAME_LZMAServer, TEXT("Autocompressing package %s"), *Sources(PrioritySelect)->Filename);

	// Create compressor if required
	if ( !Thread )
	{
		Thread = new CThread( [](void* Arg, CThread* Handler)
		{ 
			volatile int32 Key = 1; // If this is zero, do not try to access the server
			TCHAR Error[256] = {0};
			ULZMAServer* Server = (ULZMAServer*)Arg;

			// Compres ONE source
			try
			{
				// Safety stops
				int32 i = Server->AsyncSource;
				if ( (Server->ThreadPtr != Handler) || !Server->Sources.IsValidIndex(i) )
					return THREAD_END_NOT_OK;

				Server->AsyncLock.Acquire();
				Server->AsyncKey = &Key;
				void*  Data;
				size_t Size;
				Server->Sources(i)->State = CS_STATE_Compressing;
				LzmaCompress( Server->AsyncReader, Data, Size, Error);

				// Server not available after compression ended
				// Or thread timed out and server replaced it
				if ( GIsRequestingExit || !Key || (Server->GetFlags() & RF_Destroyed) || (Server->AsyncSource != i) || (Server->ThreadPtr != Handler) )
				{
					if ( Data )
						free(Data);
					return THREAD_END_OK;
				}

				appStrcpy( Server->LastError, Error);
				Server->AsyncCompressedData = Data;
				Server->AsyncCompressedSize = Size;
				if ( !Data )
					Server->Sources(i)->State = CS_STATE_NoSource;
				Server->LastUpdated = 0;
			}
			catch(...)
			{}

			if ( Key )
			{
				Key = 0;
				Server->AsyncLock.Release();
				Server->AsyncKey = nullptr;
			}

			return THREAD_END_OK;
		}, this, 0 );
	}


	unguard;
}


//
// Execute commands
//
UBOOL ULZMAServer::Exec( const TCHAR* Cmd, FOutputDevice& Ar)
{
	return 0;
}


//
// Initialize LZMA subsystem
//
void ULZMAServer::Init()
{
	guard(ULZMAServer::Init);

	bPendingRelocation = true;

	FConfigCacheIni LzmaCacheIni;
	TArray<FString> Entries;
	TArray<FString> Files;

	GFileManager->MakeDirectory( LZMA_CACHE_PATH);

	guard(Server);
	if ( !LzmaCacheIni.GetBool( TEXT("Server"), TEXT("Silent"), Silent, LZMA_CACHE_INI) )
		LzmaCacheIni.SetBool( TEXT("Server"), TEXT("Silent"), Silent, LZMA_CACHE_INI);
	if ( !LzmaCacheIni.GetInt( TEXT("Server"), TEXT("MaxMemCacheMegs"), MaxMemCacheMegs, LZMA_CACHE_INI) )
		LzmaCacheIni.SetInt( TEXT("Server"), TEXT("MaxMemCacheMegs"), MaxMemCacheMegs, LZMA_CACHE_INI);
	if ( !LzmaCacheIni.GetInt( TEXT("Server"), TEXT("MaxFileCacheMegs"), MaxFileCacheMegs, LZMA_CACHE_INI) )
		LzmaCacheIni.SetInt( TEXT("Server"), TEXT("MaxFileCacheMegs"), MaxFileCacheMegs, LZMA_CACHE_INI);
	if ( !LzmaCacheIni.GetInt( TEXT("Server"), TEXT("ForceSourceToFileMegs"), ForceSourceToFileMegs, LZMA_CACHE_INI) )
		LzmaCacheIni.SetInt( TEXT("Server"), TEXT("ForceSourceToFileMegs"), ForceSourceToFileMegs, LZMA_CACHE_INI);
	unguard;

	// Verify compressed files
	// Delete if file entry not found in cache descriptor.
	// Delete if source file not found in the game.
	guard(VerifyFiles);
	TMultiMap<FString,FString>* Section = LzmaCacheIni.GetSectionPrivate( TEXT("LzmaCache"), 1, 0, LZMA_CACHE_INI );
	TArray<FString> Files = GFileManager->FindFiles( LZMA_CACHE_PATH TEXT("*.lzma"), true, false);
	TMultiMap<FString,FString> SectionUpdate;
	for ( TArray<FString>::TIterator It(Files); It; ++It)
	{
		FString* Value = Section->Find(*It);
		if ( !Value )
			debugf( NAME_LZMAServer, TEXT("Purging unreferenced compressed cache file [%s]"), **It);
		else
		{
			double SourceAge = GetFileAge(**Value);
			if ( SourceAge == 0 )
				debugf( NAME_LZMAServer, TEXT("Purging deleted compressed cache for %s [%s]"), **Value, **It);
			else if ( GetFileAge(**It) > SourceAge )
				debugf( NAME_LZMAServer, TEXT("Purging mismatching compressed cache for %s [%s]"), **Value, **It);
			else
			{
				SectionUpdate.Set( **It, **Value);
				continue;
			}
		}
		FString Filename = FString::Printf( LZMA_CACHE_PATH TEXT("%s"), **It);
		GFileManager->Delete(*Filename);
	}
	for ( TMultiMap<FString,FString>::TIterator It(*Section); It; ++It)
		if ( !SectionUpdate.Find(It.Key()) )
		{
			debugf( NAME_LZMAServer, TEXT("Purging delete cache source for %s [%s]"), *It.Key(), *It.Value() );
			It.RemoveCurrent();
		}
	unguard;

	unguard;
}

//
// The server has received a new PackageMap
//
void ULZMAServer::UpdatePackageMap( UPackageMap* NewPackageMap)
{
	guard(ULZMAServer::UpdatePackageMap);

	bProcessingMap = true;
	bPendingRelocation = true;
	LastUpdated = 0;

	// Mark existing sources packages with negative priority (they may be outdated)
	for ( int32 i=0; i<Sources.Num(); i++)
	{
		check( Sources(i) );
		Sources(i)->Priority = -1000;
	}

	// Enumerate existing packages to be pushed
	if ( NewPackageMap )
	{
		FConfigCacheIni LzmaCacheIni;
		TMultiMap<FString,FString>* Section = LzmaCacheIni.GetSectionPrivate( TEXT("LzmaCache"), 1, 0, LZMA_CACHE_INI );

		bool bCanCompress = GetHandles();
		for ( TArray<FPackageInfo>::TIterator It(NewPackageMap->List); It; ++It)
			if ( !IsDefaultPackage(*It->Linker->Filename) && (It->PackageFlags & PKG_AllowDownload) )
			{
				FLZMASourceBase* Source = GetSource(*It);
				if ( !Source )
				{
					// Locate old XC_Engine autocompress sources
					FString OldVerSource = GetOldCompressedFile(*It,TEXT(".lzma"));
					if ( !OldVerSource.Len() )
						OldVerSource = GetOldCompressedFile(*It,TEXT(".uz"));
					if ( OldVerSource.Len() )
						Source = new FLZMASourceFile(*It,*OldVerSource);

					// Locate in LZMA cache
					if ( Section && !Source )
					{
						for ( TMultiMap<FString,FString>::TIterator CIt(*Section); CIt; ++CIt)
							if ( CIt.Value() == *It->Linker->Filename )
							{
								Source = new FLZMASourceFile(*It,*CIt.Key());
								break;
							}
					}

					// If no existing source was found, queue for compression
					if ( bCanCompress && !Source )
						Source = new FLZMASourceInitializator(*It);

					// Add source if existing
					if ( Source )
						Sources.AddItem(Source);
				}
				Source->Priority = It.GetIndex() == 0 ? 10 : 0; //Level goes first
			}
	}

	unguard;
}


//
// Move excess sources to disk
//
struct FLZMAFileCacheInfo
{
	FString Filename;
	double  Age;
	INT     Size;
	UBOOL   Locked;

	friend INT Compare( const FLZMAFileCacheInfo& A, const FLZMAFileCacheInfo& B)
	{
		return (B.Age > A.Age) - (A.Age > B.Age);
	}
};

void ULZMAServer::RelocateSources( UBOOL bCleanupDisk)
{
	guard(ULZMAServer::RelocateSources);

	// Select largest in-memory source
	int32 MaxSize   = -1;
	int32 MaxSelect = INDEX_NONE;
	int32 TotalSize = 0;
	for ( int32 i=0; i<Sources.Num(); i++)
	{
		int32 Size = Sources(i)->GetMemorySize();
		if ( Size > 0 )
		{
			TotalSize += Size;
			if ( Size > MaxSize )
			{
				MaxSize = Size;
				MaxSelect = i;
			}
		}
	}
	TotalSize /= (1024*1024);

	// Push it to disk if available and over the limit
	if ( (TotalSize > MaxMemCacheMegs) && (MaxSelect != INDEX_NONE) && (Sources(MaxSelect)->ActiveRequests == 0) )
	{
		FLZMASourceFile* SourceFile = new FLZMASourceFile( *Sources(MaxSelect), Sources(MaxSelect)->GetMemory() );
		if ( SourceFile->State == CS_STATE_Ready )
		{
			AddFileCacheEntry( *SourceFile->CmpFilename, *SourceFile->Filename);
			if ( !Silent )
				debugf( NAME_LZMAServer, TEXT("Pushing cache to file [%s] -> [%s]"), *SourceFile->Filename, *SourceFile->CmpFilename);
			delete Sources(MaxSelect);
			Sources(MaxSelect) = SourceFile;
		}
	}

	// Limit disk size cache
	if ( bCleanupDisk )
	{
		// Get files we cannot purge
		TArray<FString> LockedSources;
		for ( int32 i=0; i<Sources.Num(); i++)
		{
			FString FileSource = Sources(i)->GetCompressedFile();
			if ( FileSource.Len() > 0 )
				new(LockedSources) FString(FileSource);
		}

		// Enumerate all cache sources
		int64 TotalSize = 0;
		TArray<FString> CacheFiles = GFileManager->FindFiles( LZMA_CACHE_PATH TEXT("*.lzma"), true, false);
		TArray<FLZMAFileCacheInfo> CacheInfo;
		CacheInfo.AddZeroed(CacheFiles.Num());
		for ( int32 i=0; i<CacheFiles.Num(); i++)
		{
			FLZMAFileCacheInfo& Info = CacheInfo(i);
			Info.Filename = FString::Printf( LZMA_CACHE_PATH TEXT("%s"), *CacheFiles(i));
			Info.Age      = GetFileAge(*Info.Filename);
			Info.Size     = GFileManager->FileSize(*Info.Filename);
			Info.Locked   = LockedSources.FindItemIndex(CacheFiles(i)) != INDEX_NONE;
			TotalSize += (int64)Info.Size;
		}

		// Sort by age and delete oldest if over the limit
		int64 MaxSize = (int64)MaxFileCacheMegs * (1024 * 1024);
		if ( TotalSize >= MaxSize )
		{
			FConfigCacheIni LzmaCacheIni;
			TMultiMap<FString,FString>* Section = LzmaCacheIni.GetSectionPrivate( TEXT("LzmaCache"), 1, 0, LZMA_CACHE_INI );

			Sort(CacheInfo);
			INT Purged = 0;
			for ( INT i=CacheInfo.Num()-1; i>=0 && TotalSize>=MaxSize; i--)
			{
				FLZMAFileCacheInfo& Info = CacheInfo(i);
				if ( !Info.Locked && GFileManager->Delete(*Info.Filename) )
				{
					Section->Remove( *Info.Filename + _len(LZMA_CACHE_PATH));
					TotalSize -= (int64)Info.Size;
					Purged++;
				}
			}
			if ( Purged && !Silent )
				debugf( NAME_LZMAServer, TEXT("Purged %i files from LZMA cache"), Purged );
		}
	}

	unguard;
}

//
// Registers a file to cache entry
//
void ULZMAServer::AddFileCacheEntry(const TCHAR* CmpFilename, const TCHAR* SrcFilename)
{
	guard(ULZMAServer::AddFileCacheEntry);

	FConfigCacheIni LzmaCacheIni;
	TMultiMap<FString,FString>* Section = LzmaCacheIni.GetSectionPrivate( TEXT("LzmaCache"), 1, 0, LZMA_CACHE_INI );
	if ( Section )
		Section->Set( CmpFilename, SrcFilename);

	unguard;
}


//
// Find by GUID
//
FLZMASourceBase* ULZMAServer::GetSource( const FPackageInfo& Info)
{
	for ( int32 i=0; i<Sources.Num(); i++)
		if ( (Sources(i)->Guid == Info.Guid) && (Sources(i)->Filename == Info.Linker->Filename) )
			return Sources(i);
	return nullptr;
}

IMPLEMENT_CLASS(ULZMAServer);

/*-----------------------------------------------------------------------------
	LZMA Unreal externals
-----------------------------------------------------------------------------*/


XC_CORE_API UBOOL LzmaCompress( const TCHAR* Src, const TCHAR* Dest, TCHAR* Error)
{
	Error[0] = '\0';
	UBOOL Result = 0;

	//Check that file exists
	FArchive* SrcFile = GFileManager->CreateFileReader( Src, 0);
	if ( SrcFile )
	{
		if ( SrcFile->TotalSize() > 0 )
		{
			void*  CompressedData;
			size_t CompressedSize;
			LzmaCompress( SrcFile, CompressedData, CompressedSize, Error); // Prints to error
			if ( CompressedData )
			{
				FArchive* DestFile = GFileManager->CreateFileWriter( Dest, 0);
				if ( DestFile )
				{
					DestFile->Serialize( CompressedData, (int32)CompressedSize);
					Result = !DestFile->GetError();
					DestFile->Close();
					delete DestFile;
				}
				else appSprintf( Error, TEXT("Unable to create destination file %s."), Dest);
				free(CompressedData);
			}
		}
		else appSprintf( Error, TEXT("Empty file %s"), Src);
		delete SrcFile;
	}
	else appSprintf( Error, TEXT("Unable to load file %s"), Src);
	return Result;
}


XC_CORE_API UBOOL LzmaDecompress( const TCHAR* Src, const TCHAR* Dest, TCHAR* Error)
{
	//Check that file exists, load and move to other method
	FArchive* SrcFile = GFileManager->CreateFileReader( Src, 0);
	Error[0] = 0;
	if ( !SrcFile )
		lzPrintErrorD( TEXT("LzmaDecompress: Unable to load file %s."), Src );
	UBOOL Result = LzmaDecompress( (FArchive*)SrcFile, Dest, Error);
	delete SrcFile;
	return Result;
}

//From old version, kept as an internal/compatibility part of the main LzmaDecompress
XC_CORE_API UBOOL LzmaDecompress( FArchive* SrcFile, const TCHAR* Dest, TCHAR* Error)
{
	try
	{
		//Validate
		Error[0] = 0;
		if ( !SrcFile )
			lzPrintError( TEXT("LzmaDecompress: No source file specified") );
		if ( !GetHandles() )
			lzPrintError( TEXT("LzmaDecompress: Unable to load LZMA library.") );

		BYTE header[ LZMA_PROPS_SIZE + 8];
		SrcFile->Serialize( &header, LZMA_PROPS_SIZE + 8);
		QWORD unpackSize = *(QWORD*) &header[LZMA_PROPS_SIZE];

		//Allocate memory and fill it with the source file's contents
		INT SrcSize = SrcFile->TotalSize() - SrcFile->Tell();
		TArray<ANSICHAR> SrcData( SrcSize);
		SrcFile->Serialize( &SrcData(0), SrcSize);
	
		//Allocate destination memory, reserve extra space to avoid nasty surprises
		INT DestSize = (INT)unpackSize;
		if ( DestSize <= 0 )
			return 0;
		TArray<ANSICHAR> DestData( DestSize);
		INT DcmpRet = LzmaDecompressFunc( (BYTE*)&DestData(0), (unsigned int*)&DestSize, (BYTE*)&SrcData(0), (unsigned int*)&SrcSize, header, LZMA_PROPS_SIZE);
		SrcData.Empty();
	
		const TCHAR* ErrorT = TranslateLzmaError( DcmpRet);
		if ( ErrorT ) //Got error
			lzPrintErrorD( TEXT("LzmaDecompress: %s."), ErrorT);
	
		//No decompression error, write data stream into file
		FArchive* DestFile = GFileManager->CreateFileWriter( Dest, FILEWRITE_EvenIfReadOnly);
		if ( !DestFile )
			lzPrintErrorD( TEXT("LzmaDecompress: Unable to create destination file %s."), Dest);
		DestFile->Serialize( &DestData(0), DestSize);
		DestFile->Close();
		delete DestFile;
		return 1;
	}
	catch(...)
	{
		return 0;
	}
}
