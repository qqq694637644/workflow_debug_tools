#include "../../Source/Helper.h"
#include "../../../Source/Library/WfLibraryPredefined.h"
#include "../../../Source/Library/WfLibraryReflection.h"
#include "../../../Source/Emitter/WfEmitter.h"

using namespace vl;
using namespace vl::collections;
using namespace vl::console;
using namespace vl::glr;
using namespace vl::reflection;
using namespace vl::reflection::description;
using namespace vl::stream;
using namespace vl::filesystem;
using namespace vl::workflow;
using namespace vl::workflow::analyzer;
using namespace vl::workflow::emitter;
using namespace vl::workflow::runtime;

namespace
{
	struct ScriptCase
	{
		const wchar_t* name;
		const wchar_t* fileName;
		const wchar_t* description;
	};

	void LoadScriptTypes()
	{
		CHECK_ERROR(LoadPredefinedTypes(), L"加载预定义类型失败。");
		CHECK_ERROR(WfLoadLibraryTypes(), L"加载 Workflow 库类型失败。");
		CHECK_ERROR(GetGlobalTypeManager()->Load(), L"加载 Workflow 类型失败。");
	}

	void UnloadScriptTypes()
	{
		CHECK_ERROR(GetGlobalTypeManager()->Unload(), L"卸载 Workflow 类型失败。");
		CHECK_ERROR(ResetGlobalTypeManager(), L"重置全局类型管理器失败。");
	}

	WString ReadScriptText(const WString& relativeFileName)
	{
		List<WString> candidates;
		candidates.Add(relativeFileName);
		candidates.Add(WString(L"..\\") + relativeFileName);
		candidates.Add(WString(L"..\\..\\") + relativeFileName);
		candidates.Add(WString(L"..\\..\\..\\") + relativeFileName);

		for (auto candidate : candidates)
		{
			try
			{
				FileStream fileStream(candidate, FileStream::ReadOnly);
				BomDecoder decoder;
				DecoderStream decoderStream(fileStream, decoder);
				StreamReader reader(decoderStream);

				WString text;
				while (!reader.IsEnd())
				{
					text += reader.ReadLine();
					if (!reader.IsEnd())
					{
						text += L"\r\n";
					}
				}
				return text;
			}
			catch (const Exception&)
			{
			}
		}

		CHECK_ERROR(false, (WString(L"找不到脚本文件： ") + relativeFileName).Buffer());
		return WString::Empty;
	}

	bool RunScriptCase(const ScriptCase& scriptCase)
	{
		Console::WriteLine(L"");
		Console::WriteLine(L"==============================");
		Console::WriteLine(L"测试场景： " + WString(scriptCase.name));
		Console::WriteLine(L"文件路径： " + WString(scriptCase.fileName));
		Console::WriteLine(L"说明： " + WString(scriptCase.description));

		Parser parser;
		List<WString> moduleCodes;
		moduleCodes.Add(ReadScriptText(scriptCase.fileName));

		List<ParsingError> errors;
		auto assembly = Compile(parser, WfCpuArchitecture::AsExecutable, moduleCodes, errors);
		if (!assembly)
		{
			Console::WriteLine(L"脚本编译失败，错误如下：");
			for (auto&& error : errors)
			{
				Console::WriteLine(L"  " + error.message);
			}
			return false;
		}

		auto context = Ptr(new WfRuntimeGlobalContext(assembly));
		LoadFunction<void()>(context, L"<initialize>")();
		auto result = LoadFunction<WString()>(context, L"main")();
		Console::WriteLine(L"脚本返回： " + result);
		return true;
	}

	const ScriptCase ScriptCases[] =
	{
		{L"HelloWorld",       L"Scripts\\HelloWorld.txt",       L"最小可运行脚本。"},
		{L"IfElse",           L"Scripts\\IfElse.txt",           L"条件分支。"},
		{L"Loop",             L"Scripts\\Loop.txt",             L"循环和累加。"},
		{L"ClassMethod",      L"Scripts\\ClassMethod.txt",      L"类、方法、属性和事件。"},
		{L"ClassCtor",        L"Scripts\\ClassCtor.txt",        L"构造函数和继承。"},
		{L"TryCatch",         L"Scripts\\TryCatch.txt",         L"异常、捕获和 finally。"},
		{L"BindSimple",       L"Scripts\\BindSimple.txt",       L"绑定表达式和观察式更新。"},
	};
}

#if defined VCZH_MSVC
int wmain(int argc, wchar_t* argv[])
#elif defined VCZH_GCC
int main(int argc, char* argv[])
#endif
{
	WString selectedCase = L"";
#if defined VCZH_MSVC
	if (argc > 1)
	{
		selectedCase = argv[1];
	}
#endif

	auto typesLoaded = false;
	auto success = false;
	try
	{
		LoadScriptTypes();
		typesLoaded = true;

		auto matched = false;
		success = true;
		for (auto&& scriptCase : ScriptCases)
		{
			if (selectedCase.Length() > 0 && selectedCase != scriptCase.name && selectedCase != scriptCase.fileName)
			{
				continue;
			}

			matched = true;
			success = RunScriptCase(scriptCase) && success;
		}

		if (!matched)
		{
			Console::WriteLine(L"没有找到匹配的脚本场景。");
			Console::WriteLine(L"可用场景：");
			for (auto&& scriptCase : ScriptCases)
			{
				Console::WriteLine(L"  " + WString(scriptCase.name) + L" -> " + WString(scriptCase.fileName));
			}
			success = false;
		}
	}
	catch (const Exception& ex)
	{
		Console::WriteLine(L"运行时发生异常： " + ex.Message());
	}

	if (typesLoaded)
	{
		UnloadScriptTypes();
	}
	ThreadLocalStorage::DisposeStorages();
	return success ? 0 : 1;
}
