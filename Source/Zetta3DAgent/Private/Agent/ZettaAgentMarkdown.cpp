// Copyright Zetta 3D. See LICENSE.md.

#include "ZettaAgentMarkdown.h"

#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/TextDecorators.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Text/STextBlock.h"
#include "ZettaSettings.h"
#include "ZettaStyle.h"

#define LOCTEXT_NAMESPACE "Zetta3D"

namespace
{
	TSharedPtr<FSlateStyleSet> MdStyleSet;

	FString MdEscape(const FString& Text)
	{
		FString Out;
		Out.Reserve(Text.Len());
		for (const TCHAR C : Text)
		{
			switch (C)
			{
			case TEXT('&'): Out += TEXT("&amp;"); break;
			case TEXT('<'): Out += TEXT("&lt;"); break;
			case TEXT('>'): Out += TEXT("&gt;"); break;
			case TEXT('"'): Out += TEXT("&quot;"); break;
			default: Out.AppendChar(C);
			}
		}
		return Out;
	}

	FString MdRun(const TCHAR* Style, const FString& Text)
	{
		if (Text.IsEmpty()) return FString();
		return FString::Printf(TEXT("<%s>%s</>"), Style, *MdEscape(Text));
	}

	FString MdLink(const FString& Label, FString Url)
	{
		// The markup keeps an attribute between quotes: a link with one in it is not a link we open.
		Url.ReplaceInline(TEXT("\""), TEXT(""));
		Url.ReplaceInline(TEXT("<"), TEXT(""));
		Url.ReplaceInline(TEXT(">"), TEXT(""));
		return FString::Printf(TEXT("<a id=\"link\" href=\"%s\" style=\"Md.Link\">%s</>"), *Url, *MdEscape(Label.IsEmpty() ? Url : Label));
	}

	bool MdIsWordChar(TCHAR C) { return FChar::IsAlnum(C) || C == TEXT('_'); }

	/** Inline Markdown on one line → rich-text markup. Runs do not nest; the innermost markers are simply dropped. */
	FString MdInline(const FString& Text)
	{
		FString Out;
		const int32 N = Text.Len();
		int32 i = 0;
		auto Find = [&Text](const TCHAR* What, int32 From) { return Text.Find(What, ESearchCase::CaseSensitive, ESearchDir::FromStart, From); };
		while (i < N)
		{
			const TCHAR C = Text[i];
			if (C == TEXT('`'))
			{
				const int32 End = Find(TEXT("`"), i + 1);
				if (End > i + 1)
				{
					Out += MdRun(TEXT("Md.Code"), Text.Mid(i + 1, End - i - 1));
					i = End + 1;
					continue;
				}
			}
			if (C == TEXT('*') && i + 1 < N && Text[i + 1] == TEXT('*'))
			{
				const int32 End = Find(TEXT("**"), i + 2);
				if (End > i + 2)
				{
					FString Inner = Text.Mid(i + 2, End - i - 2).Replace(TEXT("`"), TEXT(""));
					Out += MdRun(TEXT("Md.Bold"), Inner);
					i = End + 2;
					continue;
				}
			}
			if ((C == TEXT('*') || C == TEXT('_')) && i + 1 < N && !FChar::IsWhitespace(Text[i + 1]) && (i == 0 || !MdIsWordChar(Text[i - 1])))
			{
				int32 End = INDEX_NONE;
				for (int32 j = i + 1; j < N; ++j)
				{
					if (Text[j] == C && !FChar::IsWhitespace(Text[j - 1]) && (j + 1 >= N || !MdIsWordChar(Text[j + 1])))
					{
						End = j;
						break;
					}
				}
				if (End > i + 1)
				{
					Out += MdRun(TEXT("Md.Italic"), Text.Mid(i + 1, End - i - 1).Replace(TEXT("`"), TEXT("")));
					i = End + 1;
					continue;
				}
			}
			if (C == TEXT('['))
			{
				const int32 Close = Find(TEXT("]("), i + 1);
				const int32 End = Close != INDEX_NONE ? Find(TEXT(")"), Close + 2) : INDEX_NONE;
				if (Close != INDEX_NONE && End != INDEX_NONE)
				{
					const FString Url = Text.Mid(Close + 2, End - Close - 2).TrimStartAndEnd();
					if (Url.StartsWith(TEXT("http://")) || Url.StartsWith(TEXT("https://")))
					{
						Out += MdLink(Text.Mid(i + 1, Close - i - 1), Url);
						i = End + 1;
						continue;
					}
				}
			}
			if (C == TEXT('h') && (i == 0 || FChar::IsWhitespace(Text[i - 1]) || Text[i - 1] == TEXT('(')) &&
				(Text.Mid(i, 8) == TEXT("https://") || Text.Mid(i, 7) == TEXT("http://")))
			{
				int32 End = i;
				while (End < N && !FChar::IsWhitespace(Text[End]) && Text[End] != TEXT(')')) ++End;
				FString Url = Text.Mid(i, End - i);
				// A sentence's full stop is not part of the link.
				while (Url.EndsWith(TEXT(".")) || Url.EndsWith(TEXT(",")))
				{
					Url.LeftChopInline(1);
					--End;
				}
				Out += MdLink(Url, Url);
				i = End;
				continue;
			}
			Out += MdEscape(FString(1, &Text[i]));
			++i;
		}
		return Out;
	}

