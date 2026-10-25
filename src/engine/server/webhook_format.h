#ifndef ENGINE_SERVER_WEBHOOK_FORMAT_H
#define ENGINE_SERVER_WEBHOOK_FORMAT_H

#include <cstring>
#include <string>

namespace WebhookFormat
{
inline std::string EscapeText(const char *pText)
{
	std::string Result;
	if(!pText)
		return Result;
	for(const char *p = pText; *p; ++p)
	{
		if(*p == '\\' || *p == '`' || *p == '*' || *p == '_' || *p == '~' || *p == '|' || *p == '>' || *p == '[' || *p == ']')
			Result += '\\';
		Result += *p;
	}
	return Result;
}

inline std::string Name(const char *pName)
{
	if(!pName)
		pName = "";
	if(std::strchr(pName, '`'))
		return "**" + EscapeText(pName) + "**";
	return "`" + std::string(pName) + "`";
}

inline std::string CodeBlock(std::string Text)
{
	for(size_t Pos = 0; (Pos = Text.find("```", Pos)) != std::string::npos; Pos += 5)
		Text.replace(Pos, 3, "`\xe2\x80\x8b``");
	return "```\n" + Text + "\n```";
}
}

#endif
