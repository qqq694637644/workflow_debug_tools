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
				runtimeBinding->Bind(debugger);
				runtimeBinding->AttachDebugData(state.Obj(), sourceCatalog.Obj(), stackInspector.Obj(), valueInspector.Obj());
				transport->Open();
				sourceCatalog->Clear();
				breakpointRegistry->Clear();
				stackInspector->Clear();
				valueInspector->Clear();
			}

			void WorkflowDebugSession::Detach()
			{
				runtimeBinding->Unbind();
				runtimeBinding->AttachDebugData(nullptr, nullptr, nullptr, nullptr);
				transport->Close();
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
