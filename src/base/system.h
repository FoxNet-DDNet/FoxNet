#ifndef BASE_SYSTEM_H
#define BASE_SYSTEM_H

#include "dbg.h"
#include "detect.h"
#include "fs.h"
#include "io.h"
#include "mem.h"
#include "secure.h"
#include "str.h"
#include "time.h"
#include "types.h"
#include "vmath.h"

#include <string>
#include <vector>
#include <random>

char str_lowercase(char c);
void str_lower(char *pOut);

const char *str_skip_voting_menu_prefixes(const char *pVote);

void SetFlag(uint32_t &Flags, int n, bool Value);
bool IsFlagSet(uint32_t Flags, int n);
std::string RandomUnicode(int length);
void StrToInts(int *pInts, size_t NumInts, const char *pStr);
bool IntsToStr(const int *pInts, size_t NumInts, char *pStr, size_t StrSize);

void FormatItemTime(int64_t Remaining, char *out, size_t outSize);
const char *FormatPlaytime(int64_t Time);

std::vector<const char *> StrSplit(const char *pMsg, char Delim);
std::vector<const char *> StrSplitLength(const char *pMsg, size_t Length);
void StrNewlineExceedLength(char *pOut, size_t MaxLength);
void UnescapeNewlines(char *pBuf);

const char *EscapeMessage(const char *pMessage);
const char *GetParsedArgument(const char *pStr, int Index, bool Rest);

void Rotate(vec2 Center, vec2 *pPoint, float Rotation);
std::string SanitizeMessage(const char *pMessage);

std::mt19937 &Rng();

#endif
