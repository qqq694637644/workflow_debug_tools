#include "../../Source/Helper.h"
#include "../../../Source/Library/WfLibraryPredefined.h"
#include "../../../Source/Library/WfLibraryReflection.h"
#include "../../../Source/Emitter/WfEmitter.h"

#if defined VCZH_MSVC
#include "../../../Source/WorkflowDebugHost/WorkflowDebugHost.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugSession.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugTransport.h"
#include <limits>
#endif

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

#if defined VCZH_MSVC && defined VCZH_DESCRIPTABLEOBJECT_WITH_METADATA
using namespace vl::workflow::debughost;
#endif

namespace
{
	struct ScriptCase
	{
		const wchar_t*			name;
		const wchar_t*			fileName;
		const wchar_t*			description;
		const wchar_t* const*	extraFileNames = nullptr;
		vint					extraFileCount = 0;
	};

	static const wchar_t* NestedCallsExtraFiles[] =
	{
		L"Scripts\\NestedCalls\\Level1.txt",
		L"Scripts\\NestedCalls\\Level2.txt",
		L"Scripts\\NestedCalls\\Level3.txt",
	};

#if defined VCZH_MSVC && defined VCZH_DESCRIPTABLEOBJECT_WITH_METADATA
	struct DebugLaunchOptions
	{
		bool		enabled = false;
		WString		host = L"127.0.0.1";
		vint		port = 4711;
		WString		sessionId = L"mytest";
	};

	static bool TryParseDebugPort(const WString& text, vint& port)
	{
		if (text.Length() == 0)
		{
			return false;
		}

		auto parsed = wtoi64(text);
		if (parsed <= 0 || parsed > 65535 || parsed > (std::numeric_limits<vint>::max)())
		{
			return false;
		}

		port = (vint)parsed;
		return true;
	}

	static bool TryParseDebugArgument(const WString& argument, DebugLaunchOptions& options)
	{
		const WString debugPrefix = L"--workflow-debug=";
		const WString hostPrefix = L"--workflow-debug-host=";
		const WString portPrefix = L"--workflow-debug-port=";
		const WString sessionPrefix = L"--workflow-debug-session=";

		if (argument == L"--workflow-debug")
		{
			options.enabled = true;
			return true;
		}
		if (argument.Length() >= debugPrefix.Length() && argument.Left(debugPrefix.Length()) == debugPrefix)
		{
			auto value = argument.Right(argument.Length() - debugPrefix.Length());
			if (value == L"0" || value == L"false" || value == L"False" || value == L"FALSE")
			{
				options.enabled = false;
				return true;
			}

			options.enabled = true;
			return true;
		}

		if (argument.Length() >= hostPrefix.Length() && argument.Left(hostPrefix.Length()) == hostPrefix)
		{
			options.host = argument.Right(argument.Length() - hostPrefix.Length());
			CHECK_ERROR(options.host.Length() > 0, L"调试宿主主机不能为空。");
			options.enabled = true;
			return true;
		}

		if (argument.Length() >= portPrefix.Length() && argument.Left(portPrefix.Length()) == portPrefix)
		{
			auto value = argument.Right(argument.Length() - portPrefix.Length());
			CHECK_ERROR(TryParseDebugPort(value, options.port), L"调试宿主端口必须是 1 到 65535 之间的正整数。");
			options.enabled = true;
			return true;
		}

		if (argument.Length() >= sessionPrefix.Length() && argument.Left(sessionPrefix.Length()) == sessionPrefix)
		{
			options.sessionId = argument.Right(argument.Length() - sessionPrefix.Length());
			CHECK_ERROR(options.sessionId.Length() > 0, L"调试会话标识不能为空。");
			options.enabled = true;
			return true;
		}

		return false;
	}

	static void CollectScriptFiles(const ScriptCase& scriptCase, List<WString>& fileNames)
	{
		fileNames.Clear();
		fileNames.Add(scriptCase.fileName);
		for (vint i = 0; i < scriptCase.extraFileCount; i++)
		{
			fileNames.Add(scriptCase.extraFileNames[i]);
		}
	}

	static DebugLaunchOptions ParseDebugLaunchOptions(int argc, wchar_t* argv[])
	{
		DebugLaunchOptions options;
		for (vint i = 1; i < argc; i++)
		{
			TryParseDebugArgument(argv[i], options);
		}
		return options;
	}

