/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGSESSION
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGSESSION

#include "WorkflowDebugProtocol.h"
#include "WorkflowDebugSourceCatalog.h"
#include <atomic>
#include <thread>

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace runtime
		{
			class WfDebugger;
			class WfAssembly;
		}
		namespace debughost
		{
			class WorkflowDebugBridge;
			class WorkflowDebugTransport;
			class WorkflowDebugSessionState;
			class WorkflowDebugSourceCatalog;
			class WorkflowDebugBreakpointRegistry;
			class WorkflowDebugStackInspector;
			class WorkflowDebugValueInspector;
			class WorkflowDebugRuntimeBinding;

			/// <summary>
			/// 单个调试会话对象。
			/// 它负责持有一次 attach 所需的所有子模块，并把生命周期分离出来。
			/// </summary>
			class WorkflowDebugSession : public Object
			{
			public:
				explicit WorkflowDebugSession(const WString& sessionId);
				~WorkflowDebugSession();

				const WString&						GetSessionId() const;

				void								Attach();
				void								Detach();
				bool								Dispatch(const WorkflowDebugEnvelope& envelope);
				void								SetSourceMap(const collections::List<WorkflowDebugSourceRecord>& sourceMap);
				void								SetAssembly(const Ptr<runtime::WfAssembly>& assembly);
				bool								SendHello();
				bool								WaitForReady(vint timeoutMilliseconds);
				bool								WaitForBreakpointSync(vint timeoutMilliseconds);

				WorkflowDebugSessionState*			GetState() const;
				WorkflowDebugTransport*				GetTransport() const;
				WorkflowDebugSourceCatalog*			GetSourceCatalog() const;
				WorkflowDebugBreakpointRegistry*	GetBreakpointRegistry() const;
				WorkflowDebugStackInspector*		GetStackInspector() const;
				WorkflowDebugValueInspector*		GetValueInspector() const;
				WorkflowDebugRuntimeBinding*		GetRuntimeBinding() const;
				WorkflowDebugBridge*				GetBridge() const;

			private:
				WString								sessionId;
				Ptr<WorkflowDebugSessionState>		state;
				Ptr<WorkflowDebugTransport>			transport;
				Ptr<WorkflowDebugSourceCatalog>		sourceCatalog;
				Ptr<WorkflowDebugBreakpointRegistry>	breakpointRegistry;
				Ptr<WorkflowDebugStackInspector>	stackInspector;
				Ptr<WorkflowDebugValueInspector>	valueInspector;
				Ptr<WorkflowDebugRuntimeBinding>	runtimeBinding;
				Ptr<WorkflowDebugBridge>			bridge;
				Ptr<runtime::WfDebugger>			debugger;
				Ptr<runtime::WfAssembly>			assembly;
				collections::List<WorkflowDebugSourceRecord>	sourceMap;
				std::atomic<bool>					dispatchLoopRunning = false;
				std::thread							dispatchThread;
				WString								runtimeVersion = L"workflow-runtime";
			};
		}
	}
}

#endif

#endif
