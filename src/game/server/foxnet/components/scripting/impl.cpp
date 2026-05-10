#include "impl.h"

#include <base/log.h>

#include <engine/external/regex.h>
#include <engine/storage.h>

#include <variant>

#define CHAISCRIPT_NO_THREADS
#define CHAISCRIPT_NO_THREADS_WARNING
#include <base/math.h>
#include <base/str.h>

#include <algorithm>
#include <chaiscript.hpp>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <new> // for placement new
#include <string>
#include <vector>

static const auto NAMESPACE_RE = [](chaiscript::Namespace &Re) {
	Re["compile"] = chaiscript::var(chaiscript::fun([](const std::string &s) {
		const auto R = Regex(s);
		if(!R.error().empty())
			throw std::string("Failed to compile regex: ") + R.error();
		return R;
	}));
	Re["test"] = chaiscript::var(chaiscript::fun(&Regex::test));
	Re["match"] = chaiscript::var(chaiscript::fun(&Regex::match));
	Re["replace"] = chaiscript::var(chaiscript::fun(&Regex::replace));
};

static const auto NAMESPACE_MATH = [](chaiscript::Namespace &Math) {
	Math["pi"] = chaiscript::const_var(3.14159265358979323846);
	Math["e"] = chaiscript::const_var(2.7182818284590452354);
	Math["pow"] = chaiscript::var(chaiscript::fun([](double x, double y) { return pow(x, y); }));
	Math["sqrt"] = chaiscript::var(chaiscript::fun([](double x) { return sqrt(x); }));
	Math["sin"] = chaiscript::var(chaiscript::fun([](double x) { return sin(x); }));
	Math["cos"] = chaiscript::var(chaiscript::fun([](double x) { return cos(x); }));
	Math["tan"] = chaiscript::var(chaiscript::fun([](double x) { return tan(x); }));
	Math["asin"] = chaiscript::var(chaiscript::fun([](double x) { return asin(x); }));
	Math["acos"] = chaiscript::var(chaiscript::fun([](double x) { return acos(x); }));
	Math["atan"] = chaiscript::var(chaiscript::fun([](double x) { return atan(x); }));
	Math["atan2"] = chaiscript::var(chaiscript::fun([](double y, double x) { return atan2(y, x); }));
	Math["log"] = chaiscript::var(chaiscript::fun([](double x) { return log(x); }));
	Math["log2"] = chaiscript::var(chaiscript::fun([](double x) { return log2(x); }));
	Math["log10"] = chaiscript::var(chaiscript::fun([](double x) { return log10(x); }));
	Math["ceil"] = chaiscript::var(chaiscript::fun([](double x) { return ceil(x); }));
	Math["floor"] = chaiscript::var(chaiscript::fun([](double x) { return floor(x); }));
	Math["round"] = chaiscript::var(chaiscript::fun([](double x) { return round(x); }));
	Math["abs"] = chaiscript::var(chaiscript::fun([](double x) { return fabs(x); }));
	Math["abs"] = chaiscript::var(chaiscript::fun([](int x) { return fabs(x); }));
	Math["clamp"] = chaiscript::var(chaiscript::fun([](double val, double min, double max) { return std::clamp(val, min, max); }));
	Math["clamp"] = chaiscript::var(chaiscript::fun([](int val, int min, int max) { return std::clamp(val, min, max); }));
	Math["min"] = chaiscript::var(chaiscript::fun([](double a, double b) { return std::min(a, b); }));
	Math["min"] = chaiscript::var(chaiscript::fun([](int a, int b) { return std::min(a, b); }));
	Math["max"] = chaiscript::var(chaiscript::fun([](double a, double b) { return std::max(a, b); }));
	Math["max"] = chaiscript::var(chaiscript::fun([](int a, int b) { return std::max(a, b); }));
	Math["random"] = chaiscript::var(chaiscript::fun([](double min, double max) { return (double)random_float((float)min, (float)max); }));
	Math["random"] = chaiscript::var(chaiscript::fun([](int min, int max) { return min + rand() % (max - min + 1); }));
};

