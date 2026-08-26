// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SRichTextBlock;

class SSelectableRichText : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSelectableRichText) {}
	SLATE_ATTRIBUTE(FText, RichText)
	SLATE_ATTRIBUTE(FText, PlainText)
	SLATE_ARGUMENT(const ISlateStyle*, DecoratorStyleSet)
	SLATE_ATTRIBUTE(FTextBlockStyle, TextStyle)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& KeyEvent) override;

	virtual bool SupportsKeyboardFocus() const override { return true; }

private:
	void CopySelectionToClipboard() const;
	void CopyAllToClipboard() const;

	int32 FindCharacterIndexAtPosition(const FGeometry& MyGeometry, const FVector2D& ScreenPosition) const;

	FString GetSelectedText() const;

private:
	TAttribute<FText> RichText;
	TAttribute<FText> PlainText;

	TSharedPtr<SRichTextBlock> RichTextBlock;

	int32 SelectionAnchor = INDEX_NONE;
	int32 SelectionCursor = INDEX_NONE;

	FTextBlockStyle StoredTextStyle;

	bool bDraggingSelection = false;
};