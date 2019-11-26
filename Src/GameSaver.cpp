
#include "XC_Core.h"
#include "Engine.h"
#include "UnLinker.h"
#include "XC_GameSaver.h"

#define CURRENT_FILE_VERSION 1
static int32 FileVersion = CURRENT_FILE_VERSION;
static FName FNAME_None( NAME_None);

static FNameEntry* GetNameEntry( FName& N)
{
	if ( N.IsValid() )
		return FName::GetEntry( N.GetIndex() );
	return nullptr;
}

struct FArchiveSetError : public FArchive
{
	static void Error( FArchive& Ar)
	{
		((FArchiveSetError&)Ar).ArIsError = 1;
	}
};


/*-----------------------------------------------------------------------------
	Save file private containers
-----------------------------------------------------------------------------*/

struct FIndexedNameArray : protected TArray<FName>
{
	using FArray::Num;
	using TArray<FName>::operator();

	int32 GetIndex( FName Name);

	friend FArchive& operator<<( FArchive& Ar, FIndexedNameArray& A);
};

struct FSaveGameElement
{
	UObject* Object;
	FName Name;

	DWORD ObjectFlags;
	int32 NameIndex;
	int32 ClassIndex;
	int32 WithinIndex;

	int8 DataQueued;
	int8 IsImport;
	TArray<uint8> Data;

	FSaveGameElement() {}
	FSaveGameElement( UObject* InObject, class FArchiveGameSaverTag& GameSaverTag);

	friend FArchive& operator<<( FArchive& Ar, FSaveGameElement& S);
};

struct FCompactReachSpec
{
	INT distance, Start, End, CollisionRadius, CollisionHeight, reachFlags; 
	BYTE  bPruned;

	friend FArchive& operator<<(FArchive &Ar, FCompactReachSpec &R );

	void From( const FReachSpec& Spec, struct FSaveFile& SaveFile);
	void To( FReachSpec& Spec, struct FSaveFile& SaveFile) const;
};


struct FSaveFile
{
	FSaveSummary             Summary;
	TMap<FString,FString>    TravelInfo;
	FIndexedNameArray        Names;
	TArray<FSaveGameElement> Elements;
	TArray<int32>            ActorList;
	TArray<FCompactReachSpec> ReachSpecs;

	ULevel* Level;

	int32 GetSavedIndex( UObject* Object);

	friend FArchive& operator<<( FArchive& Ar, FSaveFile& S);
};



/*-----------------------------------------------------------------------------
	Save file I/O
-----------------------------------------------------------------------------*/

static void SaveName( FArchive& Ar, const TCHAR* String)
{
	int32 i = 0;
	uint8 Data[NAME_SIZE];
	while ( *String )
	{
		Data[i] = (uint8)((*String++) & 0xFF);
		if ( !Data[i] )
			Data[i] = '_'; //Hack fix just in case for chars
		i++;
	}
	Data[i++] = '\0';
	Ar.Serialize( Data, i);
}

static bool LoadName( FArchive& Ar, TCHAR* String)
{
	int32 i = 0;
	uint8 Data[NAME_SIZE];
	while ( i < NAME_SIZE )
	{
		Ar.Serialize( &Data[i], 1);
		if ( Data[i] == '\0' )
		{
			for ( int32 j=0 ; j<=i ; j++ )
				String[j] = (TCHAR)Data[j];
			return true;
		}
		i++;
	}
	FArchiveSetError::Error(Ar);
	return false;
}

FArchive& operator<<( FArchive& Ar, FIndexedNameArray& A)
{
	guard(FIndexedNameArray::<<);
	int32 NameCount = A.Num();
	Ar << AR_INDEX(NameCount);
	if ( Ar.IsLoading() )
	{
		A.Empty();
		A.Add(NameCount);
		TCHAR SerializedName[NAME_SIZE];
		for ( int32 i=0 ; i<NameCount ; i++ )
		{
			if ( !LoadName(Ar,SerializedName) )
				break;
			A(i) = FName( SerializedName );
		}
	}
	else if ( Ar.IsSaving() )
	{
		for ( int32 i=0 ; i<NameCount ; i++ )
			SaveName( Ar, *A(i) );
	}
	else
	{
		for ( int32 i=0 ; i<NameCount ; i++ )
			Ar << A(i);
	}
	return Ar;
	unguard;
}

