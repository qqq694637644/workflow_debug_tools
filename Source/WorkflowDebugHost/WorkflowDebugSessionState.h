/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGSESSIONSTATE
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWDEBUGSESSIONSTATE

#include "WorkflowDebugProtocol.h"
#include <mutex>

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			/// <summary>调试会话的阶段。</summary>
			enum class WorkflowDebugSessionPhase
			{
				Idle,
				Connected,
				Negotiating,
				Ready,
				Running,
				Paused,
				Stopped,
				Closed,
			};

			/// <summary>调试会话的快照，仅保留骨架阶段需要的最小状态。</summary>
			struct WorkflowDebugSessionSnapshot
			{
				WString						sessionId;
				WorkflowDebugSessionPhase	phase = WorkflowDebugSessionPhase::Idle;
				vint						lastInboundSeq = 0;
				vint						lastOutboundSeq = 0;
				WString						lastStoppedReason;
				vint						lastStoppedThreadId = -1;
				vint						lastStoppedFrameId = -1;
				vint						lastStoppedSourceId = -1;
				vint						lastStoppedRow = -1;
			};

			/// <summary>
			/// 会话状态机只负责保存调试生命周期信息，不直接解析协议正文。
			/// </summary>
			class WorkflowDebugSessionState : public Object
			{
			public:
				WorkflowDebugSessionState();
				~WorkflowDebugSessionState();

				void								Reset();
				void								Attach(const WString& sessionId);
				void								Detach();

				void								SetPhase(WorkflowDebugSessionPhase phase);
				WorkflowDebugSessionPhase			GetPhase() const;



				void								SetLastInboundSeq(vint seq);
				vint								GetLastInboundSeq() const;

				void								SetLastOutboundSeq(vint seq);
				vint								GetLastOutboundSeq() const;


				void								SetLastStopped(const WString& reason, vint threadId, vint frameId, vint sourceId, vint row);
				void								ClearLastStopped();

				WorkflowDebugSessionSnapshot		Snapshot() const;

			private:
				mutable std::mutex				mutex;
				WString								sessionId;
				WorkflowDebugSessionPhase			phase = WorkflowDebugSessionPhase::Idle;
				vint								lastInboundSeq = 0;
				vint								lastOutboundSeq = 0;
				WString								lastStoppedReason;
				vint								lastStoppedThreadId = -1;
				vint								lastStoppedFrameId = -1;
				vint								lastStoppedSourceId = -1;
				vint								lastStoppedRow = -1;
			};
		}
	}
}

#endif

#endif
