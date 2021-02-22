/*=============================================================================
	ScriptCompilerAdds.cpp: 
	Script Compiler addons.

	Revision history:
		* Created by Higor
=============================================================================*/

#include "XC_Core.h"
#include "XC_CoreGlobals.h"
#include "Engine.h"

//Deus-Ex script compiler
#include "UnScrCom.h"

#define PSAPI_VERSION 1
#include <Psapi.h>

#pragma comment (lib,"Psapi.lib")

//***************
// Virtual Memory
template <size_t MemSize> class TScopedVirtualProtect
{
	uint8* Address;
	uint32 RestoreProtection;

	TScopedVirtualProtect() {}
public:
	TScopedVirtualProtect( uint8* InAddress)
		: Address( InAddress)
	{
		if ( Address )	VirtualProtect( Address, MemSize, PAGE_EXECUTE_READWRITE, &RestoreProtection);
	}

	~TScopedVirtualProtect()
	{
		if ( Address )	VirtualProtect( Address, MemSize, RestoreProtection, &RestoreProtection);
	}
};

// Writes a long relative jump
static void EncodeJump( uint8* At, uint8* To)
{
	TScopedVirtualProtect<5> VirtualUnlock( At);
	uint32 Relative = To - (At + 5);
	*At = 0xE9;
	*(uint32*)(At+1) = Relative;
}
// Writes a long relative call
static void EncodeCall( uint8* At, uint8* To)
{
	TScopedVirtualProtect<5> VirtualUnlock( At);
	uint32 Relative = To - (At + 5);
	*At = 0xE8;
	*(uint32*)(At+1) = Relative;
}
// Writes bytes
/*static void WriteByte( uint8* At, uint8 Byte)
{
	TScopedVirtualProtect<1> VirtualUnlock( At);
	*At = Byte;
}*/


//***************
// Hook resources 

template < typename T1, typename T2 > struct TUnion
{
	union
	{
		T1 Type1;
		T2 Type2;
	};
	TUnion() {}
	TUnion( T1 InType1) : Type1(InType1) {}
	TUnion( T2 InType2) : Type2(InType2) {}
	operator T1&() { return Type1; }
	operator T2&() { return Type2; }
};

typedef int (*CompileScripts_Func)( TArray<UClass*>&, class FScriptCompiler_XC*, UClass*);
static TUnion<uint8*,CompileScripts_Func> CompileScripts;

static UBOOL CompileScripts_Proxy( TArray<UClass*>& ClassList, FScriptCompiler_XC* Compiler, UClass* Class );


class FScriptCompiler_XC : public FScriptCompiler
{
public:
	UField* FindField( UStruct* Owner, const TCHAR* InIdentifier, UClass* FieldClass, const TCHAR* P4);
};
typedef UField* (FScriptCompiler_XC::*FindField_Func)( UStruct*, const TCHAR*, UClass*, const TCHAR*);

//TODO: Disassemble Editor.so for more symbols
// Hook helper
struct ScriptCompilerHelper_XC_CORE
{
public:
	UBOOL bInit;
	TArray<UFunction*> ActorFunctions; //Hardcoded Actor functions
	TArray<UFunction*> ObjectFunctions; //Hardcoded Object functions
	TArray<UFunction*> StaticFunctions; //Hardcoded static functions
	//Struct mirroring causes package dependancy, we need to copy the struct

	ScriptCompilerHelper_XC_CORE()
		: bInit(0)
	{}

	void Reset()
	{
		bInit = 0;
		ActorFunctions.Empty();
		ObjectFunctions.Empty();
		StaticFunctions.Empty();
	}

	void AddFunction( UStruct* InStruct, const TCHAR* FuncName)
	{
		UFunction* F = FindBaseFunction( InStruct, FuncName);
		if ( F )
		{
			if ( F->FunctionFlags & FUNC_Static )
				StaticFunctions.AddItem( F);
			else if ( InStruct->IsChildOf( AActor::StaticClass()) )
				ActorFunctions.AddItem( F);
			else
				ObjectFunctions.AddItem( F);
		}
	}

};
static ScriptCompilerHelper_XC_CORE Helper; //Makes C runtime init construct this object