static const char *ReadScript(IStorage *pStorage, const char *pFilename)
{
	const char *pScript;
	pScript = pStorage->ReadFileStr(pFilename, IStorage::TYPE_ALL);
	if(!pScript || !*pScript)
		throw std::string("Failed to open script '") + std::string(pFilename) + std::string("'");
	return pScript;
}

class CScriptingCtx::CScriptingCtxData
{
public:
	IStorage *m_pStorage;
	chaiscript::ChaiScript m_Chai;
	chaiscript::ChaiScript::State m_BaseState;
	std::vector<std::function<void(chaiscript::ChaiScript &)>> m_Rebinders;
};

void CScriptingCtx::BuildEngine(CScriptingCtx::CScriptingCtxData *pData)
{
	pData->m_Chai.~ChaiScript();
	new(&pData->m_Chai) chaiscript::ChaiScript({}, {}, {chaiscript::Options::No_Load_Modules, chaiscript::Options::No_External_Scripts});

	pData->m_Chai.add(chaiscript::fun([&](const std::string &Module) {
		const char *pScript = ReadScript(pData->m_pStorage, Module.c_str());
		auto Out = pData->m_Chai.eval(pScript, chaiscript::Exception_Handler(), Module);
		std::free((void *)pScript);
		return Out;
	}),
		"include");
	pData->m_Chai.add(chaiscript::fun([&](const std::string &Path) {
		return pData->m_pStorage->FileExists(Path.c_str(), IStorage::TYPE_ALL);
	}),
		"file_exists");
	pData->m_Chai.register_namespace(NAMESPACE_RE, "re");
	pData->m_Chai.register_namespace(NAMESPACE_MATH, "math");

	// Define 'args' once; value updated per Run
	pData->m_Chai.add_global(chaiscript::var(std::string()), "args");

	for(const auto &Fn : pData->m_Rebinders)
	{
		Fn(pData->m_Chai);
	}

	pData->m_BaseState = pData->m_Chai.get_state(); // snapshot clean state
}

CScriptingCtx::CScriptingCtx()
{
	m_pData = new CScriptingCtxData{
		nullptr,
		chaiscript::ChaiScript({}, {}, {chaiscript::Options::No_Load_Modules, chaiscript::Options::No_External_Scripts}),
		{},
		{}};
	BuildEngine(m_pData);
}

CScriptingCtx::~CScriptingCtx()
{
	delete m_pData;
}

static chaiscript::Boxed_Value Any2Boxed(const CScriptingCtx::Any &Any)
{
	if(std::holds_alternative<std::nullptr_t>(Any))
		return chaiscript::void_var();
	if(std::holds_alternative<std::string>(Any))
		return chaiscript::const_var(std::get<std::string>(Any));
	if(std::holds_alternative<bool>(Any))
		return chaiscript::const_var(std::get<bool>(Any));
	if(std::holds_alternative<int>(Any))
		return chaiscript::const_var(std::get<int>(Any));
	if(std::holds_alternative<float>(Any))
		return chaiscript::const_var(std::get<float>(Any));
	if(std::holds_alternative<int64_t>(Any))
		return chaiscript::const_var(std::get<int64_t>(Any));
	throw "Cannot convert Any to Boxed_Value";
}

template<>
void CScriptingCtx::AddFunctionInternal(const char *pName, const std::function<CScriptingCtx::Any(const std::string &Str, const CScriptingCtx::Any &Any)> &Function)
{
	const std::string Name = pName;
	auto Rebind = [Name, Function](chaiscript::ChaiScript &Chai) {
		Chai.add(chaiscript::fun([=](const std::string &Str) {
			return Any2Boxed(Function(Str, nullptr));
		}),
			Name);
		Chai.add(chaiscript::fun([=](const std::string &Str, int Int) {
			return Any2Boxed(Function(Str, Int));
		}),
			Name);
		Chai.add(chaiscript::fun([=](const std::string &Str, float Float) {
			return Any2Boxed(Function(Str, Float));
		}),
			Name);
		Chai.add(chaiscript::fun([=](const std::string &Str, bool Bool) {
			return Any2Boxed(Function(Str, Bool));
		}),
			Name);
		Chai.add(chaiscript::fun([=](const std::string &Str, const std::string &Str2) {
			return Any2Boxed(Function(Str, Str2));
		}),
			Name);
	};
	m_pData->m_Rebinders.push_back(Rebind);
	Rebind(m_pData->m_Chai);
}

