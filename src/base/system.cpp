
#include "system.h"

#include "dbg.h"
#include "str.h"

#if defined(CONF_WEBSOCKETS)
#include <engine/shared/websockets.h>
#endif

#if defined(CONF_FAMILY_UNIX)
#include <sys/time.h> // timeval
#include <unistd.h> // close

// UNIX net includes
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#if defined(CONF_PLATFORM_SOLARIS)
#include <sys/filio.h> // FIONBIO
#endif

#include "detect.h"

#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>
#elif defined(CONF_FAMILY_WINDOWS)
#else
#error NOT IMPLEMENTED
#endif
#include "vmath.h"

#include <random>
#include <string>
#include <vector>
#include <engine/shared/config.h>

// <FoxNet
char str_lowercase(char c)
{
	if(c >= 'a' && c <= 'z')
		return 'A' + (c - 'a');
	return c;
}

void str_lower(char *pOut)
{
	while(*pOut)
	{
		*pOut = str_lowercase(*pOut);
		pOut++;
	}
}

const char *str_skip_voting_menu_prefixes(const char *pVote)
{
	if(!pVote || !pVote[0])
		return 0;

	const char *pPrefixes[] = {"•", "☒", "☐", "╭", "─", ">", "⇨", "‣", "➤", "⁃", "◆", "◇", "│"};
	const char *pTemp = pVote;
	while(1)
	{
		bool Break = true;
		for(unsigned int p = 0; p < sizeof(pPrefixes) / sizeof(pPrefixes[0]); p++)
		{
			const char *pPrefix = str_utf8_find_nocase(pTemp, pPrefixes[p]);
			if(pPrefix)
			{
				int NewCursor = str_utf8_forward(pPrefix, 0);
				if(NewCursor != 0)
				{
					pTemp = pPrefix + NewCursor;
					Break = false;
					break;
				}
			}
		}
		if(Break)
			break;
	}
	return str_skip_whitespaces_const(pTemp);
}

void SetFlag(uint32_t &Flags, int n, bool Value)
{
	if(Value)
		Flags |= (1 << n);
	else
		Flags &= ~(1 << n);
}

bool IsFlagSet(uint32_t Flags, int n)
{
	return (Flags & (1 << n)) != 0;
}

std::string RandomUnicode(int length)
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dis(0x0400, 0x04FF);

	std::string result;
	result.reserve(length * 3);

	for(int i = 0; i < length; ++i)
	{
		int codepoint = dis(gen);
		char utf8[4] = {0};
		int bytes = str_utf8_encode(utf8, codepoint);
		result.append(utf8, bytes);
	}
	return result;
}

void StrToInts(int *pInts, size_t NumInts, const char *pStr)
{
	dbg_assert(NumInts > 0, "StrToInts: NumInts invalid");
	const size_t StrSize = str_length(pStr) + 1;
	dbg_assert(StrSize <= NumInts * sizeof(int), "StrToInts: string truncated");

	for(size_t i = 0; i < NumInts; i++)
	{
		// Copy to temporary buffer to ensure we don't read past the end of the input string
		char aBuf[sizeof(int)] = {0, 0, 0, 0};
		for(size_t c = 0; c < sizeof(int) && i * sizeof(int) + c < StrSize; c++)
		{
			aBuf[c] = pStr[i * sizeof(int) + c];
		}
		pInts[i] = ((aBuf[0] + 128) << 24) | ((aBuf[1] + 128) << 16) | ((aBuf[2] + 128) << 8) | (aBuf[3] + 128);
	}
	// Last byte is always zero and unused in this format
	pInts[NumInts - 1] &= 0xFFFFFF00;
}