FArchive& operator<<( FArchive& Ar, FSaveGameElement& S)
{
	return Ar << S.ObjectFlags << AR_INDEX(S.NameIndex) << AR_INDEX(S.ClassIndex) << AR_INDEX(S.WithinIndex) << S.IsImport << S.Data;
}

FArchive& operator<<( FArchive& Ar, FSaveSummary& S)
{
	FileVersion = CURRENT_FILE_VERSION;
	uint8 Header[] = { 'U', 'S', 'X', CURRENT_FILE_VERSION };
	Ar.Serialize( Header, sizeof(Header));
	if ( Ar.IsLoading() )
	{
		if ( Header[0]!='U' || Header[1]!='S' || Header[2]!='X' || Header[3]==0 || Header[3]>CURRENT_FILE_VERSION )
			Ar.Close();
		FileVersion = Header[3];
	}
	return Ar << S.Players << S.LevelTitle << S.Notes << S.URL << S.GUID;
}

FArchive& operator<<(FArchive &Ar, FCompactReachSpec &R )
{
	Ar << AR_INDEX(R.distance) << AR_INDEX(R.Start) << AR_INDEX(R.End) << AR_INDEX(R.CollisionRadius) << AR_INDEX(R.CollisionHeight);
	Ar << AR_INDEX(R.reachFlags) << R.bPruned;
	return Ar;
};

FArchive& operator<<( FArchive& Ar, FSaveFile& S)
{
	Ar << S.Summary;
	if ( !Ar.IsError() )
		Ar << S.TravelInfo << S.Names << S.Elements << *(TArray<FCompactIndex>*)&S.ActorList << *(TArray<FCompactReachSpec>*)&S.ReachSpecs;
	return Ar;
}

/*-----------------------------------------------------------------------------
	Save file data logic
-----------------------------------------------------------------------------*/

UBOOL FSaveSummary::IsValid()
{
	return URL.Map != TEXT("");
}

//
// Returns name index, addes new one
//
int32 FIndexedNameArray::GetIndex( FName N)
{
	guard(FSaveFile::GetNameIndex);
	FNameEntry* Entry = GetNameEntry(N);
	if ( !Entry )
		return 0;
	if ( Entry->Flags & RF_TagExp )
	{
		int32 Index = FindItemIndex(N);
		check(Index != INDEX_NONE);
		return Index;
	}
	Entry->Flags |= RF_TagExp;
	return AddItem(N);
	unguard;
}

//
// ReachSpec treatment
//
void FCompactReachSpec::From( const FReachSpec& Spec, FSaveFile& SaveFile)
{
	distance = Spec.distance;
	Start = SaveFile.GetSavedIndex(Spec.Start);
	End = SaveFile.GetSavedIndex(Spec.End);
	CollisionRadius = Spec.CollisionRadius;
	CollisionHeight = Spec.CollisionHeight;
	reachFlags = Spec.reachFlags; 
	bPruned = Spec.bPruned;
}
void FCompactReachSpec::To( FReachSpec& Spec, FSaveFile& SaveFile) const
{
	Spec.distance = distance;
	Spec.Start = Start ? (AActor*)SaveFile.Elements(Start-1).Object : nullptr;
	Spec.End = End ? (AActor*)SaveFile.Elements(End-1).Object : nullptr;
	Spec.CollisionRadius = CollisionRadius;
	Spec.CollisionHeight = CollisionHeight;
	Spec.reachFlags = reachFlags; 
	Spec.bPruned = bPruned;
}