	struct DebugSessionGuard
	{
		Ptr<WorkflowDebugHost>		host;
		Ptr<WorkflowDebugSession>	session;

		void Reset()
		{
			if (session)
			{
				session->Detach();
				session = nullptr;
			}
			if (host)
			{
				host->Shutdown();
				host = nullptr;
			}
		}
	};

	static WorkflowDebugSession* gWorkflowDebugSession = nullptr;
#endif

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
		candidates.Add(WString(L"mytest\\") + relativeFileName);
		candidates.Add(WString(L"..\\") + relativeFileName);
		candidates.Add(WString(L"..\\..\\") + relativeFileName);
		candidates.Add(WString(L"..\\..\\..\\") + relativeFileName);
		candidates.Add(WString(L"..\\..\\mytest\\") + relativeFileName);

		for (auto candidate : candidates)
		{
			try
			{
				if (!File(candidate).Exists())
				{
					continue;
				}

				WString text;
				if (File(candidate).ReadAllTextByBom(text))
				{
					return text;
				}
			}
			catch (const Error&)
			{
			}
			catch (const Exception&)
			{
			}
		}

		CHECK_ERROR(false, (WString(L"找不到脚本文件： ") + relativeFileName).Buffer());
		return WString::Empty;
	}

#if defined VCZH_MSVC && defined VCZH_DESCRIPTABLEOBJECT_WITH_METADATA
	static WString ResolveScriptPath(const WString& relativeFileName)
	{
		List<WString> candidates;
		candidates.Add(relativeFileName);
		candidates.Add(WString(L"mytest\\") + relativeFileName);
		candidates.Add(WString(L"..\\") + relativeFileName);
		candidates.Add(WString(L"..\\..\\") + relativeFileName);
		candidates.Add(WString(L"..\\..\\..\\") + relativeFileName);
		candidates.Add(WString(L"..\\..\\mytest\\") + relativeFileName);

		for (auto candidate : candidates)
		{
			try
			{
				if (File(candidate).Exists())
				{
					return FilePath(candidate).GetFullPath();
				}
			}
			catch (const Error&)
			{
			}
			catch (const Exception&)
			{
			}
		}

		CHECK_ERROR(false, (WString(L"找不到脚本文件： ") + relativeFileName).Buffer());
		return WString::Empty;
	}

	static void BuildDebugSourceMap(const List<WString>& fileNames, List<WorkflowDebugSourceRecord>& sourceMap)
	{
		sourceMap.Clear();

		// 多文件脚本的 codeIndex 必须和 moduleCodes 的顺序完全一致，否则断点和调用栈会映射到错误文件。
		for (vint codeIndex = 0; codeIndex < fileNames.Count(); codeIndex++)
		{
			auto sourcePath = ResolveScriptPath(fileNames[codeIndex]);
			List<WString> lines;
			CHECK_ERROR(File(sourcePath).ReadAllLinesByBom(lines), L"读取脚本文件行数失败。");

			for (vint row = 0; row < lines.Count(); row++)
			{
				WorkflowDebugSourceRecord record;
				record.codeIndex = codeIndex;
				record.sourcePath = sourcePath;
				record.row = row;
				sourceMap.Add(record);
			}
		}
	}
