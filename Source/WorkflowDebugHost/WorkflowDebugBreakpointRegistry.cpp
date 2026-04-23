/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugBreakpointRegistry.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			WorkflowDebugBreakpointRegistry::WorkflowDebugBreakpointRegistry(WorkflowDebugSourceCatalog* catalog)
				:sourceCatalog(catalog)
			{
			}

			WorkflowDebugBreakpointRegistry::~WorkflowDebugBreakpointRegistry()
			{
			}

			void WorkflowDebugBreakpointRegistry::SetSourceCatalog(WorkflowDebugSourceCatalog* catalog)
			{
				sourceCatalog = catalog;
			}

			void WorkflowDebugBreakpointRegistry::Clear()
			{
				breakpoints.Clear();
			}

			vint WorkflowDebugBreakpointRegistry::Count() const
			{
				return breakpoints.Count();
			}

			vint WorkflowDebugBreakpointRegistry::RegisterBreakpoint(const WorkflowDebugBreakpointRecord& breakpoint)
			{
				auto record = breakpoint;
				if (record.codeIndex < 0 && sourceCatalog && record.sourcePath.Length() > 0)
				{
					vint resolvedCodeIndex = -1;
					vint resolvedRow = 0;
					if (sourceCatalog->ResolveByPath(record.sourcePath, resolvedCodeIndex, resolvedRow))
					{
						record.codeIndex = resolvedCodeIndex;
						record.row = resolvedRow;
					}
				}

				breakpoints.Add(record);
				return breakpoints.Count() - 1;
			}

			bool WorkflowDebugBreakpointRegistry::HasBreakpoint(vint codeIndex, vint row) const
			{
				for (auto breakpoint : breakpoints)
				{
					if (breakpoint.codeIndex == codeIndex && breakpoint.row == row)
					{
						return true;
					}
				}

				return false;
			}

			const collections::List<WorkflowDebugBreakpointRecord>& WorkflowDebugBreakpointRegistry::GetBreakpoints() const
			{
				return breakpoints;
			}
		}
	}
}

#endif
