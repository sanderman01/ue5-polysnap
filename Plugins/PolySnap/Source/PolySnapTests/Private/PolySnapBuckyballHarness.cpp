// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "Components/MeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "PolySnap.h"
#include "PolySnapBuckyballFixture.h"
#include "PolySnapConnectorComponent.h"
#include "PolySnapSnapQuery.h"
#include "PolySnapSubsystem.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

/**
 * The Milestone 2 test rig: assembles a closed buckyball out of pentagon and hexagon parts and
 * reports how well it closed.
 *
 * It stands in for a player's hands and does nothing else. For each face it decides which part
 * class to spawn and roughly where to hold it, and then hands the part to the ordinary
 * FPolySnapSnapQuery::FindBest and UPolySnapSubsystem::CommitPlacement -- the same two calls the
 * builder component makes when the player presses the place key. Every transform in the finished
 * shell is therefore PolySnap's own answer, and the residuals measure the plugin rather than this
 * file.
 *
 *     PolySnap.Test.Buckyball kinematic   assemble and measure; nothing simulates
 *     PolySnap.Test.Buckyball anchored    then constrain and simulate, seed pinned
 *     PolySnap.Test.Buckyball free        then constrain and simulate, nothing pinned
 *     PolySnap.Test.ClearBuckyball        destroy what the last run spawned
 *
 * The part classes are the project's, not the plugin's, so they are named by console variable
 * rather than referenced: PolySnap.Test.PentagonClass and PolySnap.Test.HexagonClass, or as
 * trailing arguments to the command.
 */
namespace PolySnapBuckyballHarness
{
/** Unreal units are centimetres; every measurement printed here is a real distance, so millimetres. */
constexpr double UuToMm = 10.0;

/** Tag on every actor this file spawns, so ClearBuckyball can find them again. */
const FName SpawnedTag(TEXT("PolySnapTestPart"));

/** How far out to gather candidate sockets, as a multiple of the edge length. */
constexpr double GatherRadiusInEdges = 3.0;

static TAutoConsoleVariable<float> CVarClearanceUu(TEXT("PolySnap.Test.ClearanceUu"), 200.0f,
	TEXT("How much room to leave between the assembled shell and whatever is already in the level."), ECVF_Default);

static TAutoConsoleVariable<int32> CVarPartCollision(TEXT("PolySnap.Test.PartCollision"), 1,
	TEXT("Whether the buckyball test leaves collision on between its parts. Off isolates the ")
		TEXT("constraint network from what the panels' collision hulls are doing at the seams."), ECVF_Default);

static TAutoConsoleVariable<float> CVarSettleSeconds(TEXT("PolySnap.Test.SettleSeconds"), 5.0f,
	TEXT("How long a simulated buckyball is left to settle before the test reports on it again."), ECVF_Default);

static TAutoConsoleVariable<FString> CVarPentagonClass(TEXT("PolySnap.Test.PentagonClass"),
	TEXT(""), TEXT("Actor class of the pentagon part the buckyball test spawns, e.g. ")
				  TEXT("/Game/Parts/BP_Wall_Pentagon_10_200."), ECVF_Default);

static TAutoConsoleVariable<FString> CVarHexagonClass(TEXT("PolySnap.Test.HexagonClass"),
	TEXT(""), TEXT("Actor class of the hexagon part the buckyball test spawns, e.g. ")
				  TEXT("/Game/Parts/BP_Wall_Hexagon_10_200."), ECVF_Default);

/** What happens to the assembly once it has been built and measured. */
enum class EAssemblyMode : uint8
{
	/** Nothing simulates and no constraint is built. The measurement run -- see DESIGN section 5. */
	Kinematic,

	/** Constrain and simulate, with the seed part pinned so the shell holds its position. */
	Anchored,

