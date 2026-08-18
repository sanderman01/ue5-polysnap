// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "Misc/AutomationTest.h"
#include "PolySnapSocketName.h"
#include "PolySnapTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FPolySnapSocketNameSpec, "PolySnap.SocketName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

FPolySnapSocketDescriptor Descriptor;
FString Error;

/**
 * Parses into the shared Descriptor and reports the outcome by name.
 *
 * A string rather than the enum, because FAutomationTestBase::TestEqual has no overload for an
 * enum class and a failure that reads "expected Parsed, got Malformed" is worth the conversion.
 */
FString ParseName(const TCHAR* Name)
{
	Descriptor = FPolySnapSocketDescriptor();
	Error.Reset();

	switch (FPolySnapSocketName::Parse(FStringView(Name), Descriptor, &Error))
	{
		case EPolySnapParseResult::Parsed:
			return TEXT("Parsed");
		case EPolySnapParseResult::NotPolySnap:
			return TEXT("NotPolySnap");
		case EPolySnapParseResult::Malformed:
			return TEXT("Malformed");
		default:
			return TEXT("Unknown");
	}
}

END_DEFINE_SPEC(FPolySnapSocketNameSpec)

void FPolySnapSocketNameSpec::Define()
{
	Describe("a well-formed name",
		[this]()
		{
			It("parses every field",
				[this]()
				{
					TestEqual("result", ParseName(TEXT("Edge_001_Straight_2000")), TEXT("Parsed"));
					TestEqual("id", Descriptor.Id, 1);
					TestEqual("subtype", FPolySnapSocketName::SubTypeToString(Descriptor.SubType), TEXT("Straight"));
					TestEqual("size", Descriptor.SizeMillimetres, 2000);
				});

			It("accepts an unpadded size of any width",
				[this]()
				{
					TestEqual("short", ParseName(TEXT("Edge_001_Straight_500")), TEXT("Parsed"));
					TestEqual("size", Descriptor.SizeMillimetres, 500);

					TestEqual("long", ParseName(TEXT("Edge_999_Straight_12000")), TEXT("Parsed"));
					TestEqual("id", Descriptor.Id, 999);
					TestEqual("size", Descriptor.SizeMillimetres, 12000);
				});
		});

	Describe("the authoring tail",
		[this]()
		{
			It("is ignored, so a tailed and an untailed name are the same socket",
				[this]()
				{
					TestEqual("result", ParseName(TEXT("Edge_001_Straight_2000.Pent")), TEXT("Parsed"));
					TestEqual("id", Descriptor.Id, 1);
					TestEqual("size", Descriptor.SizeMillimetres, 2000);
				});

			It("strips Blender's numeric duplicate suffix rather than rejecting it",
				[this]()
				{
					// The suffix is a symptom; the duplicated ID it implies is the error, and that is
					// detected separately, per piece, on stripped names.
					TestEqual("result", ParseName(TEXT("Edge_003_Straight_2000.001")), TEXT("Parsed"));
					TestEqual("id", Descriptor.Id, 3);
				});

			It("starts at the first dot, so a second dot is still tail",
				[this]()
				{
					TestEqual("result", ParseName(TEXT("Edge_001_Straight_2000.Pent.001")), TEXT("Parsed"));
					TestEqual("id", Descriptor.Id, 1);
				});

			It("splits into head and tail",
				[this]()
				{
					FStringView Head;
					FStringView Tail;
					FPolySnapSocketName::SplitAuthoringTail(TEXT("Edge_001_Straight_2000.Pent"), Head, Tail);

					TestEqual("head", FString(Head), TEXT("Edge_001_Straight_2000"));
					TestEqual("tail", FString(Tail), TEXT("Pent"));
				});
		});

	Describe("a socket belonging to another system",
		[this]()
		{
			It("is passed over in silence",
				[this]()
				{
					TestEqual("mount", ParseName(TEXT("Mount_A")), TEXT("NotPolySnap"));
					TestEqual("no error", Error.Len(), 0);
				});

			It("is recognised by the tag and its separator, not by a prefix match",
				[this]()
				{
					TestEqual("no separator", ParseName(TEXT("EdgeGuard_001")), TEXT("NotPolySnap"));
					TestEqual("tag alone", ParseName(TEXT("Edge")), TEXT("NotPolySnap"));
				});
		});

	Describe("a malformed PolySnap name",
		[this]()
		{
			It("is a loud error rather than a foreign socket",
				[this]()
				{
					// The whole point of the Edge_ tag: this is a typo, and silence would hide it.
					TestEqual("result", ParseName(TEXT("Edge_01_Straight_2000")), TEXT("Malformed"));
					TestTrue("explains itself", Error.Len() > 0);
				});

			It("rejects an ID that is not exactly three digits",
				[this]()
				{
					TestEqual("too short", ParseName(TEXT("Edge_1_Straight_2000")), TEXT("Malformed"));
					TestEqual("too long", ParseName(TEXT("Edge_0001_Straight_2000")), TEXT("Malformed"));
					TestEqual("not a number", ParseName(TEXT("Edge_abc_Straight_2000")), TEXT("Malformed"));
				});

			It("rejects ID zero, which is not an identity",
				[this]() { TestEqual("result", ParseName(TEXT("Edge_000_Straight_2000")), TEXT("Malformed")); });

			It("rejects a subtype that is not in scope",
				[this]() { TestEqual("result", ParseName(TEXT("Edge_001_Curved_2000")), TEXT("Malformed")); });

			It("rejects a zero-padded size, because one size means one spelling",
				[this]() { TestEqual("result", ParseName(TEXT("Edge_001_Straight_0500")), TEXT("Malformed")); });

			It("rejects the wrong number of fields",
				[this]()
				{
					TestEqual("too few", ParseName(TEXT("Edge_001_Straight")), TEXT("Malformed"));
					TestEqual("too many", ParseName(TEXT("Edge_001_Straight_2000_Extra")), TEXT("Malformed"));
					TestEqual("empty field", ParseName(TEXT("Edge_001__2000")), TEXT("Malformed"));
				});
		});

	Describe("compatibility",
		[this]()
		{
			It("matches on subtype and size",
				[this]()
				{
					FPolySnapSocketDescriptor A;
					TestTrue("A parses", FPolySnapSocketName::Parse(FStringView(TEXT("Edge_001_Straight_2000")), A)
											 == EPolySnapParseResult::Parsed);

					FPolySnapSocketDescriptor B;
					TestTrue("B parses", FPolySnapSocketName::Parse(FStringView(TEXT("Edge_007_Straight_2000")), B)
											 == EPolySnapParseResult::Parsed);

					// Different IDs, and they still mate: the ID is identity, not classification.
					TestTrue("same size mates", A.IsCompatibleWith(B));
				});

			It("refuses a different size",
				[this]()
				{
					FPolySnapSocketDescriptor A;
					TestTrue("A parses", FPolySnapSocketName::Parse(FStringView(TEXT("Edge_001_Straight_2000")), A)
											 == EPolySnapParseResult::Parsed);

					FPolySnapSocketDescriptor B;
					TestTrue("B parses", FPolySnapSocketName::Parse(FStringView(TEXT("Edge_001_Straight_3464")), B)
											 == EPolySnapParseResult::Parsed);

					TestFalse("different size does not mate", A.IsCompatibleWith(B));
				});

			It("refuses an unparsed descriptor",
				[this]()
				{
					const FPolySnapSocketDescriptor Empty;
					TestFalse("default matches nothing", Empty.IsCompatibleWith(Empty));
				});
		});
}

#endif // WITH_DEV_AUTOMATION_TESTS