template<>
void CScriptingCtx::AddFunctionInternal(const char *pName, const std::function<void(const std::string &Str)> &Function)
{
	const std::string Name = pName;
	auto Rebind = [Name, Function](chaiscript::ChaiScript &Chai) {
		Chai.add(chaiscript::fun([=](const std::string &Str) { Function(Str); }), Name);
	};
	m_pData->m_Rebinders.push_back(Rebind);
	Rebind(m_pData->m_Chai);
	m_pData->m_BaseState = m_pData->m_Chai.get_state();
}

template<>
void CScriptingCtx::AddFunctionInternal(const char *pName, const std::function<CScriptingCtx::Any()> &Function)
{
	const std::string Name = pName;
	auto Rebind = [Name, Function](chaiscript::ChaiScript &Chai) {
		Chai.add(chaiscript::fun([=]() {
			return Any2Boxed(Function());
		}),
			Name);
	};
	m_pData->m_Rebinders.push_back(Rebind);
	Rebind(m_pData->m_Chai);
	m_pData->m_BaseState = m_pData->m_Chai.get_state();
}

template<>
void CScriptingCtx::AddGlobal(const char *pName, const std::string &Object)
{
	const std::string Name = pName;
	auto Rebind = [Name, Object](chaiscript::ChaiScript &Chai) {
		Chai.add_global_const(chaiscript::const_var(Object), Name);
	};
	m_pData->m_Rebinders.push_back(Rebind);
	Rebind(m_pData->m_Chai);
}

void CScriptingCtx::Run(IStorage *pStorage, const char *pFilename, const char *pArgs)
{
	m_pData->m_pStorage = pStorage;
	const char *pScript = nullptr;
	try
	{
		pScript = ReadScript(pStorage, pFilename);

		// Reset to clean host-only state (fast) to drop previous user defs
		m_pData->m_Chai.set_state(m_pData->m_BaseState);

		m_pData->m_Chai.set_global(chaiscript::var(std::string(pArgs ? pArgs : "")), "args");

		std::string Wrapped;
		Wrapped.reserve(str_length(pScript) + 18);
		Wrapped += "(fun(){\n";
		Wrapped += pScript;
		Wrapped += "\n})();";

		m_pData->m_Chai.eval(Wrapped, chaiscript::Exception_Handler(), pFilename);
	}
	catch(const chaiscript::exception::eval_error &e)
	{
		log_error(SCRIPTING_IMPL, "Eval error in '%s': %s", pFilename, e.pretty_print().c_str());
	}
	catch(const std::exception &e)
	{
		log_error(SCRIPTING_IMPL, "Exception in '%s': %s", pFilename, e.what());
	}
	catch(const std::string &e)
	{
		log_error(SCRIPTING_IMPL, "Exception in '%s': %s", pFilename, e.c_str());
	}
	catch(const chaiscript::Boxed_Value &e)
	{
		try
		{
			chaiscript::Boxed_Value ToStringRaw = m_pData->m_Chai.eval("to_string");
			std::function<std::string(chaiscript::Boxed_Value)> ToString =
				chaiscript::boxed_cast<std::function<std::string(chaiscript::Boxed_Value)>>(ToStringRaw);
			log_error(SCRIPTING_IMPL, "Exception in '%s': %s", pFilename, ToString(e).c_str());
		}
		catch(...)
		{
			log_error(SCRIPTING_IMPL, "Unknown exception while trying to print an error in '%s'", pFilename);
		}
	}
	catch(...)
	{
		log_error(SCRIPTING_IMPL, "Unknown exception in '%s'", pFilename);
	}
	std::free((void *)pScript);
}