//
// Returns import index
//
int32 FSaveFile::GetSavedIndex( UObject* Object)
{
	if ( Object && (Object->GetFlags() & RF_TagExp) )
	{
		for ( int32 i=0 ; i<Elements.Num() ; i++ )
			if ( Elements(i).Object == Object )
				return i+1;
	}
	return 0;
}

/*-----------------------------------------------------------------------------
	Serializers
-----------------------------------------------------------------------------*/

//
// Soft Linker, uses the Linker's loader to grab data without modifying the UObject system
//
class FArchiveLinkerSerializer : public FArchive
{
public:
	ULinkerLoad* Linker;
	UObject* SerializeInto;
	int32 Pos;
	TArray<BYTE> Buffer;

	FArchiveLinkerSerializer( UObject* RealObject, UObject* FakeDefaults)
	{
		ArIsLoading = 1;
		ArIsPersistent = 1;

		Linker = RealObject->GetLinker();
		auto SavedPos = Linker->Loader->Tell();
		FObjectExport& Export = Linker->ExportMap( RealObject->GetLinkerIndex() );
		Linker->Loader->Seek( Export.SerialOffset );
		Linker->Loader->Precache( Export.SerialSize );
		Pos = 0;
		Buffer.AddZeroed( Export.SerialSize + 32); //Extra size can't hurt
		Linker->Loader->Serialize( Buffer.GetData(), Export.SerialSize);
		Linker->Loader->Seek( SavedPos );

		SerializeInto = FakeDefaults;
		*(PTRINT*)SerializeInto = *(PTRINT*)RealObject; //Copy vtable
		SerializeInto->SetClass( RealObject->GetClass() );
		SerializeInto->SetFlags( RF_NeedLoad | RF_Preloading | (RealObject->GetFlags() & RF_HasStack) );
	}


	void Start()
	{
		guard(SerializeImportFromLevel);
		SerializeInto->Serialize( *this );
		SerializeInto->ConditionalPostLoad();
		if ( SerializeInto->GetStateFrame() )
			delete SerializeInto->GetStateFrame();
		unguard;
	}

	void Serialize( void* V, INT Length ) override
	{
		appMemcpy( V, &Buffer(Pos), Length);
		Pos += Length;
	}

	FString GetImportPath( FObjectImport& Import)
	{
		FString TopPath = *Import.ObjectName;
		int32 PackageIndex = -Import.PackageIndex - 1;
		if ( PackageIndex >= 0 )
			TopPath = GetImportPath( Linker->ImportMap(PackageIndex) ) + TEXT(".") + TopPath;
		return TopPath;
	}

	FArchive& operator<<( UObject*& Object )
	{
		INT Index;
		*this << AR_INDEX(Index);
		Object = nullptr;
		if ( Index > 0 ) //This is one of the level's exports
		{
			Index--;
			Object = Linker->ExportMap( Index )._Object;
			if ( !Object || Object->IsPendingKill() )
				Object = (UObject*)-1; //HACK TO FORCE A DELTA!
		}
		else if ( Index < 0 ) //This is one of the level's imports
		{
			Index = -Index - 1;
			FObjectImport& Import = Linker->ImportMap( Index );
			if ( Import.SourceLinker )
			{
				FString ClassPath = FString::Printf( TEXT("%s.%s"), *Import.ClassPackage, *Import.ClassName);
				UClass* Class = (UClass*)UObject::StaticFindObject( UClass::StaticClass(), nullptr,*ClassPath, 1);
				FString TopPath = GetImportPath( Import );
				Object = UObject::StaticFindObject( Class, nullptr, *TopPath, Class != nullptr);
				if ( !Object )
					debugf( TEXT("Failed to locate object %s"), *TopPath);
			}
			if ( !Object )
			{
				debugf( TEXT("Failed to locate import %s (%s.%s) %i"), *Import.ObjectName, *Import.ClassPackage, *Import.ClassName, Import.PackageIndex);
				Object = (UObject*)-1; //HACK TO FORCE A DELTA!
			}
		}
		return *this;
	}
	FArchive& operator<<( FName& Name )
	{
		NAME_INDEX NameIndex;
		*this << AR_INDEX(NameIndex);
		if( !Linker->NameMap.IsValidIndex(NameIndex) )
			appErrorf( TEXT("Bad name index %i/%i"), NameIndex, Linker->NameMap.Num() );	
		Name = Linker->NameMap( NameIndex );
		return *this;
	}

};

