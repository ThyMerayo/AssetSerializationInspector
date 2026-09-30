// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "InputCoreTypes.h"
#include "Widgets/Text/SRichTextBlock.h"

#include "Widgets/SSelectableRichText.h"

void SSelectableRichText::Construct(const FArguments& InArgs)
{
	RichText = InArgs._RichText;
	PlainText = InArgs._PlainText;
	StoredTextStyle = InArgs._TextStyle.Get();

	ChildSlot[SAssignNew(RichTextBlock, SRichTextBlock).Text(RichText).TextStyle(&StoredTextStyle).DecoratorStyleSet(InArgs._DecoratorStyleSet).AutoWrapText(false)];
}

FReply SSelectableRichText::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::Mouse);
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SSelectableRichText::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SSelectableRichText::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Unhandled();
}

FReply SSelectableRichText::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent)
{
	const bool bControlDown = KeyEvent.IsControlDown();

	if (bControlDown && KeyEvent.GetKey() == EKeys::A)
	{
		SelectionAnchor = 0;
		SelectionCursor = PlainText.Get().ToString().Len();
		return FReply::Handled();
	}

	if (bControlDown && KeyEvent.GetKey() == EKeys::C)
	{
		if (SelectionAnchor != INDEX_NONE && SelectionCursor != INDEX_NONE)
		{
			CopySelectionToClipboard();
		}
		else
		{
			CopyAllToClipboard();
		}

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SSelectableRichText::CopyAllToClipboard() const
{
	const FString Text = PlainText.Get().ToString();
	FPlatformApplicationMisc::ClipboardCopy(*Text);
}

FString SSelectableRichText::GetSelectedText() const
{
	const FString Text = PlainText.Get().ToString();

	if (SelectionAnchor == INDEX_NONE || SelectionCursor == INDEX_NONE)
	{
		return FString();
	}

	const int32 Start = FMath::Min(SelectionAnchor, SelectionCursor);
	const int32 End = FMath::Max(SelectionAnchor, SelectionCursor);
	if (Start < 0 || End > Text.Len() || Start == End)
	{
		return FString();
	}

	return Text.Mid(Start, End - Start);
}

void SSelectableRichText::CopySelectionToClipboard() const
{
	const FString Selected = GetSelectedText();
	if (!Selected.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*Selected);
	}
}
