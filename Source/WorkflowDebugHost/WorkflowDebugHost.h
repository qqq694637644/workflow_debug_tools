/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGHOST
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGHOST

#include "WorkflowDebugProtocol.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			class WorkflowDebugSession;

			/// <summary>
			/// 主进程内调试宿主入口。
			/// 它只负责会话生命周期，不直接理解具体协议字段。
			/// </summary>
			class WorkflowDebugHost : public Object
			{
			public:
				WorkflowDebugHost();
				~WorkflowDebugHost();

				void								Initialize();
				void								Shutdown();

				Ptr<WorkflowDebugSession>			CreateSession(const WString& sessionId);
				bool								DestroySession(const WString& sessionId);
				Ptr<WorkflowDebugSession>			FindSession(const WString& sessionId) const;
				vint								GetSessionCount() const;
				void								Clear();

			private:
				collections::Dictionary<WString, Ptr<WorkflowDebugSession>>	sessions;
			};
		}
	}
}

#endif

#endif