	/** Block-level Markdown (one line at a time) → markup for one rich text. */
	FString MdBlocks(const FString& Text)
	{
		TArray<FString> Lines;
		Text.Replace(TEXT("\r"), TEXT("")).ParseIntoArray(Lines, TEXT("\n"), false);
		FString Out;
		for (int32 n = 0; n < Lines.Num(); ++n)
		{
			const FString& Line = Lines[n];
			const FString Trimmed = Line.TrimStart();
			const int32 Indent = Line.Len() - Trimmed.Len();
			const FString Pad = FString::ChrN(FMath::Min(Indent, 12), TEXT(' '));
			FString Markup;
			if (Trimmed.StartsWith(TEXT("# ")))
			{
				Markup = MdRun(TEXT("Md.H1"), Trimmed.Mid(2).Replace(TEXT("**"), TEXT("")));
			}
			else if (Trimmed.StartsWith(TEXT("## ")) || Trimmed.StartsWith(TEXT("### ")) || Trimmed.StartsWith(TEXT("#### ")))
			{
				int32 Hashes = 0;
				while (Hashes < Trimmed.Len() && Trimmed[Hashes] == TEXT('#')) ++Hashes;
				Markup = MdRun(TEXT("Md.H2"), Trimmed.Mid(Hashes).TrimStart().Replace(TEXT("**"), TEXT("")));
			}
			else if (Trimmed.StartsWith(TEXT("- ")) || Trimmed.StartsWith(TEXT("* ")) || Trimmed.StartsWith(TEXT("+ ")))
			{
				Markup = Pad + TEXT("  •  ") + MdInline(Trimmed.Mid(2));
			}
			else if (Trimmed.Len() > 2 && FChar::IsDigit(Trimmed[0]) && (Trimmed.Contains(TEXT(". ")) || Trimmed.Contains(TEXT(") "))))
			{
				int32 Digits = 0;
				while (Digits < Trimmed.Len() && FChar::IsDigit(Trimmed[Digits])) ++Digits;
				if (Digits < Trimmed.Len() - 1 && (Trimmed[Digits] == TEXT('.') || Trimmed[Digits] == TEXT(')')) && Trimmed[Digits + 1] == TEXT(' '))
				{
					Markup = Pad + TEXT("  ") + Trimmed.Left(Digits + 1) + TEXT(" ") + MdInline(Trimmed.Mid(Digits + 2));
				}
				else
				{
					Markup = Pad + MdInline(Trimmed);
				}
			}
			else if (Trimmed.StartsWith(TEXT("> ")))
			{
				Markup = TEXT("│  ") + MdRun(TEXT("Md.Italic"), Trimmed.Mid(2));
			}
			else if (Trimmed == TEXT("---") || Trimmed == TEXT("***") || Trimmed == TEXT("___"))
			{
				Markup = TEXT("──────────");
			}
			else
			{
				Markup = Pad + MdInline(Trimmed);
			}
			Out += Markup;
			if (n < Lines.Num() - 1) Out += TEXT("\n");
		}
		return Out;
	}

	void MdOpenLink(const FSlateHyperlinkRun::FMetadata& Metadata)
	{
		const FString* Url = Metadata.Find(TEXT("href"));
		if (Url && (Url->StartsWith(TEXT("https://")) || Url->StartsWith(TEXT("http://")))) FPlatformProcess::LaunchURL(**Url, nullptr, nullptr);
	}

	TSharedRef<SWidget> MdRichText(const FString& Markup)
	{
		TArray<TSharedRef<ITextDecorator>> Decorators;
		Decorators.Add(FHyperlinkDecorator::Create(TEXT("link"), FSlateHyperlinkRun::FOnClick::CreateStatic(&MdOpenLink)));
		return SNew(SMultiLineEditableText)
			.Text(FText::FromString(Markup))
			.Marshaller(FRichTextLayoutMarshaller::Create(Decorators, &ZettaAgentMarkdown::Style()))
			.TextStyle(&ZettaAgentMarkdown::Style().GetWidgetStyle<FTextBlockStyle>("Md.Normal"))
			.IsReadOnly(true)
			.AutoWrapText(true)
			.AllowContextMenu(true)
			.ClearTextSelectionOnFocusLoss(true);
	}
}