bool IntsToStr(const int *pInts, size_t NumInts, char *pStr, size_t StrSize)
{
	dbg_assert(NumInts > 0, "IntsToStr: NumInts invalid");
	dbg_assert(StrSize >= NumInts * sizeof(int), "IntsToStr: StrSize invalid");

	// Unpack string without validation
	size_t StrIndex = 0;
	for(size_t IntIndex = 0; IntIndex < NumInts; IntIndex++)
	{
		const int CurrentInt = pInts[IntIndex];
		pStr[StrIndex] = ((CurrentInt >> 24) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = ((CurrentInt >> 16) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = ((CurrentInt >> 8) & 0xff) - 128;
		StrIndex++;
		pStr[StrIndex] = (CurrentInt & 0xff) - 128;
		StrIndex++;
	}
	// Ensure null-termination
	pStr[StrIndex - 1] = '\0';

	// Ensure valid UTF-8
	if(str_utf8_check(pStr))
	{
		return true;
	}
	pStr[0] = '\0';
	return false;
}

void FormatItemTime(int64_t Remaining, char *out, size_t outSize)
{
	if(Remaining <= 0)
		return;

	int Days = Remaining / (60 * 60 * 24);
	int Hours = (Remaining % (60 * 60 * 24)) / (60 * 60);
	int Minutes = (Remaining % (60 * 60)) / 60;

	char DayBuf[8];
	char HourBuf[8];
	char MinuteBuf[8];
	str_format(DayBuf, sizeof(DayBuf), "%dd", Days);
	str_format(HourBuf, sizeof(HourBuf), " %dh", Hours);
	str_format(MinuteBuf, sizeof(MinuteBuf), " %dm", Minutes);

	if(Days > 0)
	{
		Hours = 1;
		Minutes = 1;
	}
	if(Hours > 0)
		Minutes = 1;

	str_format(out, outSize, "%s%s%s", Days > 0 ? DayBuf : "", Hours > 0 ? HourBuf : "", Minutes > 0 ? MinuteBuf : "");
}
const char *FormatPlaytime(int64_t Time)
{
	// playtime is saved in minutes
	static char aBuf[64];

	if(Time < 0)
		Time = 0;

	int64_t Hours = Time / 60;
	int64_t Minutes = Time % 60;

	if(Hours > 0)
		str_format(aBuf, sizeof(aBuf), "%" PRId64 " Hour%s %" PRId64 " Min%s", Hours, Hours == 1 ? "" : "s", Minutes, Minutes == 1 ? "" : "s");
	else
		str_format(aBuf, sizeof(aBuf), "%" PRId64 " Min%s", Minutes, Minutes == 1 ? "" : "s");

	return aBuf;
}

std::vector<const char *> StrSplit(const char *pMsg, char Delim)
{
	std::vector<const char *> v;
	const char *pStart = pMsg;
	const char *pCur = pMsg;
	while(*pCur)
	{
		if(*pCur == Delim)
		{
			size_t Len = pCur - pStart;
			char *pPart = (char *)malloc(Len + 1);
			str_copy(pPart, pStart, Len + 1);
			v.push_back(pPart);
			pStart = pCur + 1;
		}
		pCur++;
	}
	if(pStart != pCur)
	{
		size_t Len = pCur - pStart;
		char *pPart = (char *)malloc(Len + 1);
		str_copy(pPart, pStart, Len + 1);
		v.push_back(pPart);
	}
	return v;
}

std::vector<const char *> StrSplitLength(const char *pMsg, size_t Length)
{
	std::vector<const char *> v;
	const char *pStart = pMsg;
	const char *pCur = pMsg;
	size_t Processed = 0;
	while(*pCur && Processed < Length)
	{
		if(*pCur == ' ')
		{
			size_t Len = pCur - pStart;
			char *pPart = (char *)malloc(Len + 1);
			str_copy(pPart, pStart, Len + 1);
			v.push_back(pPart);
			pStart = pCur + 1;
		}
		pCur++;
		Processed++;
	}
	if(pStart != pCur)
	{
		size_t Len = pCur - pStart;
		char *pPart = (char *)malloc(Len + 1);
		str_copy(pPart, pStart, Len + 1);
		v.push_back(pPart);
	}
	return v;
}

void StrNewlineExceedLength(char *pOut, size_t MaxLength)
{
	if(!pOut || MaxLength == 0)
		return;

	const size_t Len = str_length(pOut);
	if(Len <= MaxLength)
		return;

	size_t lineStart = 0;
	while(lineStart < Len)
	{
		size_t lineEnd = lineStart;
		while(lineEnd < Len && pOut[lineEnd] != '\n')
			++lineEnd;

		const size_t segmentLen = lineEnd - lineStart;
		if(segmentLen <= MaxLength)
		{
			lineStart = (lineEnd < Len) ? (lineEnd + 1) : lineEnd;
			continue;
		}

		const size_t wrapLimitIdx = lineStart + MaxLength;
		size_t lastSpace = SIZE_MAX;
		for(size_t i = lineStart; i < wrapLimitIdx; ++i)
		{
			if(pOut[i] == ' ')
				lastSpace = i;
		}

		if(lastSpace != SIZE_MAX)
		{
			pOut[lastSpace] = '\n';
			lineStart = lastSpace + 1;
		}
		else
		{
			// No space to wrap on, break at MaxLength
			pOut[wrapLimitIdx] = '\n';
			lineStart = wrapLimitIdx + 1;
		}
	}
}

void UnescapeNewlines(char *pBuf)
{
	int i, j;
	for(i = 0, j = 0; pBuf[i]; i++, j++)
	{
		if(pBuf[i] == '\\' && pBuf[i + 1] == 'n')
		{
			pBuf[j] = '\n';
			i++;
		}
		else if(i != j)
		{
			pBuf[j] = pBuf[i];
		}
	}
	pBuf[j] = '\0';
}

const char *EscapeMessage(const char *pMessage)
{
	static char aEscaped[2048];
	char *pDst = aEscaped;
	const unsigned char *pSrc = (const unsigned char *)pMessage;

	while(*pSrc && (size_t)(pDst - aEscaped) < sizeof(aEscaped) - 7)
	{
		unsigned char c = *pSrc++;
		switch(c)
		{
		case '\"':
			*pDst++ = '\\';
			*pDst++ = '\"';
			break;
		case '\\':
			*pDst++ = '\\';
			*pDst++ = '\\';
			break;
		case '`':
			*pDst++ = '\\';
			*pDst++ = '`';
			break; // prevent shell command substitution on Linux
		case '$':
			*pDst++ = '\\';
			*pDst++ = '$';
			break; // prevent $(...) and $VAR expansion
		case '\b':
			*pDst++ = '\\';
			*pDst++ = 'b';
			break;
		case '\f':
			*pDst++ = '\\';
			*pDst++ = 'f';
			break;
		case '\n':
			*pDst++ = '\\';
			*pDst++ = 'n';
			break;
		case '\r':
			*pDst++ = '\\';
			*pDst++ = 'r';
			break;
		case '\t':
			*pDst++ = '\\';
			*pDst++ = 't';
			break;
		case '<':
			*pDst++ = '\\';
			*pDst++ = 'u';
			*pDst++ = '0';
			*pDst++ = '0';
			*pDst++ = '3';
			*pDst++ = 'c';
			break;
		case '>':
			*pDst++ = '\\';
			*pDst++ = 'u';
			*pDst++ = '0';
			*pDst++ = '0';
			*pDst++ = '3';
			*pDst++ = 'e';
			break;
		default:
			if(c < 0x20)
			{
				*pDst++ = '\\';
				*pDst++ = 'u';
				*pDst++ = '0';
				*pDst++ = '0';
				static const char HEX[] = "0123456789abcdef";
				*pDst++ = HEX[(c >> 4) & 0xF];
				*pDst++ = HEX[c & 0xF];
			}
			else
			{
				*pDst++ = (char)c;
			}
			break;
		}
	}
	*pDst = '\0';
	return aEscaped;
}

const char *GetParsedArgument(const char *pStr, int Index, bool Rest)
{
	static char aOutBuf[2048]; // persistent buffer for non-Rest results
	const char *pCur = pStr;
	int TokenIndex = 0;

	// Scan and locate the start of the requested token
	while(*pCur)
	{
		// skip leading spaces
		while(*pCur && std::isspace(static_cast<unsigned char>(*pCur)))
			pCur++;
		if(!*pCur)
			break;

		const char *pTokenStart = pCur;

		// If this is the token we want and Rest is requested, return the rest from here.
		if(TokenIndex == Index && Rest)
		{
			return pTokenStart;
		}

		// Parse and advance pCur to the end of this token
		if(*pCur == '"')
		{
			pCur++; // skip opening quote
			while(*pCur)
			{
				if(*pCur == '\\' && (pCur[1] == '\\' || pCur[1] == '"'))
				{
					pCur += 2; // skip escaped char
					continue;
				}
				if(*pCur == '"')
				{
					pCur++; // consume closing quote
					break;
				}
				pCur++;
			}
		}
		else
		{
			while(*pCur && !std::isspace(static_cast<unsigned char>(*pCur)))
				pCur++;
		}

		// If this is the token we want and Rest is false, extract the single token into aOutBuf.
		if(TokenIndex == Index && !Rest)
		{
			// Re-parse the token content to produce the unescaped token into aOutBuf.
			char *pDst = aOutBuf;
			size_t Remaining = sizeof(aOutBuf) - 1;

			const char *pRead = pTokenStart;
			if(*pRead == '"')
			{
				pRead++; // skip opening quote
				while(*pRead && Remaining)
				{
					if(*pRead == '\\' && (pRead[1] == '\\' || pRead[1] == '"'))
					{
						*pDst++ = pRead[1];
						Remaining--;
						pRead += 2;
						continue;
					}
					if(*pRead == '"')
					{
						// end of quoted token
						pRead++;
						break;
					}
					*pDst++ = *pRead++;
					Remaining--;
				}
			}
			else
			{
				while(*pRead && !std::isspace(static_cast<unsigned char>(*pRead)) && Remaining)
				{
					*pDst++ = *pRead++;
					Remaining--;
				}
			}

			*pDst = '\0';
			return aOutBuf[0] ? aOutBuf : nullptr;
		}

		TokenIndex++;
	}

	// Index out of range
	return nullptr;
}

std::string SanitizeMessage(const char *pMessage)
{
	std::string Out;
	const char *p = pMessage;
	while(*p)
	{
		// replace @ with # to prevent unwanted pings in webhooks
		if(*p == '@')
			Out += '#';
		else
			Out += *p;
		++p;
	}

	return Out;
}
std::mt19937 &Rng()
{
	static std::random_device rd;
	static std::mt19937 gen(rd());
	return gen;
}

const char *FormatServerInsntance(const char *pPrefix)
{
	static char aBuf[256];
	if(g_Config.m_SvAccountsInstance[0] != '\0')
		str_format(aBuf, sizeof(aBuf), "%s%s", pPrefix, g_Config.m_SvAccountsInstance);
	else
		str_copy(aBuf, "", sizeof(aBuf));
	return aBuf;
}
	// FoxNet>
