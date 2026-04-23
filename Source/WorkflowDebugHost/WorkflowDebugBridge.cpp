/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

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
			WorkflowDebugBridge::WorkflowDebugBridge()
			{
			}

			WorkflowDebugBridge::~WorkflowDebugBridge()
			{
			}

			void WorkflowDebugBridge::Bind(
				WorkflowDebugSessionState* valueState,
				WorkflowDebugTransport* valueTransport,
				WorkflowDebugSourceCatalog* valueSourceCatalog,
				WorkflowDebugBreakpointRegistry* valueBreakpointRegistry,
				WorkflowDebugStackInspector* valueStackInspector,
				WorkflowDebugValueInspector* valueValueInspector,
				WorkflowDebugRuntimeBinding* valueRuntimeBinding
			)
			{
				state = valueState;
				transport = valueTransport;
				sourceCatalog = valueSourceCatalog;
				breakpointRegistry = valueBreakpointRegistry;
				stackInspector = valueStackInspector;
				valueInspector = valueValueInspector;
				runtimeBinding = valueRuntimeBinding;
			}

			void WorkflowDebugBridge::Unbind()
			{
				state = nullptr;
				transport = nullptr;
				sourceCatalog = nullptr;
				breakpointRegistry = nullptr;
				stackInspector = nullptr;
				valueInspector = nullptr;
				runtimeBinding = nullptr;
			}

			bool WorkflowDebugBridge::Dispatch(const WorkflowDebugEnvelope& envelope)
			{
				if (envelope.command.Length() == 0)
				{
					return false;
				}

				if (envelope.command == L"hello")
				{
					return HandleHello(envelope);
				}
				if (envelope.command == L"initialize")
				{
					return HandleInitialize(envelope);
				}
				if (envelope.command == L"setBreakpoints")
				{
					return HandleSetBreakpoints(envelope);
				}
				if (envelope.command == L"continue")
				{
					return HandleContinue(envelope);
				}
				if (envelope.command == L"next")
				{
					return HandleNext(envelope);
				}
				if (envelope.command == L"stepIn")
				{
					return HandleStepIn(envelope);
				}
				if (envelope.command == L"stackTrace")
				{
					return HandleStackTrace(envelope);
				}
				if (envelope.command == L"scopes")
				{
					return HandleScopes(envelope);
				}
				if (envelope.command == L"variables")
				{
					return HandleVariables(envelope);
				}
				if (envelope.command == L"exception")
				{
					return HandleException(envelope);
				}

				return false;
			}

			bool WorkflowDebugBridge::HandleHello(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (sourceCatalog)
				{
					sourceCatalog->Clear();
				}
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Negotiating);
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleInitialize(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (transport)
				{
					transport->Open();
				}
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Ready);
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleSetBreakpoints(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (breakpointRegistry)
				{
					breakpointRegistry->Clear();
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleContinue(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (runtimeBinding && runtimeBinding->GetDebugger())
				{
					runtimeBinding->GetDebugger()->Run();
				}
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Running);
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleNext(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (runtimeBinding && runtimeBinding->GetDebugger())
				{
					runtimeBinding->GetDebugger()->StepOver();
				}
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Running);
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleStepIn(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (runtimeBinding && runtimeBinding->GetDebugger())
				{
					runtimeBinding->GetDebugger()->StepInto();
				}
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Running);
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleStackTrace(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				return stackInspector != nullptr;
			}

			bool WorkflowDebugBridge::HandleScopes(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				return valueInspector != nullptr;
			}

			bool WorkflowDebugBridge::HandleVariables(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				return valueInspector != nullptr;
			}

			bool WorkflowDebugBridge::HandleException(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Paused);
					state->SetLastStopped(L"exception", -1, -1, -1, -1);
				}
				return true;
			}
		}
	}
}

#endif
