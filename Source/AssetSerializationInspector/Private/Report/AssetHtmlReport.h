// Copyright Diego Merayo Merayo. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

/**
 * Building blocks of the HTML reports: one self-contained page (styles inline, no scripts, no external files) that is easy to
 * attach to a ticket or open from a build's artifacts. Everything that comes from an asset (names, values, paths) goes through
 * Escape, so a property value can never inject markup.
 */
namespace AssetHtmlReport
{
	/** The text with &, <, >, " and ' replaced by entities. */
	FString Escape(const FString& Text);

	/** A whole page: the title, the styles and the body, which is HTML built from the functions below. */
	FString Page(const FString& Title, const FString& BodyHtml);

	/** A small coloured label. CssClass is one of: added, removed, modified, moved, ok, warn, bad, info. */
	FString Chip(const FString& Text, const TCHAR* CssClass);

	/** Value text in a monospace span that keeps line breaks. */
	FString Code(const FString& Text);

	/** A collapsible block. SummaryHtml is always shown; BodyHtml folds away. */
	FString Details(const FString& SummaryHtml, const FString& BodyHtml, bool bOpen);

	/** A table; every header and cell is HTML that the caller has already escaped. */
	FString Table(const TArray<FString>& HeaderHtml, const TArray<TArray<FString>>& RowsOfCellHtml);

	/** A two-column table of labels and plain values (escaped here). */
	FString KeyValues(const TArray<TPair<FString, FString>>& Items);
} // namespace AssetHtmlReport
