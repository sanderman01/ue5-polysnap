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
					TestEqual("length", Descriptor.Length.ToString(), TEXT("2000"));
					TestEqual("thickness", Descriptor.Thickness.ToString(), TEXT("40"));
				});

			It("accepts a size token of any width, in either size field",
				[this]()
				{
					TestEqual("short", ParseName(TEXT("Edge_001_Straight_6_500")), TEXT("Parsed"));
					TestEqual("length", Descriptor.Length.ToString(), TEXT("500"));
					TestEqual("thickness", Descriptor.Thickness.ToString(), TEXT("6"));

					TestEqual("long", ParseName(TEXT("Edge_999_Straight_250_12000")), TEXT("Parsed"));
					TestEqual("id", Descriptor.Id, 999);
					TestEqual("length", Descriptor.Length.ToString(), TEXT("12000"));
					TestEqual("thickness", Descriptor.Thickness.ToString(), TEXT("250"));
				});

			It("accepts a size token that carries its own unit, because none is implied",
				[this]()
				{
					// The grammar states no unit. A project authoring in millimetres, metres or
					// nothing at all is equally well served, so long as both ends of a joint agree.
					TestEqual("millimetres", ParseName(TEXT("Edge_001_Straight_40mm_2000mm")), TEXT("Parsed"));
					TestEqual("thickness", Descriptor.Thickness.ToString(), TEXT("40mm"));
					TestEqual("length", Descriptor.Length.ToString(), TEXT("2000mm"));

					TestEqual("metres", ParseName(TEXT("Edge_001_Straight_4cm_2m")), TEXT("Parsed"));
					TestEqual("thickness", Descriptor.Thickness.ToString(), TEXT("4cm"));
					TestEqual("length", Descriptor.Length.ToString(), TEXT("2m"));
				});

			It("accepts a size vocabulary that is not a measurement at all",
				[this]()
				{
					// Nothing reads these tokens as numbers, so an abstract vocabulary is as valid
					// as a dimensioned one. DESIGN section 2.2.
					TestEqual("result", ParseName(TEXT("Edge_001_Straight_Hull_Long")), TEXT("Parsed"));
					TestEqual("thickness", Descriptor.Thickness.ToString(), TEXT("Hull"));
					TestEqual("length", Descriptor.Length.ToString(), TEXT("Long"));
				});

			It("accepts a zero-padded size, which is simply a different token",
				[this]()
				{
					// No longer an error: with nothing reading the token as a number there is no
					// canonical spelling to enforce. 0500 and 500 are two distinct labels, and the
					// compatibility test below is where that difference shows up.
					TestEqual("result", ParseName(TEXT("Edge_001_Straight_040_0500")), TEXT("Parsed"));
					TestEqual("thickness", Descriptor.Thickness.ToString(), TEXT("040"));
					TestEqual("length", Descriptor.Length.ToString(), TEXT("0500"));
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
					TestEqual("length", Descriptor.Length.ToString(), TEXT("2000"));
					TestEqual("thickness", Descriptor.Thickness.ToString(), TEXT("40"));
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
					// Failing on the length field -- which is where the '.' lands -- is the honest
					// outcome, and it is the letters-and-digits rule that produces it. Better a loud
					// error than a second, untested spelling kept alive in the parser.
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

			It("rejects a size token carrying anything but letters and digits",
				[this]()
				{
					// The rule that keeps the structural diagnostics sharp: a separator, a decimal
					// point or a stray dash is a pipeline mistake, not a vocabulary choice.
					TestEqual("decimal point", ParseName(TEXT("Edge_001_Straight_40_2.5")), TEXT("Malformed"));
					TestEqual("dash", ParseName(TEXT("Edge_001_Straight_40-5_2000")), TEXT("Malformed"));
				});

			It("holds the thickness field to the same rules as the length field",
				[this]()
				{
					TestEqual("decimal point", ParseName(TEXT("Edge_001_Straight_4.0_2000")), TEXT("Malformed"));
					TestTrue("names the thickness field", Error.Contains(TEXT("thickness")));
				});

			It("says which of the two size fields is at fault",
				[this]()
				{
					// One diagnostic serving two fields is only useful if it names the field. "size"
					// stopped identifying anything the moment the grammar carried two of them.
					TestEqual("result", ParseName(TEXT("Edge_001_Straight_40_2.5")), TEXT("Malformed"));
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
					// The old spelling fails on its field count rather than parsing with an assumed
					// thickness. A default is exactly the silent thin-against-thick mate the field
					// exists to stop, so there is no migration path here: the asset is re-authored.
					TestEqual("untailed", ParseName(TEXT("Edge_001_Straight_2000")), TEXT("Malformed"));

					// A tailed old name is the one case the parser cannot catch. It has five fields,
					// and now that a size token may be any word, 'Pent' is a legal length -- so this
					// parses as thickness 2000, length Pent. Nothing is silently mis-sized by it: a
					// piece named this way mates only with another named identically. The field
					// count above is what makes the old grammar loud; opaque tokens are what make
					// this case quiet, and that trade buys unit-free naming.
					TestEqual("tailed", ParseName(TEXT("Edge_004_Straight_2000_Pent")), TEXT("Parsed"));
					TestEqual("thickness", Descriptor.Thickness.ToString(), TEXT("2000"));
					TestEqual("length", Descriptor.Length.ToString(), TEXT("Pent"));
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

			It("refuses two spellings of the same measurement, because nothing reads them as numbers",
				[this]()
				{
					// 40mm and 40 describe one panel and mate with neither. This is the cost of a
					// unit-free token, and the reason a project fixes one vocabulary and holds it.
					FPolySnapSocketDescriptor Bare;
					TestTrue("bare parses",
						FPolySnapSocketName::Parse(FStringView(TEXT("Edge_001_Straight_40_2000")), Bare)
							== EPolySnapParseResult::Parsed);

					FPolySnapSocketDescriptor Suffixed;
					TestTrue("suffixed parses",
						FPolySnapSocketName::Parse(FStringView(TEXT("Edge_001_Straight_40mm_2000mm")), Suffixed)
							== EPolySnapParseResult::Parsed);

					TestFalse("different spelling does not mate", Bare.IsCompatibleWith(Suffixed));
				});

			It("ignores case, so one vocabulary shouted and whispered still mates",
				[this]()
				{
					// FName equality is case-insensitive, and this is the one leniency the token
					// gets. It is documented rather than incidental, so it is tested.
					FPolySnapSocketDescriptor Lower;
					TestTrue("lower parses",
						FPolySnapSocketName::Parse(FStringView(TEXT("Edge_001_Straight_40mm_2000mm")), Lower)
							== EPolySnapParseResult::Parsed);

					FPolySnapSocketDescriptor Upper;
					TestTrue("upper parses",
						FPolySnapSocketName::Parse(FStringView(TEXT("Edge_002_Straight_40MM_2000MM")), Upper)
							== EPolySnapParseResult::Parsed);

					TestTrue("case does not separate them", Lower.IsCompatibleWith(Upper));
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
