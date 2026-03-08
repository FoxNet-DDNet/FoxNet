// Co-authored-by: ByFox <byfox213@gmail.com>
#include <base/system.h>
#include "fontconvert.h"
#include <span>
#include <algorithm>

constexpr std::array<size_t, LETTER_COUNT> g_LetterSymbolCounts = []() {
	std::array<size_t, LETTER_COUNT> Counts{};
	for(size_t i = 0; i < LETTER_COUNT; ++i)
	{
		Counts[i] = 0;
		for(const auto &Sym : g_LetterSymbols[i])
		{
			if(Sym.empty())
				break;
			++Counts[i];
		}
	}
	return Counts;
}();

static bool IsSymbolInArray(const char *pInput, std::span<const std::string_view> Symbols)
{
	return std::ranges::any_of(Symbols,
		[pInput](std::string_view Sym) -> bool {
			if(Sym.empty())
				return false;

			char Temp[8];
			if(Sym.size() < sizeof(Temp))
			{
				mem_copy(Temp, Sym.data(), Sym.size());
				Temp[Sym.size()] = 0;
				return str_find_nocase(pInput, Temp) != nullptr;
			}
			return false;
		});
}

const char *FontConvert(const char *pMsg)
{
	static char s_DecodedMsg[512];
	mem_zero(s_DecodedMsg, sizeof(s_DecodedMsg));

	const char *c = pMsg;
	char aLetter[8];

	while(*c)
	{
		const char *pOld = c;
		if(str_utf8_decode(&c) == 0)
			break;

		const int Len = c - pOld;
		if(Len > 0 && Len < static_cast<int>(sizeof(aLetter)))
		{
			mem_copy(aLetter, pOld, Len);
			aLetter[Len] = 0;

			char Replacement = 0;

			for(size_t i = 0; i < g_LetterSymbols.size(); ++i)
			{
				const auto SymbolsSpan = std::span(
					g_LetterSymbols[i].data(),
					g_LetterSymbolCounts[i]);

				if(IsSymbolInArray(aLetter, SymbolsSpan))
				{
					Replacement = static_cast<char>('a' + i);
					break;
				}
			}

			if(!Replacement)
			{
				for(size_t i = 0; i < g_NumberSymbols.size(); ++i)
				{
					if(IsSymbolInArray(aLetter, g_NumberSymbols[i]))
					{
						Replacement = static_cast<char>('0' + i);
						break;
					}
				}
			}

			if(Replacement)
			{
				const char Temp[2] = {Replacement, 0};
				str_append(s_DecodedMsg, Temp);
				continue;
			}
			str_append(s_DecodedMsg, aLetter);
		}
	}

	return s_DecodedMsg;
}

const char *ConvertToSmallCaps(const char *pMsg)
{
	static char s_SmallCapsMsg[512];
	mem_zero(s_SmallCapsMsg, sizeof(s_SmallCapsMsg));

	const char *c = pMsg;
	char aLetter[8];
	char aReplacement[8];

	while(*c)
	{
		const char *pOld = c;
		if(str_utf8_decode(&c) == 0)
			break;

		const int Len = c - pOld;
		if(Len > 0 && Len < static_cast<int>(sizeof(aLetter)))
		{
			mem_copy(aLetter, pOld, Len);
			aLetter[Len] = 0;

			bool Replaced = false;

			for(size_t i = 0; i < g_LetterSymbols.size(); ++i)
			{
				const auto SymbolsSpan = std::span(
					g_LetterSymbols[i].data(),
					g_LetterSymbolCounts[i]);

				if(IsSymbolInArray(aLetter, SymbolsSpan))
				{
					const auto SmallCaps = g_SmallCaps[i];
					if(SmallCaps.size() < sizeof(aReplacement))
					{
						mem_copy(aReplacement, SmallCaps.data(), SmallCaps.size());
						aReplacement[SmallCaps.size()] = 0;
						str_append(s_SmallCapsMsg, aReplacement, sizeof(s_SmallCapsMsg));
						Replaced = true;
					}
					break;
				}
			}

			if(!Replaced)
				str_append(s_SmallCapsMsg, aLetter, sizeof(s_SmallCapsMsg));
		}
	}

	return s_SmallCapsMsg;
}