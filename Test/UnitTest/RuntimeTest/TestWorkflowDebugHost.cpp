#include "../../Source/Helper.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugBreakpointRegistry.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugHost.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugRuntimeBinding.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugSession.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugSessionState.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugSourceCatalog.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugStackInspector.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugTransport.h"
#include "../../../Source/WorkflowDebugHost/WorkflowDebugValueInspector.h"

using namespace vl::workflow::debughost;

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
		TEST_ASSERT(scopes[1].kind == WorkflowDebugScopeKind::Argument);
		TEST_ASSERT(scopes[2].kind == WorkflowDebugScopeKind::Captured);
		TEST_ASSERT(scopes[3].kind == WorkflowDebugScopeKind::Global);

		collections::List<WorkflowDebugVariable> loadedVariables;
		TEST_ASSERT(valueInspector.TryGetVariables(1, 1, WorkflowDebugScopeKind::Local, loadedVariables) == true);
		TEST_ASSERT(loadedVariables.Count() == 1);
		TEST_ASSERT(loadedVariables[0].name == L"localValue");
		TEST_ASSERT(loadedVariables[0].value == L"42");

		TEST_ASSERT(valueInspector.TryGetVariables(1, 1, WorkflowDebugScopeKind::Argument, loadedVariables) == true);
		TEST_ASSERT(loadedVariables.Count() == 1);
		TEST_ASSERT(loadedVariables[0].name == L"argumentValue");
	});

	TEST_CASE(L"WorkflowDebugSession 任务流")
	{
		WorkflowDebugSession session(L"wf-session");
		session.Attach();

		TEST_ASSERT(session.GetState()->GetPhase() == WorkflowDebugSessionPhase::Connected);
		TEST_ASSERT(session.GetTransport()->IsOpen() == true);
		TEST_ASSERT(session.GetRuntimeBinding()->IsBound() == true);

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
}