#endif

	bool RunScriptCase(const ScriptCase& scriptCase)
	{
		List<WString> scriptFiles;
		CollectScriptFiles(scriptCase, scriptFiles);

		Console::WriteLine(L"");
		Console::WriteLine(L"==============================");
		Console::WriteLine(L"测试场景： " + WString(scriptCase.name));
		Console::WriteLine(L"文件路径： " + WString(scriptCase.fileName));
		for (vint i = 1; i < scriptFiles.Count(); i++)
		{
			Console::WriteLine(L"附加模块： " + scriptFiles[i]);
		}
		Console::WriteLine(L"说明： " + WString(scriptCase.description));
		Console::WriteLine(L"正在读取并编译脚本。");

		Parser parser;
		List<WString> moduleCodes;
		for (auto&& fileName : scriptFiles)
		{
			moduleCodes.Add(ReadScriptText(fileName));
		}

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
		Console::WriteLine(L"脚本编译完成。");

#if defined VCZH_MSVC && defined VCZH_DESCRIPTABLEOBJECT_WITH_METADATA
		if (gWorkflowDebugSession)
		{
			List<WorkflowDebugSourceRecord> sourceMap;
			BuildDebugSourceMap(scriptFiles, sourceMap);
			gWorkflowDebugSession->SetSourceMap(sourceMap);
			gWorkflowDebugSession->SetAssembly(assembly);
			Console::WriteLine(L"正在发送 Workflow 调试 hello。");
			CHECK_ERROR(gWorkflowDebugSession->SendHello(), L"发送 Workflow 调试 hello 失败。");
			CHECK_ERROR(gWorkflowDebugSession->WaitForReady(5000), L"等待 Workflow 调试器完成握手超时。");
			// LuaPanda 会在初始化完成后短暂等待断点同步，这里保持同样的节奏，避免第一行先于 F2 断点到达。
			gWorkflowDebugSession->WaitForBreakpointSync(1000);
			Console::WriteLine(L"Workflow 调试握手完成。");
		}
#endif

		auto context = Ptr(new WfRuntimeGlobalContext(assembly));
		Console::WriteLine(L"正在执行初始化函数。");
		LoadFunction<void()>(context, L"<initialize>")();
		Console::WriteLine(L"正在执行 main。");
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
		{L"NestedCalls",      L"Scripts\\NestedCalls\\Main.txt",L"多脚本嵌套调用。", NestedCallsExtraFiles, sizeof(NestedCallsExtraFiles) / sizeof(NestedCallsExtraFiles[0])},
		{L"BindSimple",       L"Scripts\\BindSimple.txt",       L"绑定表达式和观察式更新。"},
		{L"EvaluatePlayground", L"Scripts\\EvaluatePlayground.txt", L"调试求值、REPL 和 hover 验证。"},
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
	for (vint i = 1; i < argc; i++)
	{
		WString argument = argv[i];
		// 允许调试开关和脚本名自由排列，也兼容 copilotExecute 在 UnitTest 模式下传进来的 /C。
		if (argument.Left(2) == L"--" || argument.Left(1) == L"/")
		{
			continue;
		}
		selectedCase = argument;
		break;
	}
#endif
	auto typesLoaded = false;
	auto success = false;
#if defined VCZH_MSVC && defined VCZH_DESCRIPTABLEOBJECT_WITH_METADATA
	auto debugOptions = ParseDebugLaunchOptions(argc, argv);
	DebugSessionGuard debugSession;
	if (debugOptions.enabled && selectedCase.Length() == 0)
	{
		selectedCase = ScriptCases[0].name;
		Console::WriteLine(L"已启用 Workflow 调试但未指定脚本场景，默认使用 HelloWorld。");
	}
#endif
	try
	{
		LoadScriptTypes();
		typesLoaded = true;

#if defined VCZH_MSVC && defined VCZH_DESCRIPTABLEOBJECT_WITH_METADATA
		if (debugOptions.enabled)
		{
			Console::WriteLine(L"已启用 Workflow 调试宿主，准备连接 " + debugOptions.host + L":" + itow(debugOptions.port) + L"。");
			debugSession.host = Ptr(new WorkflowDebugHost);
			debugSession.host->Initialize();
			debugSession.session = debugSession.host->CreateSession(debugOptions.sessionId);
			debugSession.session->GetTransport()->SetEndpoint(debugOptions.host, debugOptions.port);
			debugSession.session->Attach();
			Console::WriteLine(L"Workflow 调试宿主已连接。");
			gWorkflowDebugSession = debugSession.session.Obj();
		}
#endif

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
	catch (const Error& ex)
	{
		Console::WriteLine(L"运行时发生错误： " + WString::Unmanaged(ex.Description()));
		success = false;
	}
	catch (const Exception& ex)
	{
		Console::WriteLine(L"运行时发生异常： " + ex.Message());
		success = false;
	}

#if defined VCZH_MSVC && defined VCZH_DESCRIPTABLEOBJECT_WITH_METADATA
	debugSession.Reset();
	gWorkflowDebugSession = nullptr;
#endif
	if (typesLoaded)
	{
		try
		{
			Console::WriteLine(L"正在卸载脚本类型。");
			UnloadScriptTypes();
			Console::WriteLine(L"脚本类型卸载完成。");
		}
		catch (const Exception& ex)
		{
			Console::WriteLine(L"卸载脚本类型时发生异常： " + ex.Message());
			success = false;
		}
	}
	Console::WriteLine(L"正在释放线程本地存储。");
	ThreadLocalStorage::DisposeStorages();
	Console::WriteLine(L"线程本地存储释放完成。");
	return success ? 0 : 1;
}
