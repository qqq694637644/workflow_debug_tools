/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugRuntimeBinding.h"
#include "WorkflowDebugBridge.h"
#include "WorkflowDebugBreakpointRegistry.h"
#include "WorkflowDebugSessionState.h"
#include "WorkflowDebugSourceCatalog.h"
#include "WorkflowDebugStackInspector.h"
#include "WorkflowDebugValueInspector.h"
#include "../Runtime/WfRuntime.h"
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <thread>

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			namespace
			{
				static void ClearInstalledBreakpoints(runtime::WfDebugger* debugger, collections::List<vint>& installedBreakpointIndices)
				{
					if (debugger)
					{
						for (vint i = installedBreakpointIndices.Count() - 1; i >= 0; --i)
						{
							debugger->RemoveBreakPoint(installedBreakpointIndices[i]);
						}
					}
					installedBreakpointIndices.Clear();
				}

				static WString DescribeValue(const reflection::description::Value& value)
				{
					if (value.IsNull())
					{
						return L"null";
					}

					auto typeDescriptor = value.GetTypeDescriptor();
					if (typeDescriptor)
					{
						if (auto serializableType = typeDescriptor->GetSerializableType())
						{
							WString text;
							if (serializableType->Serialize(value, text))
							{
								return text;
							}
						}
						return value.GetTypeFriendlyName();
					}

					return L"<unknown>";
				}

				static WString DescribeKey(const reflection::description::Value& value)
				{
					if (value.IsNull())
					{
						return L"null";
					}

					auto typeDescriptor = value.GetTypeDescriptor();
					if (typeDescriptor)
					{
						if (auto serializableType = typeDescriptor->GetSerializableType())
						{
							WString text;
							if (serializableType->Serialize(value, text))
							{
								return text;
							}
						}
					}

					return value.GetTypeFriendlyName();
				}

				static void AppendVariables(
					Ptr<reflection::description::IValueReadonlyDictionary> dictionary,
					collections::List<WorkflowDebugVariable>& output
				)
				{
					if (!dictionary)
					{
						return;
					}

					auto keys = dictionary->GetKeys();
					auto values = dictionary->GetValues();
					auto count = dictionary->GetCount();
					for (vint i = 0; i < count; i++)
					{
						WorkflowDebugVariable variable;
						variable.name = keys ? DescribeKey(keys->Get(i)) : L"<未知>";
						variable.type = values ? values->Get(i).GetTypeFriendlyName() : WString();
						variable.value = values ? DescribeValue(values->Get(i)) : WString();
						output.Add(variable);
					}
				}

				static WString BuildUnknownSourcePath(vint frameId)
				{
					return L"unknown://source/frame/" + itow(frameId >= 0 ? frameId : 0);
				}

				static vint FindThreadId(runtime::WfDebugger* debugger, runtime::WfRuntimeThreadContext* context)
				{
					if (!debugger || !context)
					{
						return -1;
					}

					const auto& threadContexts = debugger->GetThreadContexts();
					for (vint i = 0; i < threadContexts.Count(); i++)
					{
						if (threadContexts[i] == context)
						{
							return i;
						}
					}

					return threadContexts.Count() > 0 ? threadContexts.Count() - 1 : -1;
				}

				static bool IsDebuggerPaused(runtime::WfDebugger::State state)
				{
					return state == runtime::WfDebugger::PauseByOperation
						|| state == runtime::WfDebugger::PauseByBreakPoint;
				}

				static bool ShouldReportException(const runtime::WfRuntimeThreadContext* context)
				{
					// 运行时会把已捕获的异常对象继续留在 exceptionInfo 里，供 catch 体和 raise; 重用。
					// 因此这里不能只看 exceptionInfo，否则异常被捕获后，每一次单步暂停都会被误报成新的异常。
					return context
						&& (
							context->status == runtime::WfRuntimeExecutionStatus::RaisedException
							|| context->status == runtime::WfRuntimeExecutionStatus::FatalError
						);
				}

				static bool TryBuildStackFrameFromPosition(
					WorkflowDebugStackFrame& frame,
					const glr::ParsingTextRange& range,
					WorkflowDebugSourceCatalog* sourceCatalog
				)
				{
					frame.sourceId = range.codeIndex;
					frame.row = range.start.row;
					frame.column = range.start.column;

					if (sourceCatalog)
					{
						if (frame.sourceId >= 0)
						{
							vint resolvedRow = 0;
							if (sourceCatalog->ResolveByCodeIndex(frame.sourceId, frame.sourcePath, resolvedRow))
							{
								if (frame.row < 0)
								{
									frame.row = resolvedRow;
								}
								if (frame.row < 0)
								{
									frame.row = 0;
								}
								if (frame.column < 0)
								{
									frame.column = 0;
								}
								return true;
							}
						}

						if (frame.sourcePath.Length() >= 17 && frame.sourcePath.Left(17) != L"unknown://source/")
						{
							vint resolvedSourceId = -1;
							vint resolvedRow = 0;
							if (sourceCatalog->ResolveByPath(frame.sourcePath, resolvedSourceId, resolvedRow))
							{
								if (frame.sourceId < 0)
								{
									frame.sourceId = resolvedSourceId;
								}
								if (frame.row < 0)
								{
									frame.row = resolvedRow;
								}
								if (frame.row < 0)
								{
									frame.row = 0;
								}
								if (frame.column < 0)
								{
									frame.column = 0;
								}
								return true;
							}
						}
					}

					frame.sourcePath = BuildUnknownSourcePath(frame.frameId);
					if (frame.row < 0)
					{
						frame.row = 0;
					}
					if (frame.column < 0)
					{
						frame.column = 0;
					}
					return false;
				}

				static WString DescribePosition(const glr::ParsingTextRange& range)
				{
					return L"(codeIndex=" + itow(range.codeIndex)
						+ L", row=" + itow(range.start.row)
						+ L", column=" + itow(range.start.column)
						+ L")";
				}
			}

			RemoteWfDebugger::RemoteWfDebugger()
			{
			}

			RemoteWfDebugger::~RemoteWfDebugger()
			{
			}

			void RemoteWfDebugger::SetRuntimeBinding(WorkflowDebugRuntimeBinding* value)
			{
				binding = value;
			}

			bool RemoteWfDebugger::RequestRun()
			{
				auto succeeded = Run();
				if (succeeded)
				{
					std::lock_guard<std::mutex> guard(controlMutex);
					pauseSnapshotCaptured = false;
					controlCondition.notify_all();
				}
				return succeeded;
			}

			bool RemoteWfDebugger::RequestPause()
			{
				auto succeeded = Pause();
				if (succeeded)
				{
					std::lock_guard<std::mutex> guard(controlMutex);
					pauseSnapshotCaptured = false;
					controlCondition.notify_all();
				}
				return succeeded;
			}

			bool RemoteWfDebugger::RequestStop()
			{
				auto succeeded = Stop();
				if (succeeded)
				{
					std::lock_guard<std::mutex> guard(controlMutex);
					pauseSnapshotCaptured = false;
					controlCondition.notify_all();
				}
				return succeeded;
			}

			bool RemoteWfDebugger::RequestStopOnEntry()
			{
				if (!binding)
				{
					return false;
				}

				// 入口暂停不应该直接把当前内部帧强行打成 Pause，
				// 而是先挂起一个“等到第一条可映射源码语句再停”的标记。
				binding->stopOnEntryPending = true;
				{
					std::lock_guard<std::mutex> guard(controlMutex);
					pauseSnapshotCaptured = false;
				}
				return true;
			}

			bool RemoteWfDebugger::BreakIns(runtime::WfAssembly* assembly, vint instruction)
			{
				if (binding)
				{
					// stopOnEntry 先跳过内部帧；等到第一条可映射源码语句时，再触发一次真正的入口暂停。
					if (binding->ShouldDelayStopOnEntry())
					{
						return false;
					}

					if (binding->IsStopOnEntryPending())
					{
						return true;
					}
				}

				return WfDebugger::BreakIns(assembly, instruction);
			}

			bool RemoteWfDebugger::RequestStepOver(bool beforeCodegen)
			{
				auto succeeded = StepOver(beforeCodegen);
				if (succeeded)
				{
					std::lock_guard<std::mutex> guard(controlMutex);
					pauseSnapshotCaptured = false;
					controlCondition.notify_all();
				}
				return succeeded;
			}

			bool RemoteWfDebugger::RequestStepInto(bool beforeCodegen)
			{
				auto succeeded = StepInto(beforeCodegen);
				if (succeeded)
				{
					std::lock_guard<std::mutex> guard(controlMutex);
					pauseSnapshotCaptured = false;
					controlCondition.notify_all();
				}
				return succeeded;
			}

			bool RemoteWfDebugger::RequestStepOut(bool beforeCodegen)
			{
				auto succeeded = StepOut(beforeCodegen);
				if (succeeded)
				{
					std::lock_guard<std::mutex> guard(controlMutex);
					pauseSnapshotCaptured = false;
					controlCondition.notify_all();
				}
				return succeeded;
			}

			void RemoteWfDebugger::OnStartExecution()
			{
			}

			void RemoteWfDebugger::OnBlockExecution()
			{
				bool shouldCapture = false;
				{
					std::lock_guard<std::mutex> guard(controlMutex);
					if (!pauseSnapshotCaptured)
					{
						pauseSnapshotCaptured = true;
						shouldCapture = true;
					}
				}

				if (shouldCapture && binding)
				{
					binding->CapturePausedState();
				}

				// 这里通过短轮询等待外部命令，避免和运行时内部的状态锁互相抢占。
				while (IsDebuggerPaused(GetState()))
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}

			void RemoteWfDebugger::OnStopExecution()
			{
				controlCondition.notify_all();
			}

			WorkflowDebugRuntimeBinding::WorkflowDebugRuntimeBinding()
			{
			}

			WorkflowDebugRuntimeBinding::~WorkflowDebugRuntimeBinding()
			{
				Unbind();
			}

			void WorkflowDebugRuntimeBinding::Bind(const Ptr<runtime::WfDebugger>& value)
			{
				Unbind();
				debugger = value;
				remoteDebugger = value ? value.Cast<RemoteWfDebugger>() : nullptr;
				if (remoteDebugger)
				{
					remoteDebugger->SetRuntimeBinding(this);
				}
				if (debugger)
				{
					workflow::runtime::SetDebuggerForCurrentThread(debugger);
				}
				RefreshBreakpoints();
			}

			void WorkflowDebugRuntimeBinding::AttachDebugData(
				WorkflowDebugSessionState* valueState,
				WorkflowDebugSourceCatalog* valueSourceCatalog,
				WorkflowDebugBreakpointRegistry* valueBreakpointRegistry,
				WorkflowDebugStackInspector* valueStackInspector,
				WorkflowDebugValueInspector* valueValueInspector,
				WorkflowDebugBridge* valueBridge
			)
			{
				state = valueState;
				sourceCatalog = valueSourceCatalog;
				breakpointRegistry = valueBreakpointRegistry;
				stackInspector = valueStackInspector;
				valueInspector = valueValueInspector;
				bridge = valueBridge;
				breakpointsDirty = true;
				RefreshBreakpoints();
			}

			void WorkflowDebugRuntimeBinding::SetAssembly(const Ptr<runtime::WfAssembly>& value)
			{
				assembly = value;
				breakpointsDirty = true;
				RefreshBreakpoints();
			}

			void WorkflowDebugRuntimeBinding::RefreshBreakpoints()
			{
				if (!debugger || !assembly || !breakpointRegistry)
				{
					return;
				}

				// 断点可能会在 hello/initialize 之后分批到达，所以每次刷新都先清空旧的运行时断点，再按当前 registry 重建。
				ClearInstalledBreakpoints(debugger.Obj(), installedBreakpointIndices);

				for (auto breakpoint : breakpointRegistry->GetBreakpoints())
				{
					if (!breakpoint.verified || breakpoint.codeIndex < 0 || breakpoint.row < 0)
					{
						continue;
					}

					auto breakpointIndex = debugger->AddCodeLineBreakPoint(assembly.Obj(), breakpoint.codeIndex, breakpoint.row, breakpoint.beforeCodegen);
					if (breakpointIndex >= 0)
					{
						installedBreakpointIndices.Add(breakpointIndex);
					}
				}

				breakpointsDirty = false;
			}

			void WorkflowDebugRuntimeBinding::Unbind()
			{
				ClearInstalledBreakpoints(debugger.Obj(), installedBreakpointIndices);
				if (remoteDebugger)
				{
					remoteDebugger->SetRuntimeBinding(nullptr);
				}
				if (debugger)
				{
					// 这里不要再走 SetDebuggerForCurrentThread(nullptr)，直接 Reset 更稳。
					workflow::runtime::ResetDebuggerForCurrentThread();
					debugger = nullptr;
				}
				remoteDebugger = nullptr;
				stopOnEntryPending = false;
				state = nullptr;
				sourceCatalog = nullptr;
				breakpointRegistry = nullptr;
				stackInspector = nullptr;
				valueInspector = nullptr;
				bridge = nullptr;
				assembly = nullptr;
				breakpointsDirty = false;
			}

			bool WorkflowDebugRuntimeBinding::RequestStopOnEntry()
			{
				if (!remoteDebugger)
				{
					return false;
				}

				return remoteDebugger->RequestStopOnEntry();
			}

			bool WorkflowDebugRuntimeBinding::ShouldDelayStopOnEntry() const
			{
				if (!stopOnEntryPending.load())
				{
					return false;
				}

				if (!remoteDebugger || !sourceCatalog)
				{
					return true;
				}

				// 入口暂停只接受能映射到源码的当前位置；如果当前帧还是内部初始化帧，就继续跑到下一条可映射语句。
				auto context = remoteDebugger->GetCurrentThreadContext();
				if (!context || context->stackFrames.Count() == 0)
				{
					return true;
				}

				auto frameId = context->stackFrames.Count() - 1;
				auto position = remoteDebugger->GetCurrentPosition(true, context, frameId);
				WorkflowDebugStackFrame frame;
				frame.threadId = FindThreadId(remoteDebugger.Obj(), context);
				frame.frameId = frameId;
				return !TryBuildStackFrameFromPosition(frame, position, sourceCatalog);
			}

			bool WorkflowDebugRuntimeBinding::IsStopOnEntryPending() const
			{
				return stopOnEntryPending.load();
			}

			bool WorkflowDebugRuntimeBinding::IsBound() const
			{
				return debugger != nullptr;
			}

			Ptr<runtime::WfDebugger> WorkflowDebugRuntimeBinding::GetDebugger() const
			{
				return debugger;
			}

			Ptr<RemoteWfDebugger> WorkflowDebugRuntimeBinding::GetRemoteDebugger() const
			{
				return remoteDebugger;
			}

			void WorkflowDebugRuntimeBinding::CapturePausedState()
			{
				auto debuggerObject = remoteDebugger ? remoteDebugger.Obj() : nullptr;
				auto context = debuggerObject ? debuggerObject->GetCurrentThreadContext() : nullptr;
				if (!context)
				{
					return;
				}

				auto threadId = FindThreadId(debuggerObject, context);
				if (threadId < 0)
				{
					return;
				}

				if (stackInspector)
				{
					stackInspector->ClearThread(threadId);
				}
				if (valueInspector)
				{
					valueInspector->ClearThread(threadId);
				}

				collections::List<WorkflowDebugStackFrame> frames;
				bool topFrameResolved = true;
				bool topFrameAfterResolved = false;
				glr::ParsingTextRange topBeforeRange;
				glr::ParsingTextRange topAfterRange;
				WString topFunctionName;
				vint topFrameId = -1;
				for (vint frameId = 0; frameId < context->stackFrames.Count(); frameId++)
				{
					const auto& stackFrame = context->stackFrames[frameId];
					WorkflowDebugStackFrame frame;
					frame.threadId = threadId;
					frame.frameId = frameId;
					frame.functionName = context->globalContext->assembly->functions[stackFrame.functionIndex]->name;

					auto beforeRange = debuggerObject->GetCurrentPosition(true, context, frameId);
					auto resolved = TryBuildStackFrameFromPosition(frame, beforeRange, sourceCatalog);
					auto afterRange = beforeRange;
					auto afterResolved = false;
					if (!resolved)
					{
						afterRange = debuggerObject->GetCurrentPosition(false, context, frameId);
						auto alternate = frame;
						afterResolved = TryBuildStackFrameFromPosition(alternate, afterRange, sourceCatalog);
						// 这里不把 afterCodegen 当作回退，只记录诊断信息，避免隐藏当前位置映射问题。
					}

					if (frameId == context->stackFrames.Count() - 1)
					{
						topFrameResolved = resolved;
						topFrameAfterResolved = afterResolved;
						topBeforeRange = beforeRange;
						topAfterRange = afterRange;
						topFunctionName = frame.functionName;
						topFrameId = frameId;
					}

					frames.Add(frame);

					if (valueInspector)
					{
						runtime::WfRuntimeCallStackInfo callStackInfo(context, stackFrame);
						WorkflowDebugFrameValues values;
						AppendVariables(callStackInfo.GetLocalArguments(), values.argument);
						AppendVariables(callStackInfo.GetLocalVariables(), values.local);
						AppendVariables(callStackInfo.GetCapturedVariables(), values.captured);
						AppendVariables(callStackInfo.GetGlobalVariables(), values.global);
						valueInspector->CaptureFrame(threadId, frameId, values);
					}
				}

				if (stackInspector)
				{
					stackInspector->CaptureStack(threadId, frames);
				}

				auto reportException = ShouldReportException(context);

				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Paused);

					vint stoppedFrameId = context->stackFrames.Count() > 0 ? context->stackFrames.Count() - 1 : -1;
					vint stoppedSourceId = -1;
					vint stoppedRow = -1;
					if (stoppedFrameId >= 0 && stoppedFrameId < frames.Count())
					{
						stoppedSourceId = frames[stoppedFrameId].sourceId;
						stoppedRow = frames[stoppedFrameId].row;
					}

					WString reason = L"pause";
					if (reportException)
					{
						reason = L"exception";
					}
					else if (debuggerObject->GetState() == runtime::WfDebugger::PauseByBreakPoint)
					{
						reason = L"breakpoint";
					}
				else if (debuggerObject->GetRunningType() != runtime::WfDebugger::RunUntilBreakPoint)
				{
					reason = L"step";
				}
				else if (stopOnEntryPending)
					{
						reason = L"entry";
						stopOnEntryPending = false;
					}

					state->SetLastStopped(reason, threadId, stoppedFrameId, stoppedSourceId, stoppedRow);
				}

				if (bridge)
				{
					if (!topFrameResolved && topFrameId >= 0)
					{
						WString message = L"当前暂停位置无法映射到源码：threadId=" + itow(threadId)
							+ L"，frameId=" + itow(topFrameId)
							+ L"，function=" + topFunctionName
							+ L"，beforeCodegen=" + (topFrameResolved ? L"成功" : L"失败")
							+ L"，afterCodegen=" + (topFrameAfterResolved ? L"成功" : L"失败")
							+ L"，before=" + DescribePosition(topBeforeRange)
							+ L"，after=" + DescribePosition(topAfterRange)
							+ L"。";
						bridge->NotifyOutput(L"warn", message);
					}

					if (reportException)
					{
						bridge->NotifyException(context->exceptionInfo->message, context->exceptionInfo->fatal);
					}
					else
					{
						bridge->NotifyStopped();
					}
				}
			}
		}
	}
}

#endif
