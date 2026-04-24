/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugRuntimeBinding.h"
#include "WorkflowDebugBridge.h"
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
				auto succeeded = Pause();
				if (succeeded)
				{
					std::lock_guard<std::mutex> guard(controlMutex);
					pauseSnapshotCaptured = false;
					controlCondition.notify_all();
				}
				return succeeded;
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
			}

			void WorkflowDebugRuntimeBinding::AttachDebugData(
				WorkflowDebugSessionState* valueState,
				WorkflowDebugSourceCatalog* valueSourceCatalog,
				WorkflowDebugStackInspector* valueStackInspector,
				WorkflowDebugValueInspector* valueValueInspector,
				WorkflowDebugBridge* valueBridge
			)
			{
				state = valueState;
				sourceCatalog = valueSourceCatalog;
				stackInspector = valueStackInspector;
				valueInspector = valueValueInspector;
				bridge = valueBridge;
			}

			void WorkflowDebugRuntimeBinding::Unbind()
			{
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
				stackInspector = nullptr;
				valueInspector = nullptr;
				bridge = nullptr;
			}

			bool WorkflowDebugRuntimeBinding::RequestStopOnEntry()
			{
				if (!remoteDebugger)
				{
					return false;
				}

				auto succeeded = remoteDebugger->RequestStopOnEntry();
				if (succeeded)
				{
					stopOnEntryPending = true;
				}
				return succeeded;
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
				for (vint frameId = 0; frameId < context->stackFrames.Count(); frameId++)
				{
					const auto& stackFrame = context->stackFrames[frameId];
					WorkflowDebugStackFrame frame;
					frame.threadId = threadId;
					frame.frameId = frameId;
					frame.functionName = context->globalContext->assembly->functions[stackFrame.functionIndex]->name;

					auto range = debuggerObject->GetCurrentPosition(true, context, frameId);
					frame.sourceId = range.codeIndex;
					frame.row = range.start.row;
					frame.column = range.start.column;

					if (sourceCatalog && frame.sourceId >= 0)
					{
						vint resolvedRow = 0;
						sourceCatalog->ResolveByCodeIndex(frame.sourceId, frame.sourcePath, resolvedRow);
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
					if (context->exceptionInfo)
					{
						reason = L"exception";
					}
					else if (debuggerObject->GetState() == runtime::WfDebugger::PauseByBreakPoint)
					{
						reason = L"breakpoint";
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
					if (context->exceptionInfo)
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
