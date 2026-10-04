// Copyright Diego Merayo Merayo. All Rights Reserved

#include "Report/AssetHtmlReport.h"

namespace
{
	const TCHAR* const Styles = TEXT(
		":root{--bg:#fff;--fg:#1f2328;--muted:#656d76;--line:#d0d7de;--panel:#f6f8fa;--added:#1a7f37;--removed:#cf222e;--modified:#9a6700;--moved:#0969da;--ok:#1a7f37;--warn:#9a6700;--bad:#cf222e;--info:#656d76}"
		"@media (prefers-color-scheme:dark){:root{--bg:#0d1117;--fg:#e6edf3;--muted:#8d96a0;--line:#30363d;--panel:#161b22;--added:#3fb950;--removed:#f85149;--modified:#d29922;--moved:#58a6ff;--ok:#3fb950;--warn:#d29922;--bad:#f85149;--info:#8d96a0}}"
		"body{margin:0 auto;max-width:1100px;padding:24px 16px;background:var(--bg);color:var(--fg);font:14px/1.5 system-ui,Segoe UI,sans-serif}"
		"h1{font-size:22px;margin:0 0 4px}h2{font-size:17px;margin:28px 0 8px;border-bottom:1px solid var(--line);padding-bottom:4px}"
		"table{border-collapse:collapse;width:100%;margin:8px 0}th,td{text-align:left;vertical-align:top;border:1px solid var(--line);padding:4px 8px}th{background:var(--panel)}"
		"td.k{width:160px;color:var(--muted);white-space:nowrap}"
		"details{margin:2px 0 2px 0}details>summary{cursor:pointer;padding:2px 0}details>div.body{margin-left:18px;border-left:2px solid var(--line);padding-left:10px}"
		".row{padding:2px 0}.chip{display:inline-block;border:1px solid currentColor;border-radius:10px;padding:0 8px;margin-right:6px;font-size:12px;white-space:nowrap}"
		".added{color:var(--added)}.removed{color:var(--removed)}.modified{color:var(--modified)}.moved{color:var(--moved)}.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}.info{color:var(--info)}"
		"code{font:12px/1.4 ui-monospace,Consolas,monospace;white-space:pre-wrap;word-break:break-word;background:var(--panel);padding:0 4px;border-radius:3px}"
		".muted{color:var(--muted)}.type{color:var(--muted);font-size:12px;margin-left:6px}.note{color:var(--muted);font-size:13px;margin:0 0 0 4px}"
		".chips{margin:8px 0}");
} // namespace

FString AssetHtmlReport::Escape(const FString& Text)
{
	FString Result;
	Result.Reserve(Text.Len() + Text.Len() / 8);

	for (const TCHAR Char : Text)
	{
		switch (Char)
		{
			case TEXT('&'):
				Result += TEXT("&amp;");
				break;
			case TEXT('<'):
				Result += TEXT("&lt;");
				break;
			case TEXT('>'):
				Result += TEXT("&gt;");
				break;
			case TEXT('"'):
				Result += TEXT("&quot;");
				break;
			case TEXT('\''):
				Result += TEXT("&#39;");
				break;
			default:
				Result.AppendChar(Char);
				break;
		}
	}

	return Result;
}

FString AssetHtmlReport::Page(const FString& Title, const FString& BodyHtml)
{
	return FString::Printf(
		TEXT(
			"<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n<title>%s</title>\n<style>%s</style>\n</head>\n<body>\n%s\n</body>\n</html>\n"),
		*Escape(Title), Styles, *BodyHtml);
}

FString AssetHtmlReport::Chip(const FString& Text, const TCHAR* CssClass)
{
	return FString::Printf(TEXT("<span class=\"chip %s\">%s</span>"), CssClass, *Escape(Text));
}

FString AssetHtmlReport::Code(const FString& Text)
{
	return FString::Printf(TEXT("<code>%s</code>"), *Escape(Text));
}

FString AssetHtmlReport::Details(const FString& SummaryHtml, const FString& BodyHtml, const bool bOpen)
{
	return FString::Printf(TEXT("<details%s><summary>%s</summary><div class=\"body\">%s</div></details>\n"), bOpen ? TEXT(" open") : TEXT(""), *SummaryHtml, *BodyHtml);
}

FString AssetHtmlReport::Table(const TArray<FString>& HeaderHtml, const TArray<TArray<FString>>& RowsOfCellHtml)
{
	FString Result = TEXT("<table>\n<tr>");
	for (const FString& Header : HeaderHtml)
	{
		Result += FString::Printf(TEXT("<th>%s</th>"), *Header);
	}
	Result += TEXT("</tr>\n");

	for (const TArray<FString>& Row : RowsOfCellHtml)
	{
		Result += TEXT("<tr>");
		for (const FString& Cell : Row)
		{
			Result += FString::Printf(TEXT("<td>%s</td>"), *Cell);
		}
		Result += TEXT("</tr>\n");
	}

	return Result + TEXT("</table>\n");
}

FString AssetHtmlReport::KeyValues(const TArray<TPair<FString, FString>>& Items)
{
	FString Result = TEXT("<table>\n");
	for (const TPair<FString, FString>& Item : Items)
	{
		Result += FString::Printf(TEXT("<tr><td class=\"k\">%s</td><td>%s</td></tr>\n"), *Escape(Item.Key), *Escape(Item.Value));
	}

	return Result + TEXT("</table>\n");
}