//
// Game loader
//
class FArchiveGameLoader : public FArchive
{
public:
	FSaveFile* SaveData;
	TArray<uint8>* Buffer;
	int32 Pos;

	FArchiveGameLoader( FSaveFile* InSaveData, TArray<uint8>* InBuffer)
		: SaveData(InSaveData), Buffer(InBuffer), Pos(0)
	{
		ArIsLoading = 1;
		ArIsPersistent = 1;
	}

	virtual void Serialize( void* V, INT Length)
	{
		guard(FArchiveGameLoader::Serialize);
		if ( Pos + Length > Buffer->Num() )
		{
			GWarn->Logf( TEXT("FArchiveGameLoader going out of bounds %i -> %i / %i"), Pos, Pos+Length, Buffer->Num() );
			appMemzero( V, Length);
		}
		else
		{
			uint8* Data = (uint8*)Buffer->GetData() + (PTRINT)Pos;
			appMemcpy( V, Data, Length);
		}
		Pos += Length;
		unguard;
	}

	virtual FArchive& operator<<( class FName& N )
	{
		guard(FArchiveGameLoader::Name<<);
		int32 Index;
		*this << AR_INDEX( Index );
		N = SaveData->Names(Index); //TODO: NEEDS ERROR HANDLING
		return *this;
		unguard;
	}

	virtual FArchive& operator<<( UObject*& Object )
	{
		guard(FArchiveGameLoader::Object<<);
		int32 Index;
		*this << AR_INDEX( Index );
		if ( Index > 0 ) //TODO: NEEDS ERROR HANDLING
			Object = SaveData->Elements(Index-1).Object;
		else
			Object = nullptr;
		return *this;
		unguard;
	}
};



//
// Common interface of FArchiveGameSaver
// Creates name entries and converts tagged objects to indices
//
class FArchiveGameSaver : public FArchive
{
public:
	FSaveFile* SaveData;

	FArchiveGameSaver( FSaveFile* InSaveData)
		: SaveData(InSaveData)
	{
		ArIsSaving = 1;
		ArIsPersistent = 1;
	}

	virtual FArchive& operator<<( class FName& N )
	{
		int32 Index = SaveData->Names.GetIndex(N);
//		debugf( TEXT("... Indexing name [%i]=%s"), Index, *N);
		return *this << AR_INDEX( Index);
	}

	virtual FArchive& operator<<( UObject*& Object )
	{
		int32 Index = SaveData->GetSavedIndex( Object );
		return *this << AR_INDEX( Index);
	}

	virtual INT MapObject( UObject* Object )
	{
		return (Object && (Object->GetFlags() & (RF_TagExp))) ? SaveData->GetSavedIndex(Object) : 0;
	}
};

//
// Saves data to a local buffer
//
class FArchiveGameSaverBuffer : public FArchiveGameSaver
{
public:
	TArray<BYTE> Buffer;

	FArchiveGameSaverBuffer( FSaveFile* InSaveData)
		: FArchiveGameSaver(InSaveData)
	{}

	virtual void Serialize( void* V, INT Length )
	{
		int32 Offset = Buffer.Add( Length );
		appMemcpy( &Buffer(Offset), V, Length);
	}
};


//
// Tags objects and names and generates their save headers
//
class FArchiveGameSaverTag : public FArchiveGameSaver
{
public:
	using FArchiveGameSaver::FArchiveGameSaver;

