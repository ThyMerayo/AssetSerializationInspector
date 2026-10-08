// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

class SScrollBox;

/**
 * Keeps two scroll boxes at the same offset: when the user scrolls one (with the wheel or its scroll bar), the other follows. Used by the
 * hex panels of the diff, whose rows line up, so that a big range is compared without scrolling both sides.
 */
class ASSETSERIALIZATIONINSPECTOR_API FAssetLinkedScroll
{
public:
	/** The two boxes. Either can be null, which leaves the link with nothing to move. */
	void SetBoxes(const TSharedPtr<SScrollBox>& InFirst, const TSharedPtr<SScrollBox>& InSecond);

	/** Whether scrolling one moves the other. On by default. Turning it on puts the second box where the first is. */
	void SetLinked(bool bInLinked);
	bool IsLinked() const { return bLinked; }

	/** The user scrolled a box to Offset: moves the other one there when linked. */
	void OnScrolled(bool bFromFirst, float Offset);

private:
	TWeakPtr<SScrollBox> First;
	TWeakPtr<SScrollBox> Second;
	bool bLinked = true;
};