int StaticInitScriptCompiler()
{
	if ( !GIsEditor )
		return 0; // Do not setup if game instance

	HMODULE HEditor = GetModuleHandleA("Editor.dll");
	if ( !HEditor )
		return 0;

	MODULEINFO mInfo;
	GetModuleInformation( GetCurrentProcess(), HEditor, &mInfo, sizeof(MODULEINFO));
	uint8* EditorBase = (uint8*)mInfo.lpBaseOfDll;

	// Prevent multiple recursion
	static int Initialized = 0;
	if ( Initialized++ )
		return 0; 

	constexpr size_t CompileScripts_Offset                = 0x79B80;
	constexpr size_t MakeScripts_to_CompileScripts_Offset = 0x7ED73;
	constexpr size_t FindField_Offset                     = 0x7B910;

	// Get CompileScripts global/static
	CompileScripts.Type1 = EditorBase + CompileScripts_Offset;
	if ( CompileScripts.Type1[0] != 0x55 ) // PUSH EBP
		return 0;

	// Get call to CompileScripts from UEditorEngine::MakeScripts
	uint8* MakeScripts_to_CompileScripts = EditorBase + MakeScripts_to_CompileScripts_Offset;
	if ( MakeScripts_to_CompileScripts[0] != 0xE8 ) // CALL (relative)
		return 0;

	// Get FScriptCompiler::FindField address
	uint8* FindField = EditorBase + FindField_Offset;
	if ( FindField[0] != 0x55 ) // PUSH EBP
		return 0;

	// Proxy CompileScripts initial call
	EncodeCall( MakeScripts_to_CompileScripts, TUnion<uint8*,CompileScripts_Func>(&CompileScripts_Proxy));

	// Trampoline FScriptCompiler::FindField into our version
	EncodeJump( FindField, TUnion<uint8*,FindField_Func>(&FScriptCompiler_XC::FindField));
	TUnion<uint8*,FindField_Func> FindField_XC(&FScriptCompiler_XC::FindField);

	return 1;
}


static UBOOL CompileScripts_Proxy( TArray<UClass*>& ClassList, FScriptCompiler_XC* Compiler, UClass* Class )
{
	// Top call
	UBOOL Result = 1;
	if ( Class == UObject::StaticClass() )
	{
		TArray<UClass*> ImportantClasses;
		static const TCHAR* ImportantClassNames[] = { TEXT("XC_Engine_Actor"), TEXT("XC_EditorLoader")};

		for ( int i=0 ; i<ClassList.Num() ; i++ )
		for ( int j=0 ; j<ARRAY_COUNT(ImportantClassNames) ; j++ )
			if ( !appStricmp( ClassList(i)->GetName(), ImportantClassNames[j]) )
			{
				if ( j==0 ) //Needs Actor!
					ImportantClasses.AddUniqueItem( AActor::StaticClass() );
				ImportantClasses.AddUniqueItem( ClassList(i) );
				break;
			}

		if ( ImportantClasses.Num() )
		{
			Result = (*CompileScripts.Type2)(ImportantClasses,Compiler,Class); //UObject
			Helper.Reset();
		}
		if ( Result )
			Result = (*CompileScripts.Type2)(ClassList,Compiler,Class);
	}
	return Result;
}


