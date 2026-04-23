/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWSTACKINSPECTOR
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWSTACKINSPECTOR

#include "WorkflowDebugProtocol.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			struct WorkflowDebugStackFrame
			{
				vint		threadId = -1;
				vint		frameId = -1;
				vint		sourceId = -1;
				WString		functionName;
				WString		sourcePath;
				vint		row = 0;
				vint		column = 0;
			};

			struct WorkflowDebugStackFrameCollection
			{
				collections::List<WorkflowDebugStackFrame>	frames;

				WorkflowDebugStackFrameCollection() = default;
				WorkflowDebugStackFrameCollection(const WorkflowDebugStackFrameCollection& value)
				{
					CopyFrom(frames, value.frames);
				}

				WorkflowDebugStackFrameCollection& operator=(const WorkflowDebugStackFrameCollection& value)
				{
					if (this != &value)
					{
						frames.Clear();
						CopyFrom(frames, value.frames);
					}
					return *this;
				}
			};

			/// <summary>
			/// 调用栈快照只保存当前暂停现场需要的数据，方便后续接入真实运行时信息。
			/// </summary>
			class WorkflowDebugStackInspector : public Object
			{
			public:
				WorkflowDebugStackInspector();
				~WorkflowDebugStackInspector();

				void								Clear();
				void								ClearThread(vint threadId);

				void								CaptureStack(vint threadId, const collections::List<WorkflowDebugStackFrame>& frames);
				bool								TryGetFrames(vint threadId, collections::List<WorkflowDebugStackFrame>& frames) const;
				bool								TryGetFrame(vint threadId, vint frameId, WorkflowDebugStackFrame& frame) const;

			private:
				collections::Dictionary<WString, WorkflowDebugStackFrameCollection>	framesByThread;
			};
		}
	}
}

#endif

#endif
