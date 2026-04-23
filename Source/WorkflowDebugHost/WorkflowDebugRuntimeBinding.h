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

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			/// <summary>
			/// 远程调试器直接继承 WfDebugger，这样主进程内脚本执行线程就能复用现有暂停语义。
			/// </summary>
			class RemoteWfDebugger : public runtime::WfDebugger
			{
			public:
				RemoteWfDebugger();
				~RemoteWfDebugger();

			protected:
				void							OnStartExecution() override;
				void							OnBlockExecution() override;
				void							OnStopExecution() override;
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
				void								Unbind();
				bool								IsBound() const;
				Ptr<runtime::WfDebugger>			GetDebugger() const;

			private:
				Ptr<runtime::WfDebugger>			debugger;
			};
		}
	}
}

#endif

#endif
