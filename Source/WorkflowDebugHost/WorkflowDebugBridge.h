/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGBRIDGE
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGBRIDGE

#include "WorkflowDebugProtocol.h"
#include "WorkflowDebugSourceCatalog.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			class WorkflowDebugSessionState;
			class WorkflowDebugTransport;
			class WorkflowDebugSourceCatalog;
			class WorkflowDebugBreakpointRegistry;
			class WorkflowDebugStackInspector;
			class WorkflowDebugValueInspector;
			class WorkflowDebugRuntimeBinding;

			/// <summary>
			/// 协议桥负责把文本协议翻译成宿主侧状态变化。
			/// 骨架阶段先保留命令分发，后续再接真实 JSON 解析和运行时查询。
			/// </summary>
			class WorkflowDebugBridge : public Object
			{
			public:
				WorkflowDebugBridge();
				~WorkflowDebugBridge();

				void								Bind(
					WorkflowDebugSessionState*		state,
					WorkflowDebugTransport*			transport,
					WorkflowDebugSourceCatalog*		sourceCatalog,
					WorkflowDebugBreakpointRegistry*	breakpointRegistry,
					WorkflowDebugStackInspector*		stackInspector,
					WorkflowDebugValueInspector*		valueInspector,
					WorkflowDebugRuntimeBinding*		runtimeBinding
				);
				void								Unbind();

				bool								Dispatch(const WorkflowDebugEnvelope& envelope);
				bool								NotifyHello(const WString& runtimeVersion, const collections::List<WorkflowDebugSourceRecord>& sourceMap);
				bool								NotifyReady(const WString& runtimeVersion);
				bool								NotifyOutput(const WString& level, const WString& message);
				bool								NotifyDisconnect(const WString& reason, bool restart = false);
				bool								NotifyStopped();
				bool								NotifyException(const WString& message, bool fatal);

			private:
				bool								HandleInitialize(const WorkflowDebugEnvelope& envelope);
				bool								HandleSetBreakpoints(const WorkflowDebugEnvelope& envelope);
				bool								HandleContinue(const WorkflowDebugEnvelope& envelope);
				bool								HandleNext(const WorkflowDebugEnvelope& envelope);
				bool								HandleStepIn(const WorkflowDebugEnvelope& envelope);
				bool								HandleStepOut(const WorkflowDebugEnvelope& envelope);
				bool								HandleStackTrace(const WorkflowDebugEnvelope& envelope);
				bool								HandleScopes(const WorkflowDebugEnvelope& envelope);
				bool								HandleVariables(const WorkflowDebugEnvelope& envelope);
				bool								HandleDisconnect(const WorkflowDebugEnvelope& envelope);
				bool								NotifyBreakpointValidated(const WString& breakpointId, bool verified, const WString& reason, vint replyTo);

			private:
				WorkflowDebugSessionState*			state = nullptr;
				WorkflowDebugTransport*				transport = nullptr;
				WorkflowDebugSourceCatalog*			sourceCatalog = nullptr;
				WorkflowDebugBreakpointRegistry*	breakpointRegistry = nullptr;
				WorkflowDebugStackInspector*		stackInspector = nullptr;
				WorkflowDebugValueInspector*		valueInspector = nullptr;
				WorkflowDebugRuntimeBinding*		runtimeBinding = nullptr;
			};
		}
	}
}

#endif

#endif
