#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

#include "../../Source/Helper.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugBreakpointRegistry.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugBridge.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugHost.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugRuntimeBinding.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugSession.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugSessionState.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugSourceCatalog.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugStackInspector.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugTransport.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugValueInspector.h"

using namespace vl::workflow::debughost;

static bool ContainsSubstring(const WString& text, const WString& substring)
{
	return INVLOC.FindFirst(text, substring, Locale::Normalization::None).key != -1;
}

static bool SendAllBytes(SOCKET socket, const char* data, vint size)
{
	vint total = 0;
	while (total < size)
	{
		auto sent = send(socket, data + total, (int)(size - total), 0);
		if (sent <= 0)
		{
			return false;
		}
		total += sent;
	}
	return true;
}

static bool SendUtf8Line(SOCKET socket, const WString& line)
{
	auto utf8 = wtou8(line + L"\n");
	return SendAllBytes(socket, reinterpret_cast<const char*>(utf8.Buffer()), utf8.Length());
}

static bool ReadUtf8Line(SOCKET socket, WString& line)
{
	std::string buffer;
	char chunk[256];
	while (true)
	{
		auto received = recv(socket, chunk, sizeof(chunk), 0);
		if (received <= 0)
		{
			return false;
		}

		buffer.append(chunk, received);
		auto newline = buffer.find('\n');
		if (newline != std::string::npos)
		{
			auto raw = buffer.substr(0, newline);
			if (!raw.empty() && raw.back() == '\r')
			{
				raw.pop_back();
			}
			std::u8string utf8Line(raw.begin(), raw.end());
			line = u8tow(U8String::Unmanaged(utf8Line.c_str()));
			return true;
		}
	}
}

