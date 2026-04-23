/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGTRANSPORT
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGTRANSPORT

#include "WorkflowDebugProtocol.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			/// <summary>
			/// 传输层骨架先用内存队列模拟消息收发，后面再替换成真正的 socket 分帧实现。
			/// </summary>
			class WorkflowDebugTransport : public Object
			{
			public:
				WorkflowDebugTransport();
				~WorkflowDebugTransport();

				void								Clear();
				bool								IsOpen() const;
				bool								Open();
				void								Close();

				bool								Send(const WorkflowDebugEnvelope& envelope);
				bool								TryReceive(WorkflowDebugEnvelope& envelope);
				void								QueueIncoming(const WorkflowDebugEnvelope& envelope);

			private:
				bool								open = false;
				collections::List<WorkflowDebugEnvelope>	incoming;
				collections::List<WorkflowDebugEnvelope>	outgoing;
			};
		}
	}
}

#endif

#endif