	/** Constrain and simulate with nothing pinned, so the whole shell drifts under its own strain. */
	Free
};

[[nodiscard]] bool ParseMode(const FString& Argument, EAssemblyMode& OutMode)
{
	if (Argument.Equals(TEXT("kinematic"), ESearchCase::IgnoreCase))
	{
		OutMode = EAssemblyMode::Kinematic;
		return true;
	}

	if (Argument.Equals(TEXT("anchored"), ESearchCase::IgnoreCase))
	{
		OutMode = EAssemblyMode::Anchored;
		return true;
	}

	if (Argument.Equals(TEXT("free"), ESearchCase::IgnoreCase))
	{
		OutMode = EAssemblyMode::Free;
		return true;
	}

	return false;
}

/**
 * Resolves a class path, accepting the short form the content browser copies.
 *
 * "/Game/Parts/BP_Wall_Pentagon_10_200" is what a right-click gives you; the class inside that
 * package is "BP_Wall_Pentagon_10_200_C". Appending it here saves the caller knowing that.
 */
[[nodiscard]] UClass* ResolvePartClass(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return nullptr;
	}

	FString ClassPath = Path;
	if (!ClassPath.Contains(TEXT(".")))
	{
		ClassPath = FString::Printf(TEXT("%s.%s_C"), *ClassPath, *FPaths::GetCleanFilename(ClassPath));
	}

	return LoadClass<AActor>(nullptr, *ClassPath);
}

/** Spawns one part at a pose, tagged, kinematic, and anchored or not. */
[[nodiscard]] UPolySnapConnectorComponent* SpawnPart(UWorld& World, UClass& PartClass, const FTransform& Pose,
	bool bAnchored)
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Not deferred, though anchoring is a BeginPlay-time decision: a Blueprint's components are
	// built by its construction script, which deferred spawning postpones to FinishSpawning, so
	// there would be no connector to configure yet. Setting it afterwards reaches the same state,
	// because the part is put back into a kinematic body here in either case.
	AActor* Actor = World.SpawnActor<AActor>(&PartClass, Pose, Parameters);
	if (Actor == nullptr)
	{
		return nullptr;
	}

	Actor->Tags.Add(SpawnedTag);

	UPolySnapConnectorComponent* Connector = Actor->FindComponentByClass<UPolySnapConnectorComponent>();
	if (Connector == nullptr)
	{
		Actor->Destroy();
		return nullptr;
	}

	Connector->bStartAnchored = bAnchored;

	// Whatever the Blueprint asked for, the assembly is built kinematic and only handed to the
	// simulation once it has been measured.
	Connector->SetSimulating(false);

	return Connector;
}

/**
 * A part's socket positions in its own space, in ring order, with the panel's plane normal.
 *
 * Nothing about the mesh is assumed beyond what CONVENTIONS requires of it: the sockets sit at the
 * midpoints of the edges of a flat regular polygon, so they form a ring about the panel's centre
 * and that ring defines the plane.
 */
[[nodiscard]] bool GatherSocketRing(const UPolySnapConnectorComponent& Part, TArray<FVector>& OutRing,
	FVector& OutNormal)
{
	const FTransform PartTransform = Part.GetPartTransform();

	OutRing.Reset();
	for (const FPolySnapSocketDescriptor& Descriptor : Part.GetSocketDescriptors())
	{
		OutRing.Add(
			PartTransform.InverseTransformPosition(Part.GetSocketWorldTransform(Descriptor.SocketName).GetLocation()));
	}

	if (OutRing.Num() < 3)
	{
		return false;
	}

	FVector Centre = FVector::ZeroVector;
	for (const FVector& Position : OutRing)
	{
		Centre += Position;
	}
	Centre /= OutRing.Num();

	// The second spoke is chosen for being least parallel to the first rather than for being next
	// in the list: on a hexagon, socket 004 is directly opposite socket 001 and their cross product
	// is zero.
	const FVector FirstSpoke = OutRing[0] - Centre;
	FVector BestNormal = FVector::ZeroVector;

	for (int32 Index = 1; Index < OutRing.Num(); ++Index)
	{
		const FVector Candidate = FVector::CrossProduct(FirstSpoke, OutRing[Index] - Centre);
		if (Candidate.SizeSquared() > BestNormal.SizeSquared())
		{
			BestNormal = Candidate;
		}
	}

	OutNormal = BestNormal.GetSafeNormal();
	if (OutNormal.IsNearlyZero())
	{
		return false;
	}

	const FVector AxisX = FirstSpoke.GetSafeNormal();
	const FVector AxisY = FVector::CrossProduct(OutNormal, AxisX);

	OutRing.Sort(
		[Centre, AxisX, AxisY](const FVector& A, const FVector& B)
		{
			return FMath::Atan2((A - Centre) | AxisY, (A - Centre) | AxisX)
				 < FMath::Atan2((B - Centre) | AxisY, (B - Centre) | AxisX);
		});

	return true;
}

