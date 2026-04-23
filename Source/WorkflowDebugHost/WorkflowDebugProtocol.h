/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGPROTOCOL
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGPROTOCOL

#include "../Runtime/WfRuntimeDebugger.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			/// <summary>
			/// 调试宿主与适配器之间统一使用的消息外壳。
			/// 骨架阶段先保留原始文本 body，后续再替换成严格的协议解析。
			/// </summary>
			enum class WorkflowDebugEnvelopeKind
			{
				Request,
				Response,
				Event,
			};

			struct WorkflowDebugEnvelope
			{
				WorkflowDebugEnvelopeKind	kind = WorkflowDebugEnvelopeKind::Request;
				WString						command;
				WString						sessionId;
				vint						seq = -1;
				vint						replyTo = -1;
				WString						body;
			};
		}
	}
}

#endif

#endif
