/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWSOURCESCATALOG
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWSOURCESCATALOG

#include "WorkflowDebugProtocol.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			struct WorkflowDebugSourceRecord
			{
				vint		codeIndex = -1;
				WString		sourcePath;
				vint		row = 0;
			};

			/// <summary>
			/// 维护 codeIndex 与真实源码路径的对应关系。
			/// 这里先把路径映射和源码目录做成独立模块，避免后续协议解析和路径归一化混在一起。
			/// </summary>
			class WorkflowDebugSourceCatalog : public Object
			{
			public:
				WorkflowDebugSourceCatalog();
				~WorkflowDebugSourceCatalog();

				void								Clear();
				vint								Count() const;

				bool								RegisterSource(vint codeIndex, const WString& sourcePath, vint row = 0);
				bool								ResolveByCodeIndex(vint codeIndex, WString& sourcePath, vint& row) const;
				bool								ResolveByPath(const WString& sourcePath, vint& codeIndex, vint& row) const;

			private:
				collections::Dictionary<vint, WorkflowDebugSourceRecord>	entriesByCodeIndex;
				collections::Dictionary<WString, WorkflowDebugSourceRecord>	entriesByPath;
			};
		}
	}
}

#endif

#endif
