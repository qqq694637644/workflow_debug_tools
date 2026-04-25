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
					/// TCP 传输层：宿主作为客户端连接到调试适配器，并按行发送/接收 JSON envelope。
					/// 仅保留远程调试主流程所需的最小语义，不再支持内存队列模式。
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
bool								Connect();
				bool								Connect(const WString& host, vint port);
				void								Close();

				bool								Send(const WorkflowDebugEnvelope& envelope);
				bool								TryReceive(WorkflowDebugEnvelope& envelope);
private:
				struct WorkflowDebugTransportImpl;
				std::unique_ptr<WorkflowDebugTransportImpl>	impl;
			};
		}
	}
}

#endif

#endif
