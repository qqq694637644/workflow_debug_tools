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
			namespace
			{
				static WString NormalizeSourcePath(const WString& sourcePath)
				{
					if (sourcePath.Length() == 0)
					{
						return sourcePath;
					}

					// 保持和调试适配器一致的最小归一化规则，避免同一路径因斜杠或盘符大小写不同而失配。
					if (sourcePath.Length() >= 17 && sourcePath.Left(17) == L"unknown://source/")
					{
						return sourcePath;
					}

					collections::Array<wchar_t> normalized(sourcePath.Length());
					for (vint i = 0; i < sourcePath.Length(); i++)
					{
						auto ch = sourcePath[i];
						if (ch == L'\\')
						{
							ch = L'/';
						}
						normalized[i] = ch;
					}

					if (normalized.Count() >= 2
						&& normalized[1] == L':'
						&& normalized[0] >= L'A'
						&& normalized[0] <= L'Z')
					{
						normalized[0] = (wchar_t)(normalized[0] - L'A' + L'a');
					}

					vint length = normalized.Count();
					while (length > 1 && normalized[length - 1] == L'/')
					{
						length--;
					}

					if (length == 0)
					{
						return WString::Empty;
					}

					return WString::CopyFrom(&normalized[0], length);
				}
			}

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

				auto normalizedPath = NormalizeSourcePath(sourcePath);

				if (auto index = entriesByCodeIndex.Keys().IndexOf(codeIndex); index != -1)
				{
					auto oldRecord = entriesByCodeIndex.Values()[index];
					auto oldNormalizedPath = NormalizeSourcePath(oldRecord.sourcePath);
					if (oldNormalizedPath != normalizedPath)
					{
						entriesByPath.Remove(oldNormalizedPath);
					}
				}

				if (auto index = entriesByPath.Keys().IndexOf(normalizedPath); index != -1)
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
				entriesByPath.Set(normalizedPath, record);
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
				auto normalizedPath = NormalizeSourcePath(sourcePath);
				if (auto index = entriesByPath.Keys().IndexOf(normalizedPath); index != -1)
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
