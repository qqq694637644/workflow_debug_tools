/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGTRANSPORT
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGTRANSPORT

#include "WorkflowDebugProtocol.h"
#include <memory>

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			/// <summary>
			/// 传输层默认保留内存队列模式，便于骨架阶段和单元测试使用。
			/// 当调用 Connect() 时，会切换为真正的 TCP 客户端并按行发送 JSON 消息。
			/// </summary>
			class WorkflowDebugTransport : public Object
			{
			public:
				WorkflowDebugTransport();
				~WorkflowDebugTransport();

				void								Clear();
				bool								IsOpen() const;

				void								SetEndpoint(const WString& host, vint port);
				WString								GetEndpointHost() const;
				vint								GetEndpointPort() const;

				bool								Open();
				bool								Connect();
				bool								Connect(const WString& host, vint port);
				void								Close();

				bool								Send(const WorkflowDebugEnvelope& envelope);
				bool								TryReceive(WorkflowDebugEnvelope& envelope);
				void								QueueIncoming(const WorkflowDebugEnvelope& envelope);

			private:
				struct WorkflowDebugTransportImpl;
				std::unique_ptr<WorkflowDebugTransportImpl>	impl;
			};
		}
	}
}

#endif

#endif