UField* FScriptCompiler_XC::FindField( UStruct* Owner, const TCHAR* InIdentifier, UClass* FieldClass, const TCHAR* P4)
{
//	GWarn->Logf( L"FindField %s", InIdentifier);
	// Normal stuff
	check(InIdentifier);
	FName InName( InIdentifier, FNAME_Find );
	if( InName != NAME_None )
	{
		for( UStruct* St=Owner ; St ; St=Cast<UStruct>( St->GetOuter()) )
			for( TFieldIterator<UField> It(St) ; It ; ++It )
				if( It->GetFName() == InName )
				{
					if( !It->IsA(FieldClass) )
					{
						if( P4 )
							appThrowf( TEXT("%s: expecting %s, got %s"), P4, FieldClass->GetName(), It->GetClass()->GetName() );
						return nullptr;
					}
					return *It;
				}
	}

	// Initialize hardcoded opcodes
	if ( !Helper.bInit )
	{
		Helper.bInit++;
		Helper.AddFunction( ANavigationPoint::StaticClass(), TEXT("describeSpec") );
		UClass* XCGEA = FindObject<UClass>( NULL, TEXT("XC_Engine.XC_Engine_Actor"), 1);
		if ( XCGEA )
		{
			Helper.AddFunction( XCGEA, TEXT("AddToPackageMap"));
			Helper.AddFunction( XCGEA, TEXT("IsInPackageMap"));
			Helper.AddFunction( XCGEA, TEXT("PawnActors"));
			Helper.AddFunction( XCGEA, TEXT("DynamicActors"));
			Helper.AddFunction( XCGEA, TEXT("InventoryActors"));
			Helper.AddFunction( XCGEA, TEXT("CollidingActors"));
			Helper.AddFunction( XCGEA, TEXT("NavigationActors"));
			Helper.AddFunction( XCGEA, TEXT("ConnectedDests"));
			Helper.AddFunction( XCGEA, TEXT("ReplaceFunction"));
			Helper.AddFunction( XCGEA, TEXT("RestoreFunction"));
		}
		UClass* XCEL = FindObject<UClass>( NULL, TEXT("XC_Engine.XC_EditorLoader"), 1);
		if ( XCEL )
		{
			Helper.AddFunction( XCEL, TEXT("MakeColor"));
			Helper.AddFunction( XCEL, TEXT("LoadPackageContents"));
			Helper.AddFunction( XCEL, TEXT("StringToName"));
			Helper.AddFunction( XCEL, TEXT("FindObject"));
			Helper.AddFunction( XCEL, TEXT("GetParentClass"));
			Helper.AddFunction( XCEL, TEXT("AllObjects"));
			Helper.AddFunction( XCEL, TEXT("AppSeconds"));
			Helper.AddFunction( XCEL, TEXT("HasFunction"));
			Helper.AddFunction( XCEL, TEXT("Or_ObjectObject"));
			Helper.AddFunction( XCEL, TEXT("Clock"));
			Helper.AddFunction( XCEL, TEXT("UnClock"));
			Helper.AddFunction( XCEL, TEXT("AppCycles"));
			Helper.AddFunction( XCEL, TEXT("FixName"));
			Helper.AddFunction( XCEL, TEXT("HNormal"));
			Helper.AddFunction( XCEL, TEXT("HSize"));
			Helper.AddFunction( XCEL, TEXT("InvSqrt"));
			Helper.AddFunction( XCEL, TEXT("MapRoutes"));
			Helper.AddFunction( XCEL, TEXT("BuildRouteCache"));
		}
		UClass* GlobalFunctions = FindObject<UClass>( ANY_PACKAGE, TEXT("GlobalFunctions"), 1);
		if ( GlobalFunctions )
		{
			for ( TFieldIterator<UFunction>It(GlobalFunctions) ; It ; ++It )
				if ( It->GetOuter() == GlobalFunctions )
					Helper.AddFunction( GlobalFunctions, It->GetName() );
		}
	}

	if ( !FieldClass )
	{
		while ( Owner && !Owner->IsA(UClass::StaticClass()) )
			Owner = Cast<UStruct>(Owner->GetOuter());
		UBOOL IsActor = Owner && Owner->IsChildOf( AActor::StaticClass() );
		InName = FName( InIdentifier, FNAME_Find ); //Name may not have existed before
		if ( InName != NAME_None )
		{
			if ( IsActor )
				for ( int i=0 ; i<Helper.ActorFunctions.Num() ; i++ )
					if ( Helper.ActorFunctions(i)->GetFName() == InName )
						return Helper.ActorFunctions(i);
			for ( int i=0 ; i<Helper.ObjectFunctions.Num() ; i++ )
				if ( Helper.ObjectFunctions(i)->GetFName() == InName )
					return Helper.ObjectFunctions(i);
			for ( int i=0 ; i<Helper.StaticFunctions.Num() ; i++ )
				if ( Helper.StaticFunctions(i)->GetFName() == InName )
					return Helper.StaticFunctions(i);
		}
	}
	
	return nullptr;
}


