/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugSourceCatalog.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			WorkflowDebugSourceCatalog::WorkflowDebugSourceCatalog()
			{
			}

			WorkflowDebugSourceCatalog::~WorkflowDebugSourceCatalog()
			{
			}

			void WorkflowDebugSourceCatalog::Clear()
			{
				entriesByCodeIndex.Clear();
				entriesByPath.Clear();
			}

			vint WorkflowDebugSourceCatalog::Count() const
			{
				return entriesByCodeIndex.Count();
			}

			bool WorkflowDebugSourceCatalog::RegisterSource(vint codeIndex, const WString& sourcePath, vint row)
			{
				CHECK_ERROR(codeIndex >= 0, L"codeIndex 不能为负数。");
				CHECK_ERROR(sourcePath.Length() > 0, L"源码路径不能为空。");
				CHECK_ERROR(row >= 0, L"row 不能为负数。");

				if (auto index = entriesByCodeIndex.Keys().IndexOf(codeIndex); index != -1)
				{
					auto oldRecord = entriesByCodeIndex.Values()[index];
					if (oldRecord.sourcePath != sourcePath)
					{
						entriesByPath.Remove(oldRecord.sourcePath);
					}
				}

				if (auto index = entriesByPath.Keys().IndexOf(sourcePath); index != -1)
				{
					auto oldRecord = entriesByPath.Values()[index];
					if (oldRecord.codeIndex != codeIndex)
					{
						entriesByCodeIndex.Remove(oldRecord.codeIndex);
					}
				}

				WorkflowDebugSourceRecord record;
				record.codeIndex = codeIndex;
				record.sourcePath = sourcePath;
				record.row = row;

				entriesByCodeIndex.Set(codeIndex, record);
				entriesByPath.Set(sourcePath, record);
				return true;
			}

			bool WorkflowDebugSourceCatalog::ResolveByCodeIndex(vint codeIndex, WString& sourcePath, vint& row) const
			{
				if (auto index = entriesByCodeIndex.Keys().IndexOf(codeIndex); index != -1)
				{
					auto record = entriesByCodeIndex.Values()[index];
					sourcePath = record.sourcePath;
					row = record.row;
					return true;
				}

				return false;
			}

			bool WorkflowDebugSourceCatalog::ResolveByPath(const WString& sourcePath, vint& codeIndex, vint& row) const
			{
				if (auto index = entriesByPath.Keys().IndexOf(sourcePath); index != -1)
				{
					auto record = entriesByPath.Values()[index];
					codeIndex = record.codeIndex;
					row = record.row;
					return true;
				}

				return false;
			}
		}
	}
}

#endif