	virtual FArchive& operator<<( UObject*& Res )
	{
		guard(FArchiveGameSaverTag::<<);
		if ( Res && !(Res->GetFlags() & RF_TagExp) && !Res->IsPendingKill()  )
		{
			Res->SetFlags(RF_TagExp);
			if ( !Res->IsIn(UObject::GetTransientPackage()) )
			{
				if ( !(Res->GetFlags() & RF_Transient) || Res->IsA(UField::StaticClass()) )
				{
					//Automatically adds super imports
					FSaveGameElement Element( Res, *this); 
					int32 Index = SaveData->Elements.AddZeroed();
					ExchangeRaw( SaveData->Elements(Index), Element);

					//Continue tagging via recursion
					if ( Res->IsIn(SaveData->Level->GetOuter()) )
						Res->Serialize( *this );
				}
			}
		}
		return *this;
		unguardf(( TEXT("(%s)"), ObjectFullName(Res) ));
	}
};

//
// Saves tagged objects' data and queues more objects for saving.
//
class FArchiveGameSaverProperties : public FArchiveGameSaverBuffer
{
public:
	int32 i;
	TArray<int32> ObjectIndices;

	FArchiveGameSaverProperties( FSaveFile* InSaveData)
		: FArchiveGameSaverBuffer(InSaveData), i(0)
	{}

	virtual FArchive& operator<<( UObject*& Obj)
	{
		int32 Index = SaveData->GetSavedIndex(Obj);
		AddObject( Index );
		return *this << AR_INDEX( Index);
	}

	bool AddObject( int32 Index)
	{
		Index--;
		if ( (Index < 0) || (Index >= SaveData->Elements.Num()) )
			return false;

		FSaveGameElement& Element = SaveData->Elements(Index);
		if ( Element.DataQueued || !Element.Object->IsIn(SaveData->Level->GetOuter()) )
			return false;

		Element.DataQueued = 1;
		ObjectIndices.AddItem(Index);
		return true;
	}

	void ProcessObjects()
	{
		guard(FArchiveGameSaverProperties::ProcessObjects)
		for ( ; i<ObjectIndices.Num() ; i++ )
		{
			TArray<BYTE> SavedDefaults;
			FSaveGameElement& Element = SaveData->Elements( ObjectIndices(i) );
			UObject* Object = Element.Object;

			//Temporarily swap defaults so we obtain a delta of the Import instead of class defaults.
			Element.IsImport = Object->GetLinker() && (Object->GetLinkerIndex() != INDEX_NONE);
			if ( Element.IsImport )
			{
				SavedDefaults = Object->GetClass()->Defaults;
				FArchiveLinkerSerializer ArLinker( Object, (UObject*)SavedDefaults.GetData() );
				ArLinker.Start();
				ExchangeRaw( Object->GetClass()->Defaults, SavedDefaults);
			}
			
			Object->Serialize( *this );
			ExchangeRaw( Buffer, Element.Data);
			Buffer.Empty();
//			debugf( TEXT("Serialized %i bytes of %s"), Element.Data.Num(), ObjectPathName( Object) );

			if ( Element.IsImport )
				ExchangeRaw( Object->GetClass()->Defaults, SavedDefaults);
		}
		unguard;
	}
	
};

/*-----------------------------------------------------------------------------
	Save game element
-----------------------------------------------------------------------------*/

FSaveGameElement::FSaveGameElement( UObject* InObject, FArchiveGameSaverTag& GameSaverTag)
	: Object(InObject)
	, Name(InObject->GetFName())
	, ObjectFlags(InObject->GetFlags() & RF_Load)
	, DataQueued(0)
	, IsImport(2)
{
	// Tag the name (manually)
	NameIndex = GameSaverTag.SaveData->Names.GetIndex( Name );

	// Tag the class
	UClass* Class = Object->GetClass();
	GameSaverTag << Class;
	ClassIndex = GameSaverTag.SaveData->GetSavedIndex( Class );

	// Tag the outer
	UObject* Outer = Object->GetOuter();
	if ( Outer )
		GameSaverTag << Outer;
	WithinIndex = GameSaverTag.SaveData->GetSavedIndex( Outer );
}