/** The edge length a ring of socket positions implies: adjacent midpoints are a cos(pi/n) apart. */
[[nodiscard]] double EdgeLengthFromRing(const TArray<FVector>& Ring)
{
	return FVector::Dist(Ring[0], Ring[1]) / FMath::Cos(UE_DOUBLE_PI / Ring.Num());
}

/** An orthonormal frame from two spokes of a planar ring, as the rows of a rotation matrix. */
[[nodiscard]] FMatrix FrameFromSpokes(const FVector& First, const FVector& Second)
{
	const FVector AxisX = First.GetSafeNormal();
	const FVector AxisY = (Second - (Second | AxisX) * AxisX).GetSafeNormal();

	return FMatrix(AxisX, AxisY, FVector::CrossProduct(AxisX, AxisY), FVector::ZeroVector);
}

/**
 * Where to hold a part so that its sockets sit on one face's edges.
 *
 * A least-squares fit of the part's socket ring onto the face's edge midpoints, over every
 * rotational offset and both windings, keeping the one that fits best. Nothing about the mesh's
 * authoring is assumed -- not which socket is which edge, not which way its ring is wound, not
 * where its origin sits -- so this works on any panel that is a flat regular polygon.
 *
 * Candidates that would turn the panel over are discarded rather than scored: they fit exactly as
 * well by symmetry, and letting the tie fall either way would give a shell whose panels faced
 * alternately in and out.
 *
 * @param OutRmsUu How far the fit missed by. A number well above float noise means the panel is
 *                 not the regular polygon the shell needs, which is worth knowing before the
 *                 residuals get blamed on the snapper.
 */
[[nodiscard]] bool FitPartToFace(const TArray<FVector>& LocalRing, const FVector& LocalNormal,
	const TArray<FVector>& Midpoints, const FVector& FaceNormal, FTransform& OutPose, double& OutRmsUu)
{
	if (LocalRing.Num() != Midpoints.Num())
	{
		return false;
	}

	const int32 Count = LocalRing.Num();

	FVector LocalCentre = FVector::ZeroVector;
	FVector FaceCentre = FVector::ZeroVector;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		LocalCentre += LocalRing[Index];
		FaceCentre += Midpoints[Index];
	}
	LocalCentre /= Count;
	FaceCentre /= Count;

	const FMatrix LocalFrame = FrameFromSpokes(LocalRing[0] - LocalCentre, LocalRing[1] - LocalCentre);

	bool bFound = false;
	OutRmsUu = TNumericLimits<double>::Max();

	for (int32 Offset = 0; Offset < Count; ++Offset)
	{
		for (const int32 Winding : {1, -1})
		{
			const auto Corresponding = [Count, Offset, Winding](int32 Index)
			{
				return (Offset + ((Winding * Index) % Count) + Count) % Count;
			};

			const FMatrix FaceFrame =
				FrameFromSpokes(Midpoints[Corresponding(0)] - FaceCentre, Midpoints[Corresponding(1)] - FaceCentre);

			FTransform Pose(LocalFrame.GetTransposed() * FaceFrame);
			Pose.SetTranslation(FaceCentre - Pose.TransformVector(LocalCentre));

			if ((Pose.TransformVector(LocalNormal) | FaceNormal) <= 0.0)
			{
				continue;
			}

			double SumSquared = 0.0;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				SumSquared +=
					FVector::DistSquared(Pose.TransformPosition(LocalRing[Index]), Midpoints[Corresponding(Index)]);
			}

			const double RmsUu = FMath::Sqrt(SumSquared / Count);
			if (RmsUu < OutRmsUu)
			{
				OutRmsUu = RmsUu;
				OutPose = Pose;
				bFound = true;
			}
		}
	}

	return bFound;
}

