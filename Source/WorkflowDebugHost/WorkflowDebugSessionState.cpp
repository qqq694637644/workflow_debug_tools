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
				workspaceRoot = WString();
				sourceMapCount = 0;
				lastInboundSeq = 0;
				lastOutboundSeq = 0;
				pendingRequestCount = 0;
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
				workspaceRoot = WString();
				sourceMapCount = 0;
				lastInboundSeq = 0;
				lastOutboundSeq = 0;
				pendingRequestCount = 0;
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
				pendingRequestCount = 0;
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

			void WorkflowDebugSessionState::SetWorkspaceRoot(const WString& value)
			{
				std::lock_guard<std::mutex> guard(mutex);
				workspaceRoot = value;
			}

			const WString& WorkflowDebugSessionState::GetWorkspaceRoot() const
			{
				std::lock_guard<std::mutex> guard(mutex);
				return workspaceRoot;
			}

			void WorkflowDebugSessionState::SetSourceMapCount(vint value)
			{
				CHECK_ERROR(value >= 0, L"sourceMapCount 不能为负数。");
				std::lock_guard<std::mutex> guard(mutex);
				sourceMapCount = value;
			}

			vint WorkflowDebugSessionState::GetSourceMapCount() const
			{
				std::lock_guard<std::mutex> guard(mutex);
				return sourceMapCount;
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

			void WorkflowDebugSessionState::IncrementPendingRequestCount()
			{
				std::lock_guard<std::mutex> guard(mutex);
				pendingRequestCount += 1;
			}

			void WorkflowDebugSessionState::DecrementPendingRequestCount()
			{
				std::lock_guard<std::mutex> guard(mutex);
				CHECK_ERROR(pendingRequestCount > 0, L"没有待处理请求可减少。");
				pendingRequestCount -= 1;
			}

			vint WorkflowDebugSessionState::GetPendingRequestCount() const
			{
				std::lock_guard<std::mutex> guard(mutex);
				return pendingRequestCount;
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
				snapshot.pendingRequestCount = pendingRequestCount;
				snapshot.workspaceRoot = workspaceRoot;
				snapshot.sourceMapCount = sourceMapCount;
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
