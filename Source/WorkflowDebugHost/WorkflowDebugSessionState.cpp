/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugSessionState.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			WorkflowDebugSessionState::WorkflowDebugSessionState()
			{
				Reset();
			}

			WorkflowDebugSessionState::~WorkflowDebugSessionState()
			{
			}

			void WorkflowDebugSessionState::Reset()
			{
				std::lock_guard<std::mutex> guard(mutex);
				sessionId = WString();
				phase = WorkflowDebugSessionPhase::Idle;
				lastInboundSeq = 0;
				lastOutboundSeq = 0;
				lastStoppedReason = WString();
				lastStoppedThreadId = -1;
				lastStoppedFrameId = -1;
				lastStoppedSourceId = -1;
				lastStoppedRow = -1;
			}

			void WorkflowDebugSessionState::Attach(const WString& value)
			{
				CHECK_ERROR(value.Length() > 0, L"会话标识不能为空。");
				std::lock_guard<std::mutex> guard(mutex);
				sessionId = value;
				phase = WorkflowDebugSessionPhase::Connected;
				lastInboundSeq = 0;
				lastOutboundSeq = 0;
				lastStoppedReason = WString();
				lastStoppedThreadId = -1;
				lastStoppedFrameId = -1;
				lastStoppedSourceId = -1;
				lastStoppedRow = -1;
			}

			void WorkflowDebugSessionState::Detach()
			{
				std::lock_guard<std::mutex> guard(mutex);
				phase = WorkflowDebugSessionPhase::Closed;
				lastStoppedReason = WString();
				lastStoppedThreadId = -1;
				lastStoppedFrameId = -1;
				lastStoppedSourceId = -1;
				lastStoppedRow = -1;
			}

			void WorkflowDebugSessionState::SetPhase(WorkflowDebugSessionPhase value)
			{
				std::lock_guard<std::mutex> guard(mutex);
				phase = value;
			}

			WorkflowDebugSessionPhase WorkflowDebugSessionState::GetPhase() const
			{
				std::lock_guard<std::mutex> guard(mutex);
				return phase;
			}

			void WorkflowDebugSessionState::SetLastInboundSeq(vint seq)
			{
				CHECK_ERROR(seq >= 0, L"lastInboundSeq 不能为负数。");
				std::lock_guard<std::mutex> guard(mutex);
				lastInboundSeq = seq;
			}

			vint WorkflowDebugSessionState::GetLastInboundSeq() const
			{
				std::lock_guard<std::mutex> guard(mutex);
				return lastInboundSeq;
			}

			void WorkflowDebugSessionState::SetLastOutboundSeq(vint seq)
			{
				CHECK_ERROR(seq >= 0, L"lastOutboundSeq 不能为负数。");
				std::lock_guard<std::mutex> guard(mutex);
				lastOutboundSeq = seq;
			}

			vint WorkflowDebugSessionState::GetLastOutboundSeq() const
			{
				std::lock_guard<std::mutex> guard(mutex);
				return lastOutboundSeq;
			}

			void WorkflowDebugSessionState::SetLastStopped(const WString& reason, vint threadId, vint frameId, vint sourceId, vint row)
			{
				std::lock_guard<std::mutex> guard(mutex);
				lastStoppedReason = reason;
				lastStoppedThreadId = threadId;
				lastStoppedFrameId = frameId;
				lastStoppedSourceId = sourceId;
				lastStoppedRow = row;
			}

			void WorkflowDebugSessionState::ClearLastStopped()
			{
				std::lock_guard<std::mutex> guard(mutex);
				lastStoppedReason = WString();
				lastStoppedThreadId = -1;
				lastStoppedFrameId = -1;
				lastStoppedSourceId = -1;
				lastStoppedRow = -1;
			}

			WorkflowDebugSessionSnapshot WorkflowDebugSessionState::Snapshot() const
			{
				std::lock_guard<std::mutex> guard(mutex);
				WorkflowDebugSessionSnapshot snapshot;
				snapshot.sessionId = sessionId;
				snapshot.phase = phase;
				snapshot.lastInboundSeq = lastInboundSeq;
				snapshot.lastOutboundSeq = lastOutboundSeq;
				snapshot.lastStoppedReason = lastStoppedReason;
				snapshot.lastStoppedThreadId = lastStoppedThreadId;
				snapshot.lastStoppedFrameId = lastStoppedFrameId;
				snapshot.lastStoppedSourceId = lastStoppedSourceId;
				snapshot.lastStoppedRow = lastStoppedRow;
				return snapshot;
			}
		}
	}
}

#endif
