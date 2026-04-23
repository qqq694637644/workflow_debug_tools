/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugStackInspector.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			static WString BuildThreadKey(vint threadId)
			{
				return itow(threadId);
			}

			WorkflowDebugStackInspector::WorkflowDebugStackInspector()
			{
			}

			WorkflowDebugStackInspector::~WorkflowDebugStackInspector()
			{
			}

			void WorkflowDebugStackInspector::Clear()
			{
				framesByThread.Clear();
			}

			void WorkflowDebugStackInspector::ClearThread(vint threadId)
			{
				framesByThread.Remove(BuildThreadKey(threadId));
			}

			void WorkflowDebugStackInspector::CaptureStack(vint threadId, const collections::List<WorkflowDebugStackFrame>& frames)
			{
				CHECK_ERROR(threadId >= 0, L"threadId 不能为负数。");
				WorkflowDebugStackFrameCollection collection;
				CopyFrom(collection.frames, frames);
				framesByThread.Set(BuildThreadKey(threadId), collection);
			}

			bool WorkflowDebugStackInspector::TryGetFrames(vint threadId, collections::List<WorkflowDebugStackFrame>& frames) const
			{
				auto key = BuildThreadKey(threadId);
				if (auto index = framesByThread.Keys().IndexOf(key); index != -1)
				{
					CopyFrom(frames, framesByThread.Values()[index].frames);
					return true;
				}

				return false;
			}

			bool WorkflowDebugStackInspector::TryGetFrame(vint threadId, vint frameId, WorkflowDebugStackFrame& frame) const
			{
				collections::List<WorkflowDebugStackFrame> frames;
				if (!TryGetFrames(threadId, frames))
				{
					return false;
				}

				for (auto item : frames)
				{
					if (item.frameId == frameId)
					{
						frame = item;
						return true;
					}
				}

				return false;
			}
		}
	}
}

#endif
