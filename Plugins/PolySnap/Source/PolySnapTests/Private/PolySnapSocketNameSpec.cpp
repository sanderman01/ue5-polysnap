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
					TestEqual("result", ParseName(TEXT("Edge_001_Straight_40_2000")), TEXT("Parsed"));
					TestEqual("id", Descriptor.Id, 1);
					TestEqual("subtype", FPolySnapSocketName::SubTypeToString(Descriptor.SubType), TEXT("Straight"));
					TestEqual("length", Descriptor.LengthMillimetres, 2000);
					TestEqual("thickness", Descriptor.ThicknessMillimetres, 40);
				});

			It("accepts an unpadded size of any width, in either size field",
				[this]()
				{
					TestEqual("short", ParseName(TEXT("Edge_001_Straight_6_500")), TEXT("Parsed"));
					TestEqual("length", Descriptor.LengthMillimetres, 500);
					TestEqual("thickness", Descriptor.ThicknessMillimetres, 6);

					TestEqual("long", ParseName(TEXT("Edge_999_Straight_250_12000")), TEXT("Parsed"));
					TestEqual("id", Descriptor.Id, 999);
					TestEqual("length", Descriptor.LengthMillimetres, 12000);
					TestEqual("thickness", Descriptor.ThicknessMillimetres, 250);
				});
		});

	Describe("the authoring tail",
		[this]()
		{
			// Every name here is in the imported spelling. Unreal rewrites the '.' the grammar
			// reserves to '_', so a UStaticMesh only ever carries a sixth underscore-separated
			// field and the head's fixed arity is the whole of the rule.
			It("is ignored, so a tailed and an untailed name are the same socket",
				[this]()
				{
					TestEqual("result", ParseName(TEXT("Edge_004_Straight_40_2000_Pent")), TEXT("Parsed"));
					TestEqual("id", Descriptor.Id, 4);
					TestEqual("length", Descriptor.LengthMillimetres, 2000);
					TestEqual("thickness", Descriptor.ThicknessMillimetres, 40);
				});

			It("strips Blender's numeric duplicate suffix rather than rejecting it",
				[this]()
				{
					// The suffix is a symptom; the duplicated ID it implies is the error, and that is
					// detected separately, per piece, on stripped names. The validator's numeric-tail
					// warning reads this same split, so it fires on the imported _001 too.
					TestEqual("result", ParseName(TEXT("Edge_003_Straight_40_2000_001")), TEXT("Parsed"));
					TestEqual("id", Descriptor.Id, 3);
				});

			It("splits into head and tail",
				[this]()
				{
					FStringView Head;
					FStringView Tail;
					FPolySnapSocketName::SplitAuthoringTail(TEXT("Edge_004_Straight_40_2000_Pent"), Head, Tail);

					TestEqual("head", FString(Head), TEXT("Edge_004_Straight_40_2000"));
					TestEqual("tail", FString(Tail), TEXT("Pent"));
				});

			It("keeps a multi-part tail together",
				[this]()
				{
					// SOCKET_Edge_001_Straight_40_2000.Pent.001 with both dots rewritten. The tail is
					// everything past field five, however many underscores it contains.
					FStringView Head;
					FStringView Tail;
					FPolySnapSocketName::SplitAuthoringTail(TEXT("Edge_001_Straight_40_2000_Pent_001"), Head, Tail);

					TestEqual("head", FString(Head), TEXT("Edge_001_Straight_40_2000"));
					TestEqual("tail", FString(Tail), TEXT("Pent_001"));
				});

			It("is not looked for by its authored separator, which cannot reach a mesh",
				[this]()
				{
					// A '.' survives neither Blender's exporter nor Unreal's importer, so a name
					// still carrying one did not come from the pipeline CONVENTIONS.md specifies.
					// Failing on the thickness field is the honest outcome: better a loud error than
					// a second, untested spelling kept alive in the parser.
					TestEqual("result", ParseName(TEXT("Edge_001_Straight_40_2000.Pent")), TEXT("Malformed"));
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
					TestEqual("result", ParseName(TEXT("Edge_01_Straight_40_2000")), TEXT("Malformed"));
					TestTrue("explains itself", Error.Len() > 0);
				});

			It("rejects an ID that is not exactly three digits",
				[this]()
				{
					TestEqual("too short", ParseName(TEXT("Edge_1_Straight_40_2000")), TEXT("Malformed"));
					TestEqual("too long", ParseName(TEXT("Edge_0001_Straight_40_2000")), TEXT("Malformed"));
					TestEqual("not a number", ParseName(TEXT("Edge_abc_Straight_40_2000")), TEXT("Malformed"));
				});

			It("rejects ID zero, which is not an identity",
				[this]() { TestEqual("result", ParseName(TEXT("Edge_000_Straight_40_2000")), TEXT("Malformed")); });

			It("rejects a subtype that is not in scope",
				[this]() { TestEqual("result", ParseName(TEXT("Edge_001_Curved_2000_40")), TEXT("Malformed")); });

			It("rejects a zero-padded size in either field, because one size means one spelling",
				[this]()
				{
					TestEqual("length", ParseName(TEXT("Edge_001_Straight_40_0500")), TEXT("Malformed"));
					TestEqual("thickness", ParseName(TEXT("Edge_001_Straight_040_500")), TEXT("Malformed"));
				});

			It("holds the thickness field to the same rules as the length field",
				[this]()
				{
					TestEqual("not a number", ParseName(TEXT("Edge_001_Straight_thick_2000")), TEXT("Malformed"));
					TestEqual("zero", ParseName(TEXT("Edge_001_Straight_0_2000")), TEXT("Malformed"));
				});

			It("says which of the two size fields is at fault",
				[this]()
				{
					// One diagnostic serving two fields is only useful if it names the field. "size"
					// stopped identifying anything the moment the grammar carried two of them.
					TestEqual("result", ParseName(TEXT("Edge_001_Straight_40_wide")), TEXT("Malformed"));
					TestTrue("names the length field", Error.Contains(TEXT("length")));
				});

			It("rejects the wrong number of fields",
				[this]()
				{
					// Only "too few" is a count error now. A sixth field is an imported authoring
					// tail, not a typo -- see "the authoring tail" above for why that is forced.
					TestEqual("too few", ParseName(TEXT("Edge_001_Straight_2000")), TEXT("Malformed"));
					TestEqual("empty field", ParseName(TEXT("Edge_001__2000_40")), TEXT("Malformed"));
				});

			It("rejects a name written to the four-field grammar that preceded Thickness",
				[this]()
				{
					// The old spelling fails loudly rather than parsing with an assumed thickness. A
					// default is exactly the silent thin-against-thick mate the field exists to stop,
					// so there is no migration path here on purpose: the asset is re-authored.
					TestEqual("untailed", ParseName(TEXT("Edge_001_Straight_2000")), TEXT("Malformed"));

					// With a tail the old name has five fields and does get as far as the size
					// fields, where it reads 2000 as a thickness and then fails on 'Pent' as the
					// length -- on a message that names the field.
					TestEqual("tailed", ParseName(TEXT("Edge_004_Straight_2000_Pent")), TEXT("Malformed"));
					TestTrue("explains itself", Error.Contains(TEXT("length")));
				});
		});

	Describe("compatibility",
		[this]()
		{
			It("matches on subtype, thickness and length",
				[this]()
				{
					FPolySnapSocketDescriptor A;
					TestTrue("A parses", FPolySnapSocketName::Parse(FStringView(TEXT("Edge_001_Straight_40_2000")), A)
											 == EPolySnapParseResult::Parsed);

					FPolySnapSocketDescriptor B;
					TestTrue("B parses", FPolySnapSocketName::Parse(FStringView(TEXT("Edge_007_Straight_40_2000")), B)
											 == EPolySnapParseResult::Parsed);

					// Different IDs, and they still mate: the ID is identity, not classification.
					TestTrue("same length and thickness mate", A.IsCompatibleWith(B));
				});

			It("refuses a different length",
				[this]()
				{
					FPolySnapSocketDescriptor A;
					TestTrue("A parses", FPolySnapSocketName::Parse(FStringView(TEXT("Edge_001_Straight_40_2000")), A)
											 == EPolySnapParseResult::Parsed);

					FPolySnapSocketDescriptor B;
					TestTrue("B parses", FPolySnapSocketName::Parse(FStringView(TEXT("Edge_001_Straight_40_3464")), B)
											 == EPolySnapParseResult::Parsed);

					TestFalse("different length does not mate", A.IsCompatibleWith(B));
				});

			It("refuses a different thickness at the same length",
				[this]()
				{
					// The seam these two would make is stepped, not flush, and nothing geometric
					// notices: the edges are the same length and meet exactly. This pair is the
					// whole reason the thickness field exists.
					FPolySnapSocketDescriptor Partition;
					TestTrue("partition parses",
						FPolySnapSocketName::Parse(FStringView(TEXT("Edge_001_Straight_40_2000")), Partition)
							== EPolySnapParseResult::Parsed);

					FPolySnapSocketDescriptor Hull;
					TestTrue("hull parses",
						FPolySnapSocketName::Parse(FStringView(TEXT("Edge_001_Straight_100_2000")), Hull)
							== EPolySnapParseResult::Parsed);

					TestFalse("different thickness does not mate", Partition.IsCompatibleWith(Hull));
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