/*-----------------------------------------------------------------------------
	Main interfaces
-----------------------------------------------------------------------------*/

namespace XC
{

XC_CORE_API void GetSaveGameList( TArray<FSaveSummary>& SaveList)
{
	FString Path = GSys->SavePath;
	Path += PATH_SEPARATOR TEXT("*.usx");
	TArray<FString> FileList = GFileManager->FindFiles( *Path, 1, 0);

	SaveList.Empty();
	for ( int32 i=0 ; i<FileList.Num() ; i++ )
	{
		FSaveSummary Summary;
		if ( LoadSaveGameSummary( Summary, *FileList(i)) )
		{
			int32 Index = SaveList.AddZeroed();
			ExchangeRaw( SaveList(Index), Summary);
		}
	}
}

XC_CORE_API UBOOL LoadSaveGameSummary( FSaveSummary& SaveSummary, const TCHAR* FileName)
{
	guard(XC::LoadSaveGameSummary);
	UBOOL bSuccess = 0;
	FArchive* Ar = GFileManager->CreateFileReader( FileName, 0);
	if ( Ar && Ar->TotalSize() && !Ar->IsError() )
	{
		FSaveSummary Summary;
		*Ar << Summary;
		if ( !Ar->IsError() && Summary.IsValid() )
		{
			bSuccess = 1;
			ExchangeRaw( SaveSummary, Summary);
		}
	}
	delete Ar;
	return bSuccess;
	unguard;
}



XC_CORE_API UBOOL SaveGame( ULevel* Level, const TCHAR* FileName, uint32 SaveFlags)
{
	guard(XC::SaveGame);

	if ( !Level->GetLinker() )
	{
		debugf( TEXT("No linker!"));
		return 0;
	}

	guard(UntagInitial);
	for( FObjectIterator It; It; ++It )
	{
		if ( It->IsA(AStatLog::StaticClass()) ) //Do not export these classes
			It->SetFlags( RF_TagExp );
		else
			It->ClearFlags( RF_TagExp );
	}
	for( int32 i=0; i<FName::GetMaxNames(); i++ )
		if( FName::GetEntry(i) )
			FName::GetEntry(i)->Flags &= ~(RF_TagExp);
	unguard;

	FSaveFile SaveFile;
	SaveFile.Level = Level;
//	SaveFile.Summary.Players;
	SaveFile.Summary.LevelTitle = Level->GetLevelInfo()->Title;
//	SaveFile.Summary.Notes;
	SaveFile.Summary.GUID = ((ULinker*)Level->GetLinker())->Summary.Guid;
	SaveFile.TravelInfo = Level->TravelInfo;

	guard(Tag);
	FArchiveGameSaverTag ArTag( &SaveFile );
	(FArchive&)ArTag << FNAME_None; //MSVC being dumb
	ArTag << Level;
	unguard;

	guard(Process);
	FArchiveGameSaverProperties ArProp( &SaveFile );
	for ( int32 i=0 ; i<Level->Actors.Num() ; i++ )
	{
		int32 Index = SaveFile.GetSavedIndex(Level->Actors(i));
		SaveFile.ActorList.AddItem(Index);
		if ( ArProp.AddObject(Index) )
			ArProp.ProcessObjects();
	}
	unguard;

	guard(ReachSpecs)
	int32 SpecCount = Level->ReachSpecs.Num();
	SaveFile.ReachSpecs.SetSize( SpecCount );
	for ( int32 i=0 ; i<SpecCount ; i++ )
		SaveFile.ReachSpecs(i).From( Level->ReachSpecs(i), SaveFile);
	unguard;

	guard(SaveToFile);
	GFileManager->MakeDirectory( *GSys->SavePath, 1);
	FArchive* Saver = GFileManager->CreateFileWriter( FileName, FILEWRITE_EvenIfReadOnly);
	*Saver << SaveFile;
	UBOOL IsError = Saver->IsError();
	delete Saver;
	return !IsError;
	unguard;

	unguard;
}

XC_CORE_API UBOOL LoadGame( ULevel* Level, const TCHAR* FileName)
{
	if ( !FileName )
		return false;

	FSaveFile SaveFile;
	guard(LoadFromFile);
	FArchive* Loader = GFileManager->CreateFileReader( FileName, FILEWRITE_EvenIfReadOnly);
	if ( !Loader || !Loader->TotalSize() || Loader->IsError() )
	{
		debugf( /*NAME_DevLoad,*/ TEXT("LoadGame error: failed loading %s"), FileName);
		return false;
	}
	*Loader << SaveFile;
	delete Loader;
	if ( !SaveFile.Summary.IsValid() || (SaveFile.ActorList.Num() <= 0) )
	{
		debugf( /*NAME_DevLoad,*/ TEXT("LoadGame error: save file %s has bad data"), FileName);
		return false;
	}
	unguard;

	guard(SetupGeneral);
	Level->URL = SaveFile.Summary.URL;
	Level->TravelInfo = SaveFile.TravelInfo;
	unguard;

	//Names are already autogenerated during load process
	guard(SetupImports);
	for ( int32 i=0 ; i<SaveFile.Elements.Num() ; i++ )
	{
		//Setup name
		FSaveGameElement& Element = SaveFile.Elements(i);
		int32 NameIndex = Element.NameIndex;
		if ( NameIndex < 0 || NameIndex >= SaveFile.Names.Num() )
		{
			GWarn->Logf( TEXT("LoadGame error: found object with invalid name index %i/%i"), NameIndex, SaveFile.Names.Num() );
			return false;
		}
		Element.Name = SaveFile.Names( NameIndex);

		//Setup outer
		FString PathName;
		FSaveGameElement* ElementPtr = &Element;
		while ( ElementPtr )
		{
			if ( PathName.Len() )
				PathName = FString(TEXT(".")) + PathName;
			PathName = FString(*ElementPtr->Name) + PathName;

			int32 WithinIndex = ElementPtr->WithinIndex;
			ElementPtr = nullptr;
			if ( WithinIndex > SaveFile.Elements.Num() )
			{
				GWarn->Logf( TEXT("LoadGame error: found object with invalid outer index %i/%i"), WithinIndex-1, SaveFile.Elements.Num() );
				return 0;
			}
			else if ( WithinIndex > 0 )
				ElementPtr = &SaveFile.Elements( WithinIndex-1 );
		}

		UClass* Class = nullptr;
		if ( Element.ClassIndex > 0 )
		{
			Class = (UClass*)SaveFile.Elements( Element.ClassIndex-1 ).Object;
			if ( Class && (Class->GetClass() != UClass::StaticClass()) )
			{
				GWarn->Logf( TEXT("LoadGame error: referred class is not a class %s"), Class->GetPathName() );
				return 0;
			}
		}
		else if ( !Element.WithinIndex ) //This is package
			Class = UPackage::StaticClass();


		guard(AssociateObject);
		Element.Object = nullptr;
		if ( Element.IsImport == 2 )
		{
			Element.Object = UObject::StaticFindObject( Class, nullptr, *PathName, Class != nullptr);
			if ( !Element.Object && Class )
				Element.Object = UObject::StaticLoadObject( Class, nullptr, *PathName, nullptr, LOAD_Quiet, nullptr);
			if ( !Element.Object )
				GWarn->Logf( TEXT("LoadGame: failed to load %s (%s) %i"), *PathName, ObjectPathName(Class), Element.ClassIndex );
		}
		else if ( Element.IsImport == 1 )
		{
			Element.Object = UObject::StaticFindObject( Class, nullptr, *PathName, Class != nullptr);
			if ( !Element.Object ) //Failed imports don't count
				GWarn->Logf( TEXT("LoadGame: failed import %s (%s)"), *PathName, ObjectPathName(Class));
		}
		else
		{
			Element.Object = UObject::StaticFindObject( nullptr, nullptr, *PathName, 0);
			if ( Element.Object ) //Rename old object if it's interfering with an import
			{
				if ( Element.Object->GetClass() == Class ) //Attempt to replace
					Element.Object = UObject::StaticConstructObject( Class, Element.Object->GetOuter(), Element.Name, Element.Object->GetFlags() );
				else
				{
					GWarn->Logf( TEXT("LoadGame: unable to overwrite mismatching %s"), *PathName);
					Element.Object = nullptr;
				}
			}
			else if ( Class && Element.WithinIndex && SaveFile.Elements( Element.WithinIndex-1).Object )
			{
				Element.Object = UObject::StaticConstructObject( Class, SaveFile.Elements( Element.WithinIndex-1).Object, Element.Name);
				if ( Class->IsChildOf(AActor::StaticClass()) )
					Element.Object->SetFlags( RF_Transactional );
			}
			else
				GWarn->Logf( TEXT("LoadGame: failed to create object %s"), *PathName);

			//Hardcode XLevel
			if ( Cast<AActor>(Element.Object) && (Element.Object->GetOuter() == Level->GetOuter()) )
				((AActor*)Element.Object)->XLevel = Level;

			//Hardcode flags (risky!)
			if ( Element.Object )
			{
				Element.Object->ClearFlags( RF_Load );
				Element.Object->SetFlags( Element.ObjectFlags );
			}
		}
		unguard;
//		debugf( TEXT("Setting up element %s (%s) as %s"), *PathName, ObjectPathName(Class), ObjectPathName(Element.Object) );
	}
	unguard;

	guard(SetupProperties);
	for ( int32 i=0 ; i<SaveFile.Elements.Num() ; i++ )
	{
		FSaveGameElement& Element = SaveFile.Elements(i);
		if ( Element.Object && Element.Data.Num() ) //TODO: CHECK IF IN LEVEL?
		{
//			debugf( TEXT("Loading properties for %s (%i)"), ObjectPathName(Element.Object), Element.Data.Num() );
			FArchiveGameLoader ArLoader( &SaveFile, &Element.Data);
			Element.Object->Serialize( ArLoader );
		}
	}
	unguard;

	guard(SetupActorList);
	debugf( TEXT("Setting up Actor list %i -> %i"), Level->Actors.Num(), SaveFile.ActorList.Num());
	int32 ActorCount = SaveFile.ActorList.Num();
	Level->Actors.SetSize( ActorCount );
	for ( int32 i=0 ; i<ActorCount ; i++ )
	{
		int32 Index = SaveFile.ActorList(i);
		if ( !Index )
			Level->Actors(i) = nullptr;
		else if ( !SaveFile.Elements.IsValidIndex(Index-1) )
		{
			debugf( TEXT("LoadGame error: invalid object index in actor list %i/%i"), Index, SaveFile.Elements.Num() );
			return 0;
		}
		else
			Level->Actors(i) = Cast<AActor>(SaveFile.Elements(Index-1).Object); //TODO: FAIL IF NOT ACTOR
	}
	unguard;

	guard(SetupReachSpecs);
	int32 ReachSpecCount = SaveFile.ReachSpecs.Num();
	Level->ReachSpecs.SetSize( ReachSpecCount );
	for ( int32 i=0 ; i<ReachSpecCount ; i++ )
		SaveFile.ReachSpecs(i).To( Level->ReachSpecs(i), SaveFile);
	unguard;

	debugf( TEXT("LoadGame success"));

	return 1;
}



};

