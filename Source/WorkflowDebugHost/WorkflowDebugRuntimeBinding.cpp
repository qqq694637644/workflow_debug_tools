/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugRuntimeBinding.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			RemoteWfDebugger::RemoteWfDebugger()
			{
			}

			RemoteWfDebugger::~RemoteWfDebugger()
			{
			}

			void RemoteWfDebugger::OnStartExecution()
			{
			}

			void RemoteWfDebugger::OnBlockExecution()
			{
				// 骨架阶段先直接恢复执行，避免调试线程被占住。
				Run();
			}

			void RemoteWfDebugger::OnStopExecution()
			{
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
				if (debugger)
				{
					workflow::runtime::SetDebuggerForCurrentThread(debugger);
				}
			}

			void WorkflowDebugRuntimeBinding::Unbind()
			{
				if (debugger)
				{
					// 这里不要再走 SetDebuggerForCurrentThread(nullptr)，直接 Reset 更稳。
					workflow::runtime::ResetDebuggerForCurrentThread();
					debugger = nullptr;
				}
			}

			bool WorkflowDebugRuntimeBinding::IsBound() const
			{
				return debugger != nullptr;
			}

			Ptr<runtime::WfDebugger> WorkflowDebugRuntimeBinding::GetDebugger() const
			{
				return debugger;
			}
		}
	}
}

#endif
