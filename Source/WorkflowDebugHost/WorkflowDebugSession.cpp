/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugSession.h"
#include "WorkflowDebugBridge.h"
#include "WorkflowDebugBreakpointRegistry.h"
#include "WorkflowDebugRuntimeBinding.h"
#include "WorkflowDebugSessionState.h"
#include "WorkflowDebugSourceCatalog.h"
#include "WorkflowDebugStackInspector.h"
#include "WorkflowDebugTransport.h"
#include "WorkflowDebugValueInspector.h"
#include <chrono>
#include <thread>

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			WorkflowDebugSession::WorkflowDebugSession(const WString& valueSessionId)
				:sessionId(valueSessionId)
			{
				CHECK_ERROR(sessionId.Length() > 0, L"会话标识不能为空。");

				state = Ptr(new WorkflowDebugSessionState);
				transport = Ptr(new WorkflowDebugTransport);
				sourceCatalog = Ptr(new WorkflowDebugSourceCatalog);
				breakpointRegistry = Ptr(new WorkflowDebugBreakpointRegistry(sourceCatalog.Obj()));
				stackInspector = Ptr(new WorkflowDebugStackInspector);
				valueInspector = Ptr(new WorkflowDebugValueInspector);
				runtimeBinding = Ptr(new WorkflowDebugRuntimeBinding);
				debugger = Ptr<runtime::WfDebugger>(new RemoteWfDebugger);
				bridge = Ptr(new WorkflowDebugBridge);

				bridge->Bind(
					state.Obj(),
					transport.Obj(),
					sourceCatalog.Obj(),
					breakpointRegistry.Obj(),
					stackInspector.Obj(),
					valueInspector.Obj(),
					runtimeBinding.Obj()
				);
			}

			WorkflowDebugSession::~WorkflowDebugSession()
			{
				Detach();
			}

			const WString& WorkflowDebugSession::GetSessionId() const
			{
				return sessionId;
			}

			void WorkflowDebugSession::Attach()
			{
				state->Attach(sessionId);
				sourceCatalog->Clear();
				breakpointRegistry->Clear();
				stackInspector->Clear();
				valueInspector->Clear();
				runtimeBinding->Bind(debugger);
				runtimeBinding->AttachDebugData(state.Obj(), sourceCatalog.Obj(), stackInspector.Obj(), valueInspector.Obj(), bridge.Obj());
				if (transport->GetEndpointPort() > 0)
				{
					CHECK_ERROR(transport->Connect(), L"无法连接到调试适配器。");
					dispatchLoopRunning = true;
					dispatchThread = std::thread([this]()
					{
						WorkflowDebugEnvelope envelope;
						while (dispatchLoopRunning)
						{
							bool received = false;
							while (transport && transport->TryReceive(envelope))
							{
								received = true;
								Dispatch(envelope);
								if (!dispatchLoopRunning)
								{
									break;
								}
							}

							if (!dispatchLoopRunning)
							{
								break;
							}

							if (!transport || !transport->IsOpen())
							{
								break;
							}

							if (!received)
							{
								std::this_thread::sleep_for(std::chrono::milliseconds(1));
							}
						}

						dispatchLoopRunning = false;
					});
				}
				else
				{
					transport->Open();
				}
			}

			void WorkflowDebugSession::Detach()
			{
				if (bridge && transport && transport->IsOpen())
				{
					bridge->NotifyDisconnect(L"会话关闭");
				}
				dispatchLoopRunning = false;
				runtimeBinding->Unbind();
				runtimeBinding->AttachDebugData(nullptr, nullptr, nullptr, nullptr, nullptr);
				transport->Close();
				if (dispatchThread.joinable())
				{
					dispatchThread.join();
				}
				if (state)
				{
					state->Detach();
				}
				sourceCatalog->Clear();
				breakpointRegistry->Clear();
				stackInspector->Clear();
				valueInspector->Clear();
			}

			bool WorkflowDebugSession::Dispatch(const WorkflowDebugEnvelope& envelope)
			{
				return bridge ? bridge->Dispatch(envelope) : false;
			}

			void WorkflowDebugSession::SetSourceMap(const collections::List<WorkflowDebugSourceRecord>& value)
			{
				sourceMap.Clear();
				for (auto source : value)
				{
					sourceMap.Add(source);
				}
			}

			bool WorkflowDebugSession::SendHello()
			{
				if (!bridge || !transport || !transport->IsOpen())
				{
					return false;
				}

				sourceCatalog->Clear();
				for (auto source : sourceMap)
				{
					sourceCatalog->RegisterSource(source.codeIndex, source.sourcePath, source.row);
				}
				if (state)
				{
					state->SetSourceMapCount(sourceMap.Count());
				}

				return bridge->NotifyHello(runtimeVersion, sourceMap);
			}

			bool WorkflowDebugSession::WaitForReady(vint timeoutMilliseconds)
			{
				if (!state)
				{
					return false;
				}

				auto begin = std::chrono::steady_clock::now();
				while (true)
				{
					auto phase = state->GetPhase();
					if (phase == WorkflowDebugSessionPhase::Ready)
					{
						return true;
					}
					if (phase == WorkflowDebugSessionPhase::Closed)
					{
						return false;
					}
					if (transport && !transport->IsOpen())
					{
						return false;
					}

					if (timeoutMilliseconds > 0)
					{
						auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
						if (elapsed >= timeoutMilliseconds)
						{
							return false;
						}
					}

					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}

			WorkflowDebugSessionState* WorkflowDebugSession::GetState() const
			{
				return state.Obj();
			}

			WorkflowDebugTransport* WorkflowDebugSession::GetTransport() const
			{
				return transport.Obj();
			}

			WorkflowDebugSourceCatalog* WorkflowDebugSession::GetSourceCatalog() const
			{
				return sourceCatalog.Obj();
			}

			WorkflowDebugBreakpointRegistry* WorkflowDebugSession::GetBreakpointRegistry() const
			{
				return breakpointRegistry.Obj();
			}

			WorkflowDebugStackInspector* WorkflowDebugSession::GetStackInspector() const
			{
				return stackInspector.Obj();
			}

			WorkflowDebugValueInspector* WorkflowDebugSession::GetValueInspector() const
			{
				return valueInspector.Obj();
			}

			WorkflowDebugRuntimeBinding* WorkflowDebugSession::GetRuntimeBinding() const
			{
				return runtimeBinding.Obj();
			}

			WorkflowDebugBridge* WorkflowDebugSession::GetBridge() const
			{
				return bridge.Obj();
			}
		}
	}
}

#endif
