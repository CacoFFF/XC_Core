/*=============================================================================
	FPathBuilderMaster.h
	Author: Fernando Vel�zquez

	Unreal Editor addon for path network generation.
	Can also generate paths inside the game (single Navigation Point)
=============================================================================*/

#ifndef INC_PATHBUILDER
#define INC_PATHBUILDER

class ENGINE_API FPathBuilder
{
	friend class FPathBuilderMaster;

public:
	ULevel*                  Level;
	APawn*                   Scout;

private:
	void getScout();
	int32 findScoutStart(FVector start);
};


enum EPathBuilderFlags
{
	PB_BuildAir      = 0x01,
	PB_BuildSelected = 0x02,
	PB_FastPrune     = 0x04,
};

class XC_CORE_API FPathBuilderMaster : public FPathBuilder
{
public:
	float                    GoodDistance; //Up to 2x during lookup
	float                    GoodHeight;
	float                    GoodRadius;
	float                    GoodJumpZ;
	float                    GoodGroundSpeed;
	uint32                   BuildFlags;
	UClass*                  InventorySpotClass;
	UClass*                  WarpZoneMarkerClass;
	int32                    TotalCandidates;
	FString                  BuildResult;

	FPathBuilderMaster();
	void RebuildPaths();

	void Setup();
	void AutoDefine( ANavigationPoint* NewPoint, AActor* AdjustTo=0);

private:
	void DefinePaths();
	void UndefinePaths();

	void AddMarkers();
	void DefineSpecials();
	void BuildCandidatesLists();
	void ProcessCandidatesLists();

	void HandleInventory( AInventory* Inv);
	void HandleWarpZone( AWarpZoneInfo* Info);
	void AdjustToActor( ANavigationPoint* N, AActor* Actor);

	void DefineFor( ANavigationPoint* A, ANavigationPoint* B);
	FReachSpec CreateSpec( ANavigationPoint* Start, ANavigationPoint* End);
	int AttachReachSpec( const FReachSpec& Spec, int32 bPrune=0);
	int32 ValidPaths( int32* Paths);

	void GetScout();
	int FindStart( FVector V);
};

#endif