/** The mean position of the parts still alive, which is where the shell has drifted to. */
[[nodiscard]] FVector ShellCentre(const TArray<UPolySnapConnectorComponent*>& Parts)
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;

	for (const UPolySnapConnectorComponent* Part : Parts)
	{
		if (IsValid(Part))
		{
			Sum += Part->GetPartTransform().GetLocation();
			++Count;
		}
	}

	return Count > 0 ? Sum / Count : FVector::ZeroVector;
}

/** How many of the parts are still alive, so a shell that tore itself apart says so. */
[[nodiscard]] int32 SurvivingCount(const TArray<UPolySnapConnectorComponent*>& Parts)
{
	int32 Count = 0;
	for (const UPolySnapConnectorComponent* Part : Parts)
	{
		Count += IsValid(Part) ? 1 : 0;
	}

	return Count;
}

void ClearBuckyball(const TArray<FString>& Args, UWorld* World)
{
	if (World == nullptr)
	{
		return;
	}

	int32 Destroyed = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->Tags.Contains(SpawnedTag))
		{
			It->Destroy();
			++Destroyed;
		}
	}

	UE_LOG(LogPolySnap, Display, TEXT("PolySnap.Test.ClearBuckyball: destroyed %d part(s)."), Destroyed);
}

/**
 * Where to build: in front of the player and clear of the floor.
 *
 * Not a cosmetic choice. A shell built at the world origin is built inside whatever the level put
 * there, and thirty-two rigid bodies that start simulating already interpenetrating the floor are
 * flung apart by depenetration -- which looks exactly like a constraint network that cannot hold.
 */
[[nodiscard]] FVector ChooseShellCentre(UWorld& World, double CircumradiusUu)
{
	const double ClearanceUu = FMath::Max(CVarClearanceUu.GetValueOnGameThread(), 0.0f);

	if (const APlayerController* Controller = World.GetFirstPlayerController())
	{
		if (const APawn* Pawn = Controller->GetPawn())
		{
			FVector ViewLocation = FVector::ZeroVector;
			FRotator ViewRotation = FRotator::ZeroRotator;
			Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);

			// Far enough ahead to see the whole thing, and high enough to clear the ground the
			// player is standing on.
			return Pawn->GetActorLocation() + ViewRotation.Vector().GetSafeNormal2D() * (3.0 * CircumradiusUu)
				 + FVector::UpVector * (CircumradiusUu + ClearanceUu);
		}
	}

	return FVector::UpVector * (CircumradiusUu + ClearanceUu);
}

