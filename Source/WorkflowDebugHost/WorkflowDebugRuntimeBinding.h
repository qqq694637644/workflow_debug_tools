/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWRUNTIMEBINDING
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWRUNTIMEBINDING

#include "WorkflowDebugProtocol.h"
#include "../Runtime/WfRuntimeDebugger.h"
#include <condition_variable>
#include <mutex>

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			class WorkflowDebugSessionState;
			class WorkflowDebugSourceCatalog;
			class WorkflowDebugBreakpointRegistry;
			class WorkflowDebugStackInspector;
			class WorkflowDebugValueInspector;
			class WorkflowDebugBridge;
			class WorkflowDebugRuntimeBinding;

			/// <summary>
			/// 远程调试器直接继承 WfDebugger，这样主进程内脚本执行线程就能复用现有暂停语义。
			/// </summary>
			class RemoteWfDebugger : public runtime::WfDebugger
			{
			public:
				RemoteWfDebugger();
				~RemoteWfDebugger();

				void							SetRuntimeBinding(WorkflowDebugRuntimeBinding* value);

				bool							RequestRun();
				bool							RequestPause();
				bool							RequestStop();
				bool							RequestStopOnEntry();
				bool							RequestStepOver(bool beforeCodegen = true);
				bool							RequestStepInto(bool beforeCodegen = true);

			protected:
				void							OnStartExecution() override;
				void							OnBlockExecution() override;
				void							OnStopExecution() override;

			private:
				WorkflowDebugRuntimeBinding*	binding = nullptr;
				std::mutex						controlMutex;
				std::condition_variable			controlCondition;
				bool							pauseSnapshotCaptured = false;
			};

			/// <summary>
			/// 运行时绑定只负责把当前线程上的 debugger 装配和拆卸掉。
			/// 注意这里要用 ResetDebuggerForCurrentThread()，不要再走 SetDebuggerForCurrentThread(nullptr)。
			/// </summary>
			class WorkflowDebugRuntimeBinding : public Object
			{
			public:
				WorkflowDebugRuntimeBinding();
				~WorkflowDebugRuntimeBinding();

				void								Bind(const Ptr<runtime::WfDebugger>& debugger);
				void								AttachDebugData(
					WorkflowDebugSessionState*		state,
					WorkflowDebugSourceCatalog*		sourceCatalog,
					WorkflowDebugBreakpointRegistry* breakpointRegistry,
					WorkflowDebugStackInspector*	stackInspector,
					WorkflowDebugValueInspector*	valueInspector,
					WorkflowDebugBridge*			bridge
				);
				void								SetAssembly(const Ptr<runtime::WfAssembly>& value);
				void								RefreshBreakpoints();
				void								Unbind();
				bool								RequestStopOnEntry();
				bool								IsBound() const;
				Ptr<runtime::WfDebugger>			GetDebugger() const;
				Ptr<RemoteWfDebugger>				GetRemoteDebugger() const;
				void								CapturePausedState();

			private:
				Ptr<runtime::WfDebugger>			debugger;
				Ptr<RemoteWfDebugger>				remoteDebugger;
				bool								stopOnEntryPending = false;
				WorkflowDebugSessionState*			state = nullptr;
				WorkflowDebugSourceCatalog*			sourceCatalog = nullptr;
				WorkflowDebugBreakpointRegistry*	breakpointRegistry = nullptr;
				WorkflowDebugStackInspector*		stackInspector = nullptr;
				WorkflowDebugValueInspector*		valueInspector = nullptr;
				WorkflowDebugBridge*				bridge = nullptr;
				Ptr<runtime::WfAssembly>			assembly;
				collections::List<vint>				installedBreakpointIndices;
				bool								breakpointsDirty = false;
			};
		}
	}
}

#endif

#endif