static bool WaitForEnvelope(WorkflowDebugTransport& transport, WorkflowDebugEnvelope& envelope, vint timeoutMilliseconds)
{
	auto begin = std::chrono::steady_clock::now();
	while (true)
	{
		if (transport.TryReceive(envelope))
		{
			return true;
		}

		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
		if (elapsed >= timeoutMilliseconds)
		{
			return false;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

static bool WaitForFlag(std::atomic<bool>& flag, vint timeoutMilliseconds)
{
	auto begin = std::chrono::steady_clock::now();
	while (true)
	{
		if (flag.load())
		{
			return true;
		}

		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
		if (elapsed >= timeoutMilliseconds)
		{
			return false;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

static SOCKET CreateLoopbackListener(vint& port)
{
	WSADATA wsaData;
	TEST_ASSERT(WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);

	auto listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	TEST_ASSERT(listenSocket != INVALID_SOCKET);

	sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(0);

	TEST_ASSERT(bind(listenSocket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
	TEST_ASSERT(listen(listenSocket, 1) == 0);

	int addressSize = sizeof(address);
	TEST_ASSERT(getsockname(listenSocket, reinterpret_cast<sockaddr*>(&address), &addressSize) == 0);
	port = ntohs(address.sin_port);
	return listenSocket;
}

class TestRemoteWfDebugger : public RemoteWfDebugger
{
public:
	bool WaitForContinueOnce()
	{
		return WaitForContinue();
	}
};

TEST_FILE
{
	TEST_CASE(L"WorkflowDebugHost 会话管理")
	{
		WorkflowDebugHost host;
		host.Initialize();

		TEST_ASSERT(host.GetSessionCount() == 0);

		auto session = host.CreateSession(L"wf-1");
		TEST_ASSERT(session);
		TEST_ASSERT(host.GetSessionCount() == 1);
		TEST_ASSERT(host.FindSession(L"wf-1") == session);
		TEST_ASSERT(host.CreateSession(L"wf-1") == session);

		TEST_ASSERT(host.DestroySession(L"wf-1") == true);
		TEST_ASSERT(host.GetSessionCount() == 0);
		TEST_ASSERT(host.FindSession(L"wf-1") == nullptr);

		host.Shutdown();
	});

	TEST_CASE(L"WorkflowDebugSessionState 复位")
	{
		WorkflowDebugSessionState state;
		auto snapshot = state.Snapshot();
		TEST_ASSERT(snapshot.phase == WorkflowDebugSessionPhase::Idle);
		TEST_ASSERT(snapshot.pendingRequestCount == 0);
		TEST_ASSERT(snapshot.sessionId == L"");

		state.Attach(L"wf-2");
		state.SetWorkspaceRoot(L"D:/workspace");
		state.SetSourceMapCount(3);
		state.SetLastInboundSeq(4);
		state.SetLastOutboundSeq(5);
		state.IncrementPendingRequestCount();
		state.SetLastStopped(L"breakpoint", 1, 2, 3, 4);

		snapshot = state.Snapshot();
		TEST_ASSERT(snapshot.sessionId == L"wf-2");
		TEST_ASSERT(snapshot.phase == WorkflowDebugSessionPhase::Connected);
		TEST_ASSERT(snapshot.workspaceRoot == L"D:/workspace");
		TEST_ASSERT(snapshot.sourceMapCount == 3);
		TEST_ASSERT(snapshot.lastInboundSeq == 4);
		TEST_ASSERT(snapshot.lastOutboundSeq == 5);
		TEST_ASSERT(snapshot.pendingRequestCount == 1);
		TEST_ASSERT(snapshot.lastStoppedReason == L"breakpoint");
		TEST_ASSERT(snapshot.lastStoppedThreadId == 1);
		TEST_ASSERT(snapshot.lastStoppedFrameId == 2);
		TEST_ASSERT(snapshot.lastStoppedSourceId == 3);
		TEST_ASSERT(snapshot.lastStoppedRow == 4);

		state.Detach();
		snapshot = state.Snapshot();
		TEST_ASSERT(snapshot.phase == WorkflowDebugSessionPhase::Closed);
		TEST_ASSERT(snapshot.pendingRequestCount == 0);
		TEST_ASSERT(snapshot.lastStoppedReason == L"");
	});

	TEST_CASE(L"WorkflowDebugSourceCatalog 和断点登记")
	{
		WorkflowDebugSourceCatalog catalog;
		TEST_ASSERT(catalog.RegisterSource(7, L"D:/src/main.wf", 12));

		WString resolvedPath;
		vint resolvedRow = -1;
		TEST_ASSERT(catalog.ResolveByCodeIndex(7, resolvedPath, resolvedRow) == true);
		TEST_ASSERT(resolvedPath == L"D:/src/main.wf");
		TEST_ASSERT(resolvedRow == 12);

		vint resolvedCodeIndex = -1;
		TEST_ASSERT(catalog.ResolveByPath(L"D:/src/main.wf", resolvedCodeIndex, resolvedRow) == true);
		TEST_ASSERT(resolvedCodeIndex == 7);
		TEST_ASSERT(resolvedRow == 12);

		WorkflowDebugBreakpointRegistry registry(&catalog);
		WorkflowDebugBreakpointRecord breakpoint;
		breakpoint.sourcePath = L"D:/src/main.wf";
		breakpoint.row = 28;
		breakpoint.condition = L"x > 0";
		breakpoint.logMessage = L"hit";

		auto breakpointId = registry.RegisterBreakpoint(breakpoint);
		TEST_ASSERT(breakpointId == 0);
		TEST_ASSERT(registry.Count() == 1);

		const auto& breakpoints = registry.GetBreakpoints();
		TEST_ASSERT(breakpoints[0].codeIndex == 7);
		TEST_ASSERT(breakpoints[0].row == 28);
		TEST_ASSERT(breakpoints[0].verified == true);
		TEST_ASSERT(breakpoints[0].reason == L"");
		TEST_ASSERT(registry.HasBreakpoint(7, 28) == true);
		TEST_ASSERT(registry.HasBreakpoint(7, 12) == false);
	});

	TEST_CASE(L"WorkflowDebugTransport 收发")
	{
		WorkflowDebugTransport transport;
		TEST_ASSERT(transport.IsOpen() == false);

		WorkflowDebugEnvelope envelope;
		envelope.kind = WorkflowDebugEnvelopeKind::Event;
		envelope.command = L"hello";
		envelope.sessionId = L"wf-3";
		envelope.seq = 1;
		envelope.replyTo = 0;
		envelope.body = L"{\"runtimeVersion\":\"1\"}";

		TEST_ASSERT(transport.Send(envelope) == false);
		TEST_ASSERT(transport.Open() == true);
		TEST_ASSERT(transport.IsOpen() == true);
		TEST_ASSERT(transport.Send(envelope) == true);

		transport.QueueIncoming(envelope);
		WorkflowDebugEnvelope received;
		TEST_ASSERT(transport.TryReceive(received) == true);
		TEST_ASSERT(received.command == L"hello");
		TEST_ASSERT(received.seq == 1);
		TEST_ASSERT(received.sessionId == L"wf-3");

		transport.Close();
		TEST_ASSERT(transport.IsOpen() == false);
	});

	TEST_CASE(L"WorkflowDebugRuntimeBinding 入口暂停")
	{
		Ptr<TestRemoteWfDebugger> debugger = Ptr(new TestRemoteWfDebugger);
		Ptr<runtime::WfDebugger> debuggerBase = debugger;
		WorkflowDebugRuntimeBinding binding;
		binding.Bind(debuggerBase);

		TEST_ASSERT(binding.RequestStopOnEntry() == true);
		TEST_ASSERT(debugger->GetState() == runtime::WfDebugger::RequiredToPause);

		binding.Unbind();
	});

	TEST_CASE(L"WorkflowDebugTransport TCP 收发")
	{
		vint port = 0;
		auto listenSocket = CreateLoopbackListener(port);

		WString serverReceivedLine;
		std::atomic<bool> serverFailed = false;
		std::atomic<bool> serverSucceeded = false;
		std::thread serverThread([&]()
		{
			auto clientSocket = accept(listenSocket, nullptr, nullptr);
			if (clientSocket == INVALID_SOCKET)
			{
				serverFailed = true;
				return;
			}

			// 给客户端一点时间把接收线程拉起来，避免把分包测试误判成连接时序问题。
			std::this_thread::sleep_for(std::chrono::milliseconds(50));

			WString firstEvent = L"{\"type\":\"event\",\"seq\":101,\"sessionId\":\"wf-net\",\"cmd\":\"output\",\"body\":{\"level\":\"info\",\"message\":\"first\"}}";
			auto firstUtf8 = wtou8(firstEvent + L"\n");
			auto split = firstUtf8.Length() / 2;
			if (!SendAllBytes(clientSocket, reinterpret_cast<const char*>(firstUtf8.Buffer()), split))
			{
				serverFailed = true;
				shutdown(clientSocket, SD_BOTH);
				closesocket(clientSocket);
				return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
			if (!SendAllBytes(clientSocket, reinterpret_cast<const char*>(firstUtf8.Buffer()) + split, firstUtf8.Length() - split))
			{
				serverFailed = true;
				shutdown(clientSocket, SD_BOTH);
				closesocket(clientSocket);
				return;
			}

			if (!ReadUtf8Line(clientSocket, serverReceivedLine))
			{
				serverFailed = true;
				shutdown(clientSocket, SD_BOTH);
				closesocket(clientSocket);
				return;
			}

			WString secondEvent = L"{\"type\":\"event\",\"seq\":102,\"sessionId\":\"wf-net\",\"cmd\":\"stopped\",\"body\":{\"reason\":\"breakpoint\"}}";
			if (!SendUtf8Line(clientSocket, secondEvent))
			{
				serverFailed = true;
				shutdown(clientSocket, SD_BOTH);
				closesocket(clientSocket);
				return;
			}

			serverSucceeded = true;
			shutdown(clientSocket, SD_BOTH);
			closesocket(clientSocket);
		});

		WorkflowDebugTransport transport;
		bool connectSucceeded = transport.Connect(L"127.0.0.1", port);
		bool sendSucceeded = false;
		bool firstReceiveSucceeded = false;
		bool secondReceiveSucceeded = false;
		bool requestLineMatched = false;

		WorkflowDebugEnvelope request;
		request.kind = WorkflowDebugEnvelopeKind::Request;
		request.command = L"hello";
		request.sessionId = L"wf-net";
		request.seq = 7;
		request.replyTo = 1;
		request.body = L"{\"workspaceRoot\":\"D:/workspace\"}";

		if (connectSucceeded)
		{
			sendSucceeded = transport.Send(request);
		}

		WorkflowDebugEnvelope firstReceived;
		WorkflowDebugEnvelope secondReceived;
		if (connectSucceeded && sendSucceeded)
		{
			firstReceiveSucceeded = WaitForEnvelope(transport, firstReceived, 2000);
			secondReceiveSucceeded = WaitForEnvelope(transport, secondReceived, 2000);
		}

		if (firstReceiveSucceeded)
		{
			firstReceiveSucceeded = firstReceived.kind == WorkflowDebugEnvelopeKind::Event
				&& firstReceived.command == L"output"
				&& firstReceived.sessionId == L"wf-net"
				&& firstReceived.seq == 101
				&& firstReceived.body == L"{\"level\":\"info\",\"message\":\"first\"}";
		}

		if (secondReceiveSucceeded)
		{
			secondReceiveSucceeded = secondReceived.kind == WorkflowDebugEnvelopeKind::Event
				&& secondReceived.command == L"stopped"
				&& secondReceived.sessionId == L"wf-net"
				&& secondReceived.seq == 102
				&& secondReceived.body == L"{\"reason\":\"breakpoint\"}";
		}

		transport.Close();
		TEST_ASSERT(transport.IsOpen() == false);

		closesocket(listenSocket);
		if (serverThread.joinable())
		{
			serverThread.join();
		}
		WSACleanup();

		requestLineMatched = ContainsSubstring(serverReceivedLine, WString::Unmanaged(L"\"type\":\"request\""))
			&& ContainsSubstring(serverReceivedLine, WString::Unmanaged(L"\"cmd\":\"hello\""))
			&& ContainsSubstring(serverReceivedLine, WString::Unmanaged(L"\"sessionId\":\"wf-net\""))
			&& ContainsSubstring(serverReceivedLine, WString::Unmanaged(L"\"replyTo\":1"))
			&& ContainsSubstring(serverReceivedLine, WString::Unmanaged(L"\"workspaceRoot\":\"D:\\/workspace\""));

		TEST_ASSERT(connectSucceeded == true);
		TEST_ASSERT(sendSucceeded == true);
		TEST_ASSERT(firstReceiveSucceeded == true);
		TEST_ASSERT(secondReceiveSucceeded == true);
		TEST_ASSERT(serverFailed.load() == false);
		TEST_ASSERT(serverSucceeded.load() == true);
		TEST_ASSERT(requestLineMatched == true);
	});

	TEST_CASE(L"WorkflowDebugStackInspector 和变量检查器")
	{
		WorkflowDebugStackInspector stackInspector;
		WorkflowDebugValueInspector valueInspector;

		collections::List<WorkflowDebugStackFrame> frames;
		WorkflowDebugStackFrame frame0;
		frame0.threadId = 1;
		frame0.frameId = 0;
		frame0.sourceId = 7;
		frame0.functionName = L"Main";
		frame0.sourcePath = L"D:/src/main.wf";
		frame0.row = 12;
		frame0.column = 3;

		WorkflowDebugStackFrame frame1 = frame0;
		frame1.frameId = 1;
		frame1.functionName = L"Helper";
		frame1.row = 20;

		frames.Add(frame0);
		frames.Add(frame1);
		stackInspector.CaptureStack(1, frames);

		collections::List<WorkflowDebugStackFrame> loadedFrames;
		TEST_ASSERT(stackInspector.TryGetFrames(1, loadedFrames) == true);
		TEST_ASSERT(loadedFrames.Count() == 2);
		TEST_ASSERT(loadedFrames[0].functionName == L"Main");
		TEST_ASSERT(loadedFrames[1].functionName == L"Helper");

		WorkflowDebugStackFrame loadedFrame;
		TEST_ASSERT(stackInspector.TryGetFrame(1, 1, loadedFrame) == true);
		TEST_ASSERT(loadedFrame.functionName == L"Helper");

		WorkflowDebugStackFrame unknownFrame = frame0;
		unknownFrame.frameId = 2;
		unknownFrame.sourceId = -1;
		unknownFrame.sourcePath = L"unknown://source/frame/2";
		unknownFrame.functionName = L"Unknown";
		unknownFrame.row = 0;
		unknownFrame.column = 0;
		frames.Add(unknownFrame);
		stackInspector.CaptureStack(1, frames);

		TEST_ASSERT(stackInspector.TryGetFrame(1, 2, loadedFrame) == true);
		TEST_ASSERT(loadedFrame.sourceId == -1);
		TEST_ASSERT(loadedFrame.sourcePath == L"unknown://source/frame/2");

		WorkflowDebugFrameValues values;
		WorkflowDebugVariable local;
		local.name = L"localValue";
		local.type = L"vint";
		local.value = L"42";
		values.local.Add(local);

		WorkflowDebugVariable argument = local;
		argument.name = L"argumentValue";
		values.argument.Add(argument);

		WorkflowDebugVariable captured = local;
		captured.name = L"capturedValue";
		values.captured.Add(captured);

		WorkflowDebugVariable global = local;
		global.name = L"globalValue";
		values.global.Add(global);

		valueInspector.CaptureFrame(1, 1, values);

		collections::List<WorkflowDebugScope> scopes;
		TEST_ASSERT(valueInspector.TryGetScopes(1, 1, scopes) == true);
		TEST_ASSERT(scopes.Count() == 4);
		TEST_ASSERT(scopes[0].kind == WorkflowDebugScopeKind::Local);
		TEST_ASSERT(scopes[0].variablesReference > 0);
		TEST_ASSERT(scopes[0].canExpand == true);
		TEST_ASSERT(scopes[1].kind == WorkflowDebugScopeKind::Argument);
		TEST_ASSERT(scopes[1].variablesReference > 0);
		TEST_ASSERT(scopes[1].canExpand == true);
		TEST_ASSERT(scopes[2].kind == WorkflowDebugScopeKind::Captured);
		TEST_ASSERT(scopes[2].variablesReference > 0);
		TEST_ASSERT(scopes[2].canExpand == true);
		TEST_ASSERT(scopes[3].kind == WorkflowDebugScopeKind::Global);
		TEST_ASSERT(scopes[3].variablesReference > 0);
		TEST_ASSERT(scopes[3].canExpand == true);

		collections::List<WorkflowDebugVariable> loadedVariables;
		TEST_ASSERT(valueInspector.TryGetVariables(1, 1, WorkflowDebugScopeKind::Local, loadedVariables) == true);
		TEST_ASSERT(loadedVariables.Count() == 1);
		TEST_ASSERT(loadedVariables[0].name == L"localValue");
		TEST_ASSERT(loadedVariables[0].value == L"42");

		TEST_ASSERT(valueInspector.TryGetVariables(1, 1, WorkflowDebugScopeKind::Argument, loadedVariables) == true);
		TEST_ASSERT(loadedVariables.Count() == 1);
		TEST_ASSERT(loadedVariables[0].name == L"argumentValue");
	});

	TEST_CASE(L"WorkflowDebugRuntimeBinding 断点刷新")
	{
		auto CreateThreadContextFromSample = [](const WString& name)
		{
			List<WString> moduleCodes;
			moduleCodes.Add(LoadSample(L"Debugger", name));
			List<glr::ParsingError> errors;
			auto assembly = Compile(GetWorkflowParser(), WfCpuArchitecture::AsExecutable, moduleCodes, errors);
			TEST_ASSERT(assembly && errors.Count() == 0);
			return Ptr(new WfRuntimeGlobalContext(assembly));
		};

		WorkflowDebugSourceCatalog catalog;
		TEST_ASSERT(catalog.RegisterSource(0, L"D:/src/Assignment.wf", 0));

		WorkflowDebugBreakpointRegistry registry(&catalog);
		WorkflowDebugBreakpointRecord breakpoint;
		breakpoint.breakpointId = L"wf-bp-1";
		breakpoint.sourcePath = L"D:/src/Assignment.wf";
		breakpoint.codeIndex = 0;
		breakpoint.row = 5;
		auto breakpointIndex = registry.RegisterBreakpoint(breakpoint);
		TEST_ASSERT(breakpointIndex == 0);
		TEST_ASSERT(registry.GetBreakpoints()[0].verified == true);

		Ptr<TestRemoteWfDebugger> debugger = Ptr(new TestRemoteWfDebugger);
		Ptr<runtime::WfDebugger> debuggerBase = debugger;
		WorkflowDebugRuntimeBinding binding;
		binding.Bind(debuggerBase);
		binding.AttachDebugData(nullptr, &catalog, &registry, nullptr, nullptr, nullptr);

		auto context = CreateThreadContextFromSample(L"Assignment");
		binding.SetAssembly(context->assembly);

		TEST_ASSERT(debugger->GetBreakPointCount() == 1);
		const auto& installed = debugger->GetBreakPoint(0);
		TEST_ASSERT(installed.available == true);
		TEST_ASSERT(installed.type == runtime::WfBreakPoint::Instruction);
		TEST_ASSERT(installed.assembly == context->assembly.Obj());

		binding.Unbind();
	});

	TEST_CASE(L"WorkflowDebugSession 任务流")
	{
		WorkflowDebugSession session(L"wf-session");
		session.Attach();

		TEST_ASSERT(session.GetState()->GetPhase() == WorkflowDebugSessionPhase::Connected);
		TEST_ASSERT(session.GetTransport()->IsOpen() == true);
		TEST_ASSERT(session.GetRuntimeBinding()->IsBound() == true);
		TEST_ASSERT(session.GetRuntimeBinding()->GetRemoteDebugger() != nullptr);

		WorkflowDebugEnvelope hello;
		hello.kind = WorkflowDebugEnvelopeKind::Request;
		hello.command = L"hello";
		hello.sessionId = L"wf-session";
		hello.seq = 1;

		TEST_ASSERT(session.Dispatch(hello) == true);
		TEST_ASSERT(session.GetState()->GetPhase() == WorkflowDebugSessionPhase::Negotiating);

		WorkflowDebugEnvelope initialize = hello;
		initialize.command = L"initialize";
		initialize.seq = 2;
		TEST_ASSERT(session.Dispatch(initialize) == true);
		TEST_ASSERT(session.GetState()->GetPhase() == WorkflowDebugSessionPhase::Ready);

		WorkflowDebugEnvelope exception = hello;
		exception.kind = WorkflowDebugEnvelopeKind::Event;
		exception.command = L"exception";
		exception.seq = 3;
		TEST_ASSERT(session.Dispatch(exception) == true);
		TEST_ASSERT(session.GetState()->GetPhase() == WorkflowDebugSessionPhase::Paused);
		TEST_ASSERT(session.GetState()->Snapshot().lastStoppedReason == L"exception");

		session.Detach();
		TEST_ASSERT(session.GetState()->GetPhase() == WorkflowDebugSessionPhase::Closed);
		TEST_ASSERT(session.GetTransport()->IsOpen() == false);
	});

	TEST_CASE(L"WorkflowDebugSession 断开通知")
	{
		WorkflowDebugSession session(L"wf-disconnect");
		session.Attach();

		session.Detach();

		WorkflowDebugEnvelope disconnect;
		TEST_ASSERT(session.GetTransport()->TryPopOutgoing(disconnect) == true);
		TEST_ASSERT(disconnect.kind == WorkflowDebugEnvelopeKind::Event);
		TEST_ASSERT(disconnect.command == L"disconnect");
		TEST_ASSERT(ContainsSubstring(disconnect.body, L"\"reason\":\"会话关闭\""));
		TEST_ASSERT(ContainsSubstring(disconnect.body, L"\"restart\":false"));
	});

	TEST_CASE(L"WorkflowDebugSession 端点连接")
	{
		vint port = 0;
		auto listenSocket = CreateLoopbackListener(port);
		std::atomic<bool> accepted = false;
		std::atomic<bool> readyReceived = false;
		WString helloLine;
		WString readyLine;
		std::thread serverThread([&]()
		{
			auto clientSocket = accept(listenSocket, nullptr, nullptr);
			if (clientSocket == INVALID_SOCKET)
			{
				return;
			}

			accepted = true;
			if (!ReadUtf8Line(clientSocket, helloLine))
			{
				shutdown(clientSocket, SD_BOTH);
				closesocket(clientSocket);
				return;
			}

			WString initialize = L"{\"type\":\"request\",\"seq\":2,\"sessionId\":\"wf-connect\",\"cmd\":\"initialize\",\"body\":{\"workspaceRoot\":\"D:/workspace\"}}";
			if (!SendUtf8Line(clientSocket, initialize))
			{
				shutdown(clientSocket, SD_BOTH);
				closesocket(clientSocket);
				return;
			}

			if (!ReadUtf8Line(clientSocket, readyLine))
			{
				shutdown(clientSocket, SD_BOTH);
				closesocket(clientSocket);
				return;
			}

			readyReceived = true;
			shutdown(clientSocket, SD_BOTH);
			closesocket(clientSocket);
		});

		WorkflowDebugSession session(L"wf-connect");
		collections::List<WorkflowDebugSourceRecord> sourceMap;
		WorkflowDebugSourceRecord source0;
		source0.codeIndex = 0;
		source0.sourcePath = L"D:/workspace/Scripts/HelloWorld.txt";
		source0.row = 0;
		sourceMap.Add(source0);
		WorkflowDebugSourceRecord source1 = source0;
		source1.row = 1;
		sourceMap.Add(source1);
		session.SetSourceMap(sourceMap);
		session.GetTransport()->SetEndpoint(L"127.0.0.1", port);
		session.Attach();
		TEST_ASSERT(session.SendHello() == true);

		TEST_ASSERT(WaitForFlag(accepted, 2000) == true);
		TEST_ASSERT(session.WaitForReady(2000) == true);
		TEST_ASSERT(WaitForFlag(readyReceived, 2000) == true);
		TEST_ASSERT(ContainsSubstring(helloLine, L"\"cmd\":\"hello\""));
		TEST_ASSERT(ContainsSubstring(helloLine, L"\"sourceMap\""));
		TEST_ASSERT(ContainsSubstring(helloLine, L"HelloWorld.txt"));
		TEST_ASSERT(ContainsSubstring(helloLine, L"\"codeIndex\":0"));
		TEST_ASSERT(ContainsSubstring(readyLine, L"\"cmd\":\"ready\""));
		TEST_ASSERT(ContainsSubstring(readyLine, L"\"accepted\":true"));

		session.Detach();

		closesocket(listenSocket);
		if (serverThread.joinable())
		{
			serverThread.join();
		}
		WSACleanup();
	});

	TEST_CASE(L"WorkflowDebugBridge 返回暂停数据")
	{
		WorkflowDebugSession session(L"wf-stack");
		session.Attach();

		collections::List<WorkflowDebugStackFrame> frames;
		WorkflowDebugStackFrame frame0;
		frame0.threadId = 0;
		frame0.frameId = 0;
		frame0.sourceId = 7;
		frame0.functionName = L"Main";
		frame0.sourcePath = L"D:/src/main.wf";
		frame0.row = 12;
		frame0.column = 3;

		WorkflowDebugStackFrame frame1 = frame0;
		frame1.frameId = 1;
		frame1.functionName = L"Helper";
		frame1.row = 20;

		frames.Add(frame0);
		frames.Add(frame1);
		session.GetStackInspector()->CaptureStack(0, frames);

		WorkflowDebugFrameValues values;
		WorkflowDebugVariable local;
		local.name = L"localValue";
		local.type = L"vint";
		local.value = L"42";
		values.local.Add(local);

		WorkflowDebugVariable argument = local;
		argument.name = L"argumentValue";
		values.argument.Add(argument);

		WorkflowDebugVariable captured = local;
		captured.name = L"capturedValue";
		values.captured.Add(captured);

		WorkflowDebugVariable global = local;
		global.name = L"globalValue";
		values.global.Add(global);

		session.GetValueInspector()->CaptureFrame(0, 1, values);
		session.GetState()->SetPhase(WorkflowDebugSessionPhase::Paused);
		session.GetState()->SetLastStopped(L"breakpoint", 0, 1, 7, 20);

		WorkflowDebugEnvelope stackTrace;
		stackTrace.kind = WorkflowDebugEnvelopeKind::Request;
		stackTrace.command = L"stackTrace";
		stackTrace.sessionId = L"wf-stack";
		stackTrace.seq = 10;
		stackTrace.body = L"{\"threadId\":0,\"startFrame\":0,\"levels\":20}";
		TEST_ASSERT(session.Dispatch(stackTrace) == true);

		WorkflowDebugEnvelope stackTraceResponse;
		TEST_ASSERT(session.GetTransport()->TryPopOutgoing(stackTraceResponse) == true);
		TEST_ASSERT(stackTraceResponse.kind == WorkflowDebugEnvelopeKind::Response);
		TEST_ASSERT(stackTraceResponse.replyTo == 10);
		TEST_ASSERT(stackTraceResponse.command == L"stackTrace");
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"levels\":20"));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"totalFrames\":2"));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"callStackIndex\":1"));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"callStackIndex\":0"));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"line\":21"));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"line\":13"));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"column\":4"));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"canRequestVariables\":true"));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"functionName\":\"Main\""));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"functionName\":\"Helper\""));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"sourcePath\":\"D:\\/src\\/main.wf\""));

		WorkflowDebugEnvelope scopes = stackTrace;
		scopes.command = L"scopes";
		scopes.seq = 11;
		scopes.body = L"{\"frameId\":1}";
		TEST_ASSERT(session.Dispatch(scopes) == true);

		WorkflowDebugEnvelope scopesResponse;
		TEST_ASSERT(session.GetTransport()->TryPopOutgoing(scopesResponse) == true);
		TEST_ASSERT(scopesResponse.kind == WorkflowDebugEnvelopeKind::Response);
		TEST_ASSERT(scopesResponse.replyTo == 11);
		TEST_ASSERT(ContainsSubstring(scopesResponse.body, L"\"scopes\""));
		TEST_ASSERT(ContainsSubstring(scopesResponse.body, L"\"Local\""));
		TEST_ASSERT(ContainsSubstring(scopesResponse.body, L"\"Argument\""));
		TEST_ASSERT(ContainsSubstring(scopesResponse.body, L"\"Captured\""));
		TEST_ASSERT(ContainsSubstring(scopesResponse.body, L"\"Global\""));
		TEST_ASSERT(ContainsSubstring(scopesResponse.body, L"\"canExpand\":true"));

		WorkflowDebugEnvelope variables = stackTrace;
		variables.command = L"variables";
		variables.seq = 12;
		variables.body = L"{\"variablesReference\":1,\"frameId\":1,\"scopeKind\":\"Local\"}";
		TEST_ASSERT(session.Dispatch(variables) == true);

		WorkflowDebugEnvelope variablesResponse;
		TEST_ASSERT(session.GetTransport()->TryPopOutgoing(variablesResponse) == true);
		TEST_ASSERT(variablesResponse.kind == WorkflowDebugEnvelopeKind::Response);
		TEST_ASSERT(variablesResponse.replyTo == 12);
		TEST_ASSERT(ContainsSubstring(variablesResponse.body, L"\"variablesReference\":1"));
		TEST_ASSERT(ContainsSubstring(variablesResponse.body, L"\"variables\""));
		TEST_ASSERT(ContainsSubstring(variablesResponse.body, L"\"localValue\""));
		TEST_ASSERT(ContainsSubstring(variablesResponse.body, L"\"42\""));
		TEST_ASSERT(ContainsSubstring(variablesResponse.body, L"\"canExpand\":false"));

		session.Detach();
	});

	TEST_CASE(L"WorkflowDebugBridge 负行号栈帧归一化")
	{
		WorkflowDebugSession session(L"wf-negative-row");
		session.Attach();

		collections::List<WorkflowDebugStackFrame> frames;
		WorkflowDebugStackFrame frame;
		frame.threadId = 0;
		frame.frameId = 0;
		frame.sourceId = -1;
		frame.functionName = L"Hidden";
		frame.sourcePath = L"unknown://source/frame/0";
		frame.row = -1;
		frame.column = -1;
		frames.Add(frame);
		session.GetStackInspector()->CaptureStack(0, frames);
		session.GetState()->SetPhase(WorkflowDebugSessionPhase::Paused);
		session.GetState()->SetLastStopped(L"pause", 0, 0, -1, -1);

		WorkflowDebugEnvelope stackTrace;
		stackTrace.kind = WorkflowDebugEnvelopeKind::Request;
		stackTrace.command = L"stackTrace";
		stackTrace.sessionId = L"wf-negative-row";
		stackTrace.seq = 13;
		stackTrace.body = L"{\"threadId\":0,\"startFrame\":0,\"levels\":20}";
		TEST_ASSERT(session.Dispatch(stackTrace) == true);

		WorkflowDebugEnvelope stackTraceResponse;
		TEST_ASSERT(session.GetTransport()->TryPopOutgoing(stackTraceResponse) == true);
		TEST_ASSERT(stackTraceResponse.kind == WorkflowDebugEnvelopeKind::Response);
		TEST_ASSERT(stackTraceResponse.replyTo == 13);
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"sourceId\":-1"));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"row\":0"));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"line\":1"));
		TEST_ASSERT(ContainsSubstring(stackTraceResponse.body, L"\"functionName\":\"Hidden\""));

		session.Detach();
	});

	TEST_CASE(L"WorkflowDebugBridge 下发断点事件")
	{
		WorkflowDebugSession session(L"wf-breakpoint");
		session.Attach();

		session.GetSourceCatalog()->RegisterSource(7, L"D:/src/main.wf", 12);

		WorkflowDebugEnvelope setBreakpoints;
		setBreakpoints.kind = WorkflowDebugEnvelopeKind::Request;
		setBreakpoints.command = L"setBreakpoints";
		setBreakpoints.sessionId = L"wf-breakpoint";
		setBreakpoints.seq = 20;
		setBreakpoints.body = L"{\"sourcePath\":\"D:/src/main.wf\",\"codeIndex\":7,\"breakpoints\":[{\"breakpointId\":\"wf-bp-1\",\"row\":28,\"condition\":\"x > 0\",\"logMessage\":\"hit\"}]}";

		TEST_ASSERT(session.Dispatch(setBreakpoints) == true);

		WorkflowDebugEnvelope validation;
		TEST_ASSERT(session.GetTransport()->TryPopOutgoing(validation) == true);
		TEST_ASSERT(validation.kind == WorkflowDebugEnvelopeKind::Event);
		TEST_ASSERT(validation.command == L"breakpointValidated");
		TEST_ASSERT(validation.replyTo == 20);
		TEST_ASSERT(ContainsSubstring(validation.body, L"\"breakpointId\":\"wf-bp-1\""));
		TEST_ASSERT(ContainsSubstring(validation.body, L"\"verified\":true"));
		TEST_ASSERT(!ContainsSubstring(validation.body, L"\"reason\""));
		TEST_ASSERT(session.GetBreakpointRegistry()->Count() == 1);

		session.Detach();
	});

	TEST_CASE(L"WorkflowDebugBridge 暂停与异常事件")
	{
		WorkflowDebugSession session(L"wf-event");
		session.Attach();

		collections::List<WorkflowDebugStackFrame> frames;
		WorkflowDebugStackFrame frame0;
		frame0.threadId = 2;
		frame0.frameId = 0;
		frame0.sourceId = 7;
		frame0.functionName = L"Main";
		frame0.sourcePath = L"D:/src/main.wf";
		frame0.row = 12;
		frame0.column = 3;

		WorkflowDebugStackFrame frame1 = frame0;
		frame1.frameId = 1;
		frame1.functionName = L"Helper";
		frame1.row = 20;
		frame1.column = 5;

		frames.Add(frame0);
		frames.Add(frame1);
		session.GetStackInspector()->CaptureStack(2, frames);

		session.GetState()->SetLastInboundSeq(30);
		session.GetState()->SetLastStopped(L"breakpoint", 2, 1, 7, 20);
		TEST_ASSERT(session.GetBridge()->NotifyStopped() == true);

		WorkflowDebugEnvelope stopped;
		TEST_ASSERT(session.GetTransport()->TryPopOutgoing(stopped) == true);
		TEST_ASSERT(stopped.kind == WorkflowDebugEnvelopeKind::Event);
		TEST_ASSERT(stopped.command == L"stopped");
		TEST_ASSERT(stopped.replyTo == 30);
		TEST_ASSERT(ContainsSubstring(stopped.body, L"\"reason\":\"breakpoint\""));
		TEST_ASSERT(ContainsSubstring(stopped.body, L"\"threadId\":2"));
		TEST_ASSERT(ContainsSubstring(stopped.body, L"\"frameId\":1"));
		TEST_ASSERT(ContainsSubstring(stopped.body, L"\"sourceId\":7"));
		TEST_ASSERT(ContainsSubstring(stopped.body, L"\"row\":20"));

		session.GetState()->SetLastInboundSeq(31);
		session.GetState()->SetLastStopped(L"exception", 2, 1, 7, 20);
		TEST_ASSERT(session.GetBridge()->NotifyException(L"boom", true) == true);

		WorkflowDebugEnvelope exceptionEvent;
		TEST_ASSERT(session.GetTransport()->TryPopOutgoing(exceptionEvent) == true);
		TEST_ASSERT(exceptionEvent.kind == WorkflowDebugEnvelopeKind::Event);
		TEST_ASSERT(exceptionEvent.command == L"exception");
		TEST_ASSERT(exceptionEvent.replyTo == 31);
		TEST_ASSERT(ContainsSubstring(exceptionEvent.body, L"\"message\":\"boom\""));
		TEST_ASSERT(ContainsSubstring(exceptionEvent.body, L"\"fatal\":true"));
		TEST_ASSERT(ContainsSubstring(exceptionEvent.body, L"\"callStack\""));
		TEST_ASSERT(ContainsSubstring(exceptionEvent.body, L"\"functionName\":\"Main\""));
		TEST_ASSERT(ContainsSubstring(exceptionEvent.body, L"\"functionName\":\"Helper\""));

		session.Detach();
	});

	TEST_CASE(L"RemoteWfDebugger 暂停恢复")
	{
		TestRemoteWfDebugger debugger;
		std::atomic<bool> runResult = false;
		std::atomic<bool> stopResult = false;

		TEST_ASSERT(debugger.Pause() == true);
		TEST_ASSERT(debugger.GetState() == runtime::WfDebugger::RequiredToPause);

		std::thread runThread([&]()
		{
			runResult = debugger.WaitForContinueOnce();
		});

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		TEST_ASSERT(debugger.RequestRun() == true);
		runThread.join();

		TEST_ASSERT(runResult.load() == true);
		TEST_ASSERT(debugger.GetState() == runtime::WfDebugger::Running);

		TEST_ASSERT(debugger.Pause() == true);
		TEST_ASSERT(debugger.GetState() == runtime::WfDebugger::RequiredToPause);

		std::thread stopThread([&]()
		{
			stopResult = debugger.WaitForContinueOnce();
		});

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		TEST_ASSERT(debugger.RequestStop() == true);
		stopThread.join();

		TEST_ASSERT(stopResult.load() == false);
		TEST_ASSERT(debugger.GetState() == runtime::WfDebugger::RequiredToStop);
	});
}