void SpawnBuckyball(const TArray<FString>& Args, UWorld* World)
{
	if (World == nullptr)
	{
		UE_LOG(LogPolySnap, Warning, TEXT("PolySnap.Test.Buckyball: no world."));
		return;
	}

	UPolySnapSubsystem* Subsystem = World->GetSubsystem<UPolySnapSubsystem>();
	if (Subsystem == nullptr)
	{
		UE_LOG(LogPolySnap, Warning, TEXT("PolySnap.Test.Buckyball: no PolySnap subsystem in this world."));
		return;
	}

	EAssemblyMode Mode = EAssemblyMode::Kinematic;
	if (Args.Num() > 0 && !ParseMode(Args[0], Mode))
	{
		UE_LOG(LogPolySnap, Warning,
			TEXT("PolySnap.Test.Buckyball: '%s' is not kinematic, anchored or free."), *Args[0]);
		return;
	}

	UClass* PentagonClass = ResolvePartClass(Args.Num() > 1 ? Args[1] : CVarPentagonClass.GetValueOnGameThread());
	UClass* HexagonClass = ResolvePartClass(Args.Num() > 2 ? Args[2] : CVarHexagonClass.GetValueOnGameThread());

	if (PentagonClass == nullptr || HexagonClass == nullptr)
	{
		UE_LOG(LogPolySnap, Warning,
			TEXT("PolySnap.Test.Buckyball: set PolySnap.Test.PentagonClass and PolySnap.Test.HexagonClass, or ")
				TEXT("pass both class paths after the mode."));
		return;
	}

	// The seed goes in first, at the origin, so its sockets can be measured: the shell is built to
	// whatever edge length the parts actually have rather than to the number in their asset names.
	UPolySnapConnectorComponent* Seed =
		SpawnPart(*World, *PentagonClass, FTransform::Identity, Mode == EAssemblyMode::Anchored);

	TArray<FVector> PentagonRing;
	FVector PentagonNormal = FVector::ZeroVector;

	if (Seed == nullptr || !GatherSocketRing(*Seed, PentagonRing, PentagonNormal))
	{
		// Most often a part whose mesh reference did not resolve: it spawns, it has a connector, and
		// it has no sockets at all. Saying which is which here saves guessing at it from a shell
		// that refused to start.
		UE_LOG(LogPolySnap, Warning,
			TEXT("PolySnap.Test.Buckyball: '%s' has no usable socket ring -- %d edge socket(s) on mesh '%s'."),
				*PentagonClass->GetName(), Seed != nullptr ? Seed->GetSocketDescriptors().Num() : -1,
				*GetNameSafe(Seed != nullptr ? Seed->GetResolvedSocketMesh() : nullptr));
		return;
	}

	const double EdgeLengthUu = EdgeLengthFromRing(PentagonRing);
	const FPolySnapBuckyballFixture Fixture(EdgeLengthUu);

	// The fixture is centred on its own origin; the shell is built wherever there is room for it.
	double CircumradiusUu = 0.0;
	for (const FVector& Vertex : Fixture.GetVertices())
	{
		CircumradiusUu = FMath::Max(CircumradiusUu, Vertex.Size());
	}

	const FVector ShellCentreWorld = ChooseShellCentre(*World, CircumradiusUu);

	UE_LOG(LogPolySnap, Display,
		TEXT("PolySnap.Test.Buckyball: %s, edge %.3f cm measured off %s, %d faces, centred on %s."),
			Mode == EAssemblyMode::Kinematic ? TEXT("kinematic")
											 : (Mode == EAssemblyMode::Anchored ? TEXT("anchored") : TEXT("free")),
												   EdgeLengthUu, *PentagonClass->GetName(), Fixture.GetFaces().Num(),
												   *ShellCentreWorld.ToCompactString());

	TArray<FVector> HexagonRing;
	FVector HexagonNormal = FVector::ZeroVector;
	TArray<UPolySnapConnectorComponent*> Placed;

	const TArray<int32> Order = Fixture.BuildOrder();
	const FPolySnapQueryTolerances Tolerances = UPolySnapSubsystem::GetQueryTolerances();

	double WorstFitRmsUu = 0.0;
	double WorstDeviationUu = 0.0;
	double WorstDeviationDegrees = 0.0;

	for (int32 Step = 0; Step < Order.Num(); ++Step)
	{
		const int32 FaceIndex = Order[Step];
		const FPolySnapFixtureFace& Face = Fixture.GetFaces()[FaceIndex];
		const bool bIsPentagon = Face.Kind == EPolySnapFixtureFaceKind::Pentagon;

		UPolySnapConnectorComponent* Part =
			Step == 0 ? Seed
					  : SpawnPart(*World, bIsPentagon ? *PentagonClass : *HexagonClass, FTransform::Identity, false);

		if (Part == nullptr)
		{
			UE_LOG(LogPolySnap, Warning,
				TEXT("PolySnap.Test.Buckyball: could not spawn the part for face %d."), FaceIndex);
			return;
		}

		// Measured once per part type, off the first one of each spawned.
		TArray<FVector>& Ring = bIsPentagon ? PentagonRing : HexagonRing;
		FVector& Normal = bIsPentagon ? PentagonNormal : HexagonNormal;

		if (Ring.IsEmpty())
		{
			if (!GatherSocketRing(*Part, Ring, Normal))
			{
				UE_LOG(LogPolySnap, Warning,
					TEXT("PolySnap.Test.Buckyball: the hexagon part has no usable socket ring."));
				return;
			}

			// The two part types must agree about the edge length or no shell of them can close.
			// Worth saying out loud here rather than leaving it to be inferred from the residuals.
			const double MeasuredUu = EdgeLengthFromRing(Ring);
			if (!FMath::IsNearlyEqual(MeasuredUu, EdgeLengthUu, 1.0e-2))
			{
				UE_LOG(LogPolySnap, Warning,
					TEXT("PolySnap.Test.Buckyball: %s measures %.3f cm to the pentagon's %.3f cm."),
						*Part->GetOwner()->GetClass()->GetName(), MeasuredUu, EdgeLengthUu);
			}
		}

		FTransform IdealPose;
		double FitRmsUu = 0.0;

		TArray<FVector> Midpoints = Fixture.EdgeMidpoints(FaceIndex);
		for (FVector& Midpoint : Midpoints)
		{
			Midpoint += ShellCentreWorld;
		}

		if (!FitPartToFace(Ring, Normal, Midpoints, Face.Normal, IdealPose, FitRmsUu))
		{
			UE_LOG(LogPolySnap, Warning,
				TEXT("PolySnap.Test.Buckyball: face %d has %d edges and the part has %d ")
					TEXT("sockets."), FaceIndex, Face.VertexIndices.Num(), Ring.Num());
			return;
		}

		WorstFitRmsUu = FMath::Max(WorstFitRmsUu, FitRmsUu);

		// The ideal pose is only what the player's hands would have supplied. Everything from here
		// on is PolySnap's answer: which pair anchors, which way round the part goes, what angle it
		// folds to, and which other seams close as a result.
		Part->GetOwner()->SetActorTransform(IdealPose, false, nullptr, ETeleportType::TeleportPhysics);

		if (Step > 0)
		{
			TArray<FPolySnapWorldSocket> HeldSockets;
			Part->AppendWorldSockets(HeldSockets);

			TArray<FPolySnapWorldSocket> TargetSockets;
			Subsystem->GatherWorldSockets(Part, IdealPose.GetLocation(), GatherRadiusInEdges * EdgeLengthUu,
				TargetSockets);

			const FPolySnapCandidate Candidate =
				FPolySnapSnapQuery::FindBest(HeldSockets, TargetSockets, IdealPose, Tolerances);

			if (!Candidate.IsSet())
			{
				UE_LOG(LogPolySnap, Error,
					TEXT("PolySnap.Test.Buckyball: face %d (step %d) found nothing to snap to. Stopping with %d ")
						TEXT("parts placed."), FaceIndex, Step, Placed.Num());
				return;
			}

			Part->GetOwner()->SetActorTransform(Candidate.SolvedPartTransform, false, nullptr,
				ETeleportType::TeleportPhysics);

			Subsystem->CommitPlacement(Candidate, /*bCreateConstraints=*/false);

			// How far PolySnap's answer drifted from the ideal shell. It grows with distance from
			// the seed if error is accumulating, and stays flat if the panels are simply mis-cut --
			// which is what separates the two explanations for a large residual.
			const FTransform Actual = Part->GetPartTransform();
			WorstDeviationUu =
				FMath::Max(WorstDeviationUu, FVector::Dist(Actual.GetLocation(), IdealPose.GetLocation()));
			WorstDeviationDegrees = FMath::Max(WorstDeviationDegrees,
				Actual.GetRotation().AngularDistance(IdealPose.GetRotation()) * (180.0 / UE_DOUBLE_PI));
		}

		Placed.Add(Part);
	}

	// The subsystem reports on the whole world, which may hold parts that were in the level before
	// this ran. Whether the SHELL sealed is a question about the parts this spawned and no others.
	int32 ShellSockets = 0;
	int32 ShellOpenSockets = 0;
	for (const UPolySnapConnectorComponent* Part : Placed)
	{
		for (const FPolySnapSocketDescriptor& Descriptor : Part->GetSocketDescriptors())
		{
			++ShellSockets;
			ShellOpenSockets += Part->IsSocketConnected(Descriptor.Id) ? 0 : 1;
		}
	}

	UE_LOG(LogPolySnap, Display, TEXT("%s"), *Subsystem->BuildAssemblyReport());
	UE_LOG(LogPolySnap, Display,
		TEXT("  shell: %d part(s), %d socket(s), %d open -- %s"), Placed.Num(), ShellSockets, ShellOpenSockets,
			ShellOpenSockets == 0 ? TEXT("SEALED") : TEXT("NOT SEALED"));
	UE_LOG(LogPolySnap, Display,
		TEXT("  worst deviation from the ideal shell: %.4f mm, %.4f deg   (panel fit %.4f mm)"),
			WorstDeviationUu * UuToMm, WorstDeviationDegrees, WorstFitRmsUu * UuToMm);

	if (Mode == EAssemblyMode::Kinematic)
	{
		return;
	}

	// Measured first, simulated second, and in that order for every mode -- so the three runs
	// report numbers that mean the same thing. A constraint solver leaves bodies slightly off
	// their ideal positions, and that strain would otherwise be indistinguishable from panel
	// error (DESIGN section 5).
	const bool bPartCollision = CVarPartCollision.GetValueOnGameThread() != 0;

	for (UPolySnapConnectorComponent* Part : Placed)
	{
		if (!bPartCollision)
		{
			if (UMeshComponent* Mesh = Part->GetResolvedSocketMesh())
			{
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}

		Part->SetSimulating(true);
	}

	const int32 Constraints = Subsystem->CreateConstraintsForAllConnections();

	UE_LOG(LogPolySnap, Display,
		TEXT("  handed %d part(s) to the simulation across %d constraint(s)%s."), Placed.Num(), Constraints,
			Mode == EAssemblyMode::Anchored ? TEXT(", seed anchored") : TEXT(", nothing anchored"));

	// One more reading once the solver has had time to work. The recorded residuals do not change --
	// they are what the seams measured at placement -- so what this shows is the strain the
	// constraints ended up holding, and whether the shell held together at all.
	const FVector CentreAtRest = ShellCentre(Placed);

	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[WeakWorld = TWeakObjectPtr<UWorld>(World), Placed, CentreAtRest](float)
			{
				UWorld* TickWorld = WeakWorld.Get();
				UPolySnapSubsystem* TickSubsystem =
					TickWorld != nullptr ? TickWorld->GetSubsystem<UPolySnapSubsystem>() : nullptr;

				if (TickSubsystem != nullptr)
				{
					UE_LOG(LogPolySnap, Display, TEXT("after settling: %s"), *TickSubsystem->BuildAssemblyReport());
					UE_LOG(LogPolySnap, Display,
						TEXT("  shell centre moved %.2f cm, %d of %d part(s) survive."),
							FVector::Dist(ShellCentre(Placed), CentreAtRest), SurvivingCount(Placed), Placed.Num());
				}

				return false;
			}),
		FMath::Max(CVarSettleSeconds.GetValueOnGameThread(), 0.0f));
}

static FAutoConsoleCommandWithWorldAndArgs CmdBuckyball(TEXT("PolySnap.Test.Buckyball"),
	TEXT("PolySnap.Test.Buckyball <kinematic|anchored|free> [pentagonClass] [hexagonClass]. Assembles a ")
		TEXT("truncated icosahedron through the ordinary snap path and reports how well it closed."),
			FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnBuckyball));

static FAutoConsoleCommandWithWorldAndArgs CmdClearBuckyball(TEXT("PolySnap.Test.ClearBuckyball"),
	TEXT("PolySnap.Test.ClearBuckyball. Destroys every part the buckyball test spawned."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ClearBuckyball));
} // namespace PolySnapBuckyballHarness

#endif // WITH_DEV_AUTOMATION_TESTS
