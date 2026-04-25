/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

接口:
***********************************************************************/

#ifndef VCZH_WORKFLOW_DEBUGHOST_WORKFLOWVALUEINSPECTOR
#define VCZH_WORKFLOW_DEBUGHOST_WORKFLOWVALUEINSPECTOR

#include "WorkflowDebugProtocol.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			/// <summary>变量作用域的分类。</summary>
			enum class WorkflowDebugScopeKind
			{
				Local,
				Argument,
				Captured,
				Global,
				Object,
			};

			struct WorkflowDebugVariable
			{
				WString		name;
				WString		type;
				WString		value;
				vint		variablesReference = 0;
				bool		canExpand = false;
				vint		namedVariables = 0;
				vint		indexedVariables = 0;
			};

			struct WorkflowDebugScope
			{
				WorkflowDebugScopeKind	kind = WorkflowDebugScopeKind::Local;
				WString					name;
				vint					variablesReference = 0;
				bool					canExpand = false;
				vint					namedVariables = 0;
				vint					indexedVariables = 0;
			};

			struct WorkflowDebugFrameValues
			{
				collections::List<WorkflowDebugVariable>	local;
				collections::List<WorkflowDebugVariable>	argument;
				collections::List<WorkflowDebugVariable>	captured;
				collections::List<WorkflowDebugVariable>	global;

				WorkflowDebugFrameValues() = default;
				WorkflowDebugFrameValues(const WorkflowDebugFrameValues& value)
				{
					CopyFrom(local, value.local);
					CopyFrom(argument, value.argument);
					CopyFrom(captured, value.captured);
					CopyFrom(global, value.global);
				}

				WorkflowDebugFrameValues& operator=(const WorkflowDebugFrameValues& value)
				{
					if (this != &value)
					{
						local.Clear();
						argument.Clear();
						captured.Clear();
						global.Clear();
						CopyFrom(local, value.local);
						CopyFrom(argument, value.argument);
						CopyFrom(captured, value.captured);
						CopyFrom(global, value.global);
					}
					return *this;
				}
			};

			/// <summary>
			/// 变量检查器负责缓存单个暂停现场的变量快照。
			///
			/// 收敛后的约定：
			/// - Host 为每个 scope 分配稳定的 variablesReference。
			/// - Adapter 直接透传 variablesReference，不再维护额外的句柄表。
			/// </summary>
			class WorkflowDebugValueInspector : public Object
			{
			public:
				WorkflowDebugValueInspector();
				~WorkflowDebugValueInspector();

				void							Clear();
				void							ClearThread(vint threadId);

				void							CaptureFrame(vint threadId, vint frameId, const WorkflowDebugFrameValues& values);
				bool							TryGetFrame(vint threadId, vint frameId, WorkflowDebugFrameValues& values) const;
				bool							TryGetScopes(vint threadId, vint frameId, collections::List<WorkflowDebugScope>& scopes) const;
				bool							TryGetVariables(vint threadId, vint frameId, WorkflowDebugScopeKind kind, collections::List<WorkflowDebugVariable>& variables) const;

				/// <summary>
				/// 通过 variablesReference 获取变量列表，并返回它关联的 threadId/frameId/kind。
				/// </summary>
				bool							TryGetVariables(vint variablesReference, vint& threadId, vint& frameId, WorkflowDebugScopeKind& kind, collections::List<WorkflowDebugVariable>& variables) const;

			private:
				struct ScopeReferenceInfo
				{
					vint					threadId = 0;
					vint					frameId = 0;
					WorkflowDebugScopeKind	kind = WorkflowDebugScopeKind::Local;
				};

				collections::Dictionary<WString, WorkflowDebugFrameValues>	framesByKey;
				mutable collections::Dictionary<WString, vint>					scopeReferences;
				mutable collections::Dictionary<vint, ScopeReferenceInfo>		scopeReferenceInfos;
				mutable vint												nextScopeReference = 1;

				vint							GetScopeReference(vint threadId, vint frameId, WorkflowDebugScopeKind kind) const;
			};
		}
	}
}

#endif

#endif
