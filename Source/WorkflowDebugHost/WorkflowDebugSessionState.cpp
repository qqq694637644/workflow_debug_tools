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
				phase = value;
			}

			WorkflowDebugSessionPhase WorkflowDebugSessionState::GetPhase() const
			{
				return phase;
			}

			void WorkflowDebugSessionState::SetWorkspaceRoot(const WString& value)
			{
				workspaceRoot = value;
			}

			const WString& WorkflowDebugSessionState::GetWorkspaceRoot() const
			{
				return workspaceRoot;
			}

			void WorkflowDebugSessionState::SetSourceMapCount(vint value)
			{
				CHECK_ERROR(value >= 0, L"sourceMapCount 不能为负数。");
				sourceMapCount = value;
			}

			vint WorkflowDebugSessionState::GetSourceMapCount() const
			{
				return sourceMapCount;
			}

			void WorkflowDebugSessionState::SetLastInboundSeq(vint seq)
			{
				CHECK_ERROR(seq >= 0, L"lastInboundSeq 不能为负数。");
				lastInboundSeq = seq;
			}

			vint WorkflowDebugSessionState::GetLastInboundSeq() const
			{
				return lastInboundSeq;
			}

			void WorkflowDebugSessionState::SetLastOutboundSeq(vint seq)
			{
				CHECK_ERROR(seq >= 0, L"lastOutboundSeq 不能为负数。");
				lastOutboundSeq = seq;
			}

			vint WorkflowDebugSessionState::GetLastOutboundSeq() const
			{
				return lastOutboundSeq;
			}

			void WorkflowDebugSessionState::IncrementPendingRequestCount()
			{
				pendingRequestCount += 1;
			}

			void WorkflowDebugSessionState::DecrementPendingRequestCount()
			{
				CHECK_ERROR(pendingRequestCount > 0, L"没有待处理请求可减少。");
				pendingRequestCount -= 1;
			}

			vint WorkflowDebugSessionState::GetPendingRequestCount() const
			{
				return pendingRequestCount;
			}

			void WorkflowDebugSessionState::SetLastStopped(const WString& reason, vint threadId, vint frameId, vint sourceId, vint row)
			{
				lastStoppedReason = reason;
				lastStoppedThreadId = threadId;
				lastStoppedFrameId = frameId;
				lastStoppedSourceId = sourceId;
				lastStoppedRow = row;
			}

			void WorkflowDebugSessionState::ClearLastStopped()
			{
				lastStoppedReason = WString();
				lastStoppedThreadId = -1;
				lastStoppedFrameId = -1;
				lastStoppedSourceId = -1;
				lastStoppedRow = -1;
			}

			WorkflowDebugSessionSnapshot WorkflowDebugSessionState::Snapshot() const
			{
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
