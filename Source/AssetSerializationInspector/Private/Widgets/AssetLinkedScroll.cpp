// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Widgets/AssetLinkedScroll.h"

#include "Widgets/Layout/SScrollBox.h"

void FAssetLinkedScroll::SetBoxes(const TSharedPtr<SScrollBox>& InFirst, const TSharedPtr<SScrollBox>& InSecond)
{
	First = InFirst;
	Second = InSecond;
}

void FAssetLinkedScroll::SetLinked(const bool bInLinked)
{
	bLinked = bInLinked;
	if (bLinked)
	{
		const TSharedPtr<SScrollBox> FirstBox = First.Pin();
		if (FirstBox.IsValid())
		{
			OnScrolled(true, FirstBox->GetScrollOffset());
		}
	}
}

void FAssetLinkedScroll::OnScrolled(const bool bFromFirst, const float Offset)
{
	if (!bLinked)
	{
		return;
	}

	// SetScrollOffset does not tell the box's own listener, so the two boxes cannot call each other.
	if (const TSharedPtr<SScrollBox> Other = (bFromFirst ? Second : First).Pin())
	{
		Other->SetScrollOffset(Offset);
	}
}
