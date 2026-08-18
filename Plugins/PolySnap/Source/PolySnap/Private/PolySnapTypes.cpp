// Copyright (c) 2026, Alexander Verbeek. All rights reserved.

#include "PolySnapTypes.h"

namespace PolySnapTypesPrivate
{
/** The enumerator that names a unit axis vector, for a diagnostic. "none" if it names none. */
[[nodiscard]] const TCHAR* AxisNameOf(const FVector& Unit)
{
	for (uint8 Index = 0; Index <= static_cast<uint8>(EPolySnapSocketAxis::MinusZ); ++Index)
	{
		const EPolySnapSocketAxis Axis = static_cast<EPolySnapSocketAxis>(Index);
		if (PolySnapAxisToVector(Axis).Equals(Unit))
		{
			return PolySnapAxisToString(Axis);
		}
	}

	return TEXT("none");
}
} // namespace PolySnapTypesPrivate

FVector PolySnapAxisToVector(EPolySnapSocketAxis Axis)
{
	switch (Axis)
	{
		case EPolySnapSocketAxis::PlusX:
			return FVector::ForwardVector;
		case EPolySnapSocketAxis::MinusX:
			return -FVector::ForwardVector;
		case EPolySnapSocketAxis::PlusY:
			return FVector::RightVector;
		case EPolySnapSocketAxis::MinusY:
			return -FVector::RightVector;
		case EPolySnapSocketAxis::PlusZ:
			return FVector::UpVector;
		case EPolySnapSocketAxis::MinusZ:
			return -FVector::UpVector;
	}

	return FVector::ZeroVector;
}

const TCHAR* PolySnapAxisToString(EPolySnapSocketAxis Axis)
{
	switch (Axis)
	{
		case EPolySnapSocketAxis::PlusX:
			return TEXT("+X");
		case EPolySnapSocketAxis::MinusX:
			return TEXT("-X");
		case EPolySnapSocketAxis::PlusY:
			return TEXT("+Y");
		case EPolySnapSocketAxis::MinusY:
			return TEXT("-Y");
		case EPolySnapSocketAxis::PlusZ:
			return TEXT("+Z");
		case EPolySnapSocketAxis::MinusZ:
			return TEXT("-Z");
	}

	return TEXT("?");
}

FVector FPolySnapSocketAxes::DerivedNormal() const
{
	return PolySnapAxisToVector(TangentAxis) ^ PolySnapAxisToVector(OutwardAxis);
}

bool FPolySnapSocketAxes::Validate(FString* OutError) const
{
	using namespace PolySnapTypesPrivate;

	const FVector Outward = PolySnapAxisToVector(OutwardAxis);
	const FVector Tangent = PolySnapAxisToVector(TangentAxis);

	// Every value is a signed base axis, so two roles are either perpendicular or they are the
	// same axis. A non-zero dot product is exactly the second case, and it is the one that would
	// otherwise reach AxisCorrection and produce a singular matrix rather than a rotation.
	if (!FMath::IsNearlyZero(Outward | Tangent))
	{
		if (OutError != nullptr)
		{
			*OutError = FString::Printf(TEXT("Outward (%s) and Tangent (%s) are the same axis; a socket basis needs three different ones."),
				PolySnapAxisToString(OutwardAxis), PolySnapAxisToString(TangentAxis));
		}

		return false;
	}

	const FVector Derived = DerivedNormal();

	if (!PolySnapAxisToVector(NormalAxis).Equals(Derived))
	{
		if (OutError != nullptr)
		{
			// Two different mistakes, and the second one catches half of all orthogonal pairs, so
			// it is worth telling the author exactly which dropdown is the loose one. Nothing in
			// the axis table tells them that Normal's sign is not theirs to choose freely.
			*OutError = FMath::IsNearlyZero(PolySnapAxisToVector(NormalAxis) | Derived)
				? FString::Printf(TEXT("Normal (%s) must be the axis that Outward (%s) and Tangent (%s) leave over, which is %s."),
					  PolySnapAxisToString(NormalAxis), PolySnapAxisToString(OutwardAxis),
					  PolySnapAxisToString(TangentAxis), AxisNameOf(Derived))
				: FString::Printf(TEXT("Outward (%s) and Tangent (%s) put the Normal on %s, not %s. Flip either Tangent or "
                           "Normal: which face the Normal names is arbitrary, but the three must agree."),
					  PolySnapAxisToString(OutwardAxis), PolySnapAxisToString(TangentAxis), AxisNameOf(Derived),
					  PolySnapAxisToString(NormalAxis));
		}

		return false;
	}

	return true;
}
