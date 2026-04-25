/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWBREAKPOINTREGISTRY
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWBREAKPOINTREGISTRY

#include "WorkflowDebugSourceCatalog.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			// Remote debug: only keep line breakpoints.
			struct WorkflowDebugBreakpointRecord
			{
				WString		breakpointId;
				WString		sourcePath;
				vint		codeIndex = -1;
				vint		row = 0;
				bool		verified = false;
				WString		reason;
			};

			/// <summary>
			/// 断点登记器只保存断点和校验结果，后续再接入真正的 WfDebugger。
			/// </summary>
			class WorkflowDebugBreakpointRegistry : public Object
			{
			public:
				explicit WorkflowDebugBreakpointRegistry(WorkflowDebugSourceCatalog* sourceCatalog = nullptr);
				~WorkflowDebugBreakpointRegistry();

				void							SetSourceCatalog(WorkflowDebugSourceCatalog* sourceCatalog);
				void							Clear();
				void							ClearSource(const WString& sourcePath);
				vint							Count() const;

				vint							RegisterBreakpoint(const WorkflowDebugBreakpointRecord& breakpoint);
				bool							HasBreakpoint(vint codeIndex, vint row) const;
				const collections::List<WorkflowDebugBreakpointRecord>&	GetBreakpoints() const;

			private:
				WorkflowDebugSourceCatalog*							sourceCatalog = nullptr;
				collections::List<WorkflowDebugBreakpointRecord>	breakpoints;
			};
		}
	}
}

#endif

#endif
