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

			void WorkflowDebugBreakpointRegistry::ClearSource(const WString& sourcePath)
			{
				if (sourcePath.Length() == 0)
				{
					return;
				}

				for (vint i = breakpoints.Count() - 1; i >= 0; --i)
				{
					if (breakpoints[i].sourcePath == sourcePath)
					{
						breakpoints.RemoveAt(i);
					}
				}
			}

			vint WorkflowDebugBreakpointRegistry::Count() const
			{
				return breakpoints.Count();
			}

			vint WorkflowDebugBreakpointRegistry::RegisterBreakpoint(const WorkflowDebugBreakpointRecord& breakpoint)
			{
				auto record = breakpoint;
				record.verified = record.codeIndex >= 0;
				record.reason = WString();

				if (sourceCatalog && record.sourcePath.Length() > 0)
				{
					vint resolvedCodeIndex = -1;
					vint resolvedRow = 0;
					if (sourceCatalog->ResolveByPath(record.sourcePath, resolvedCodeIndex, resolvedRow))
					{
						(void)resolvedRow;
						if (record.codeIndex < 0)
						{
							record.codeIndex = resolvedCodeIndex;
						}

						record.verified = record.codeIndex == resolvedCodeIndex;
						if (!record.verified)
						{
							record.reason = L"源码路径与 codeIndex 不匹配。";
						}
					}
					else if (record.codeIndex < 0)
					{
						record.reason = L"找不到对应的源码路径。";
					}
				}
				else if (record.codeIndex < 0)
				{
					record.reason = L"缺少源码路径或 codeIndex。";
				}

				if (!record.verified && record.reason.Length() == 0)
				{
					record.reason = L"断点未通过基础校验。";
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