namespace ZettaAgentMarkdown
{
	/** Every text style at the standard size plus Step points (FSlateStyleSet::Set replaces what was there). */
	static void MdFill(int32 Step)
	{
		const FTextBlockStyle Normal = FTextBlockStyle(FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
			.SetFont(FZettaStyle::Font(10 + Step))
			.SetColorAndOpacity(FSlateColor::UseForeground());
		MdStyleSet->Set("Md.Normal", Normal);
		MdStyleSet->Set("Md.Muted", FTextBlockStyle(Normal).SetColorAndOpacity(FSlateColor(FZettaStyle::Muted())));
		MdStyleSet->Set("Md.Error", FTextBlockStyle(Normal).SetColorAndOpacity(FSlateColor(FZettaStyle::Error())));
		MdStyleSet->Set("Md.Bold", FTextBlockStyle(Normal).SetFont(FZettaStyle::Font(10 + Step, true)));
		MdStyleSet->Set("Md.Italic", FTextBlockStyle(Normal).SetFont(FCoreStyle::GetDefaultFontStyle("Italic", 10 + Step)));
		MdStyleSet->Set("Md.H1", FTextBlockStyle(Normal).SetFont(FZettaStyle::Font(13 + Step, true)));
		MdStyleSet->Set("Md.H2", FTextBlockStyle(Normal).SetFont(FZettaStyle::Font(11 + Step, true)));
		MdStyleSet->Set("Md.Code", FTextBlockStyle(Normal)
			.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 9 + Step))
			.SetColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(FColor(0x8C, 0xC8, 0xFF)))));
		MdStyleSet->Set("Md.Mono", FTextBlockStyle(Normal)
			.SetFont(FCoreStyle::GetDefaultFontStyle("Mono", 9 + Step))
			.SetColorAndOpacity(FSlateColor(FLinearColor::FromSRGBColor(FColor(0xC8, 0xDC, 0xF0)))));

		const FTextBlockStyle LinkText = FTextBlockStyle(Normal).SetColorAndOpacity(FSlateColor(FZettaStyle::AccentHover()));
		const FButtonStyle LinkButton = FButtonStyle()
			.SetNormal(FSlateNoResource())
			.SetHovered(FSlateNoResource())
			.SetPressed(FSlateNoResource());
		MdStyleSet->Set("Md.Link", FHyperlinkStyle().SetTextStyle(LinkText).SetUnderlineStyle(LinkButton).SetPadding(FMargin(0)));
	}

	void Initialize()
	{
		if (MdStyleSet.IsValid()) return;
		MdStyleSet = MakeShared<FSlateStyleSet>(TEXT("ZettaAgentMarkdown"));
		MdFill(UZettaUserSettings::Get()->GetAgentTextSize());
		// The one brush: set once here, not on every resize (the style set owns it).
		MdStyleSet->Set("Md.CodeBlock", new FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(0x14, 0x13, 0x10)), 6.f));
		FSlateStyleRegistry::RegisterSlateStyle(*MdStyleSet);
	}

	void Shutdown()
	{
		if (!MdStyleSet.IsValid()) return;
		FSlateStyleRegistry::UnRegisterSlateStyle(*MdStyleSet);
		MdStyleSet.Reset();
	}

	void SetTextSize(int32 Step)
	{
		if (!MdStyleSet.IsValid()) Initialize();
		MdFill(Step);
	}

	const ISlateStyle& Style()
	{
		if (!MdStyleSet.IsValid()) Initialize();
		return *MdStyleSet;
	}

	TSharedRef<SWidget> Plain(const FString& Text, FName StyleName)
	{
		return SNew(SMultiLineEditableText)
			.Text(FText::FromString(Text))
			.TextStyle(&Style().GetWidgetStyle<FTextBlockStyle>(StyleName))
			.IsReadOnly(true)
			.AutoWrapText(true)
			.AllowContextMenu(true)
			.ClearTextSelectionOnFocusLoss(true);
	}

	TSharedRef<SWidget> Code(const FString& Code, const FString& Language)
	{
		const FString Copy = Code;
		return SNew(SBorder)
			.BorderImage(Style().GetBrush("Md.CodeBlock"))
			.Padding(FMargin(10, 4, 4, 8))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(Language.IsEmpty() ? LOCTEXT("AgentUI.CodeLabel", "code") : FText::FromString(Language))
						.Font(FZettaStyle::Font(8))
						.ColorAndOpacity(FSlateColor(FZettaStyle::Muted()))
					]
					+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
						.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButton"))
						.ToolTipText(LOCTEXT("AgentUI.CopyCodeTip", "Copy this code"))
						.OnClicked_Lambda([Copy]()
						{
							FPlatformApplicationMisc::ClipboardCopy(*Copy);
							return FReply::Handled();
						})
						[
							SNew(STextBlock).Text(LOCTEXT("AgentUI.CopyCode", "Copy")).Font(FZettaStyle::Font(8))
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
				[
					SNew(SMultiLineEditableText)
					.Text(FText::FromString(Code.Replace(TEXT("\t"), TEXT("    "))))
					.TextStyle(&Style().GetWidgetStyle<FTextBlockStyle>("Md.Mono"))
					.IsReadOnly(true)
					.AutoWrapText(true)
					.AllowContextMenu(true)
					.ClearTextSelectionOnFocusLoss(true)
				]
			];
	}

	TSharedRef<SWidget> RenderLive(TFunction<FString()> GetText)
	{
		// The conversion runs only when the text has grown, not every frame.
		struct FLive
		{
			FString Source;
			FText Markup;
		};
		TSharedRef<FLive> Live = MakeShared<FLive>();
		TArray<TSharedRef<ITextDecorator>> Decorators;
		Decorators.Add(FHyperlinkDecorator::Create(TEXT("link"), FSlateHyperlinkRun::FOnClick::CreateStatic(&MdOpenLink)));
		return SNew(SMultiLineEditableText)
			.Text_Lambda([Live, GetText]()
			{
				const FString Now = GetText ? GetText() : FString();
				if (Now != Live->Source)
				{
					Live->Source = Now;
					FString Trimmed = Now;
					while (Trimmed.StartsWith(TEXT("\n"))) Trimmed.RightChopInline(1);
					while (Trimmed.EndsWith(TEXT("\n"))) Trimmed.LeftChopInline(1);
					Live->Markup = FText::FromString(MdBlocks(Trimmed));
				}
				return Live->Markup;
			})
			.Marshaller(FRichTextLayoutMarshaller::Create(Decorators, &Style()))
			.TextStyle(&Style().GetWidgetStyle<FTextBlockStyle>("Md.Normal"))
			.IsReadOnly(true)
			.AutoWrapText(true)
			.AllowContextMenu(true)
			.ClearTextSelectionOnFocusLoss(true);
	}

	TSharedRef<SWidget> Render(const FString& Markdown)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		auto AddText = [&Box](const FString& Text)
		{
			FString Trimmed = Text;
			while (Trimmed.StartsWith(TEXT("\n"))) Trimmed.RightChopInline(1);
			while (Trimmed.EndsWith(TEXT("\n"))) Trimmed.LeftChopInline(1);
			if (Trimmed.TrimStartAndEnd().IsEmpty()) return;
			Box->AddSlot().AutoHeight().Padding(0, 1)[MdRichText(MdBlocks(Trimmed))];
		};

		// Fenced code blocks first; everything between them is text. An unclosed fence (still streaming) runs to the end.
		int32 Cursor = 0;
		while (Cursor < Markdown.Len())
		{
			const int32 Fence = Markdown.Find(TEXT("```"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor);
			if (Fence == INDEX_NONE) break;
			AddText(Markdown.Mid(Cursor, Fence - Cursor));
			int32 LineEnd = Markdown.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Fence);
			if (LineEnd == INDEX_NONE) LineEnd = Markdown.Len();
			const FString Language = Markdown.Mid(Fence + 3, LineEnd - Fence - 3).TrimStartAndEnd();
			const int32 BodyStart = FMath::Min(LineEnd + 1, Markdown.Len());
			int32 Close = Markdown.Find(TEXT("```"), ESearchCase::CaseSensitive, ESearchDir::FromStart, BodyStart);
			FString Body = Markdown.Mid(BodyStart, (Close == INDEX_NONE ? Markdown.Len() : Close) - BodyStart);
			while (Body.EndsWith(TEXT("\n"))) Body.LeftChopInline(1);
			Box->AddSlot().AutoHeight().Padding(0, 4)[Code(Body, Language)];
			Cursor = Close == INDEX_NONE ? Markdown.Len() : Close + 3;
		}
		if (Cursor < Markdown.Len()) AddText(Markdown.Mid(Cursor));
		return Box;
	}
}

#undef LOCTEXT_NAMESPACE
