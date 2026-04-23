/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugValueInspector.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			static WString BuildFrameKey(vint threadId, vint frameId)
			{
				return itow(threadId) + L":" + itow(frameId);
			}

			static WString ScopeKindToName(WorkflowDebugScopeKind kind)
			{
				switch (kind)
				{
				case WorkflowDebugScopeKind::Local:
					return L"局部";
				case WorkflowDebugScopeKind::Argument:
					return L"参数";
				case WorkflowDebugScopeKind::Captured:
					return L"捕获";
				case WorkflowDebugScopeKind::Global:
					return L"全局";
				case WorkflowDebugScopeKind::Object:
					return L"对象";
				default:
					return L"未知";
				}
			}

			WorkflowDebugValueInspector::WorkflowDebugValueInspector()
			{
			}

			WorkflowDebugValueInspector::~WorkflowDebugValueInspector()
			{
			}

			void WorkflowDebugValueInspector::Clear()
			{
				framesByKey.Clear();
			}

			void WorkflowDebugValueInspector::ClearThread(vint threadId)
			{
				auto prefix = itow(threadId) + L":";
				collections::List<WString> keysToRemove;
				for (auto key : framesByKey.Keys())
				{
					if (key.Length() >= prefix.Length() && key.Left(prefix.Length()) == prefix)
					{
						keysToRemove.Add(key);
					}
				}

				for (auto key : keysToRemove)
				{
					framesByKey.Remove(key);
				}
			}

			void WorkflowDebugValueInspector::CaptureFrame(vint threadId, vint frameId, const WorkflowDebugFrameValues& values)
			{
				CHECK_ERROR(threadId >= 0, L"threadId 不能为负数。");
				CHECK_ERROR(frameId >= 0, L"frameId 不能为负数。");
				framesByKey.Set(BuildFrameKey(threadId, frameId), values);
			}

			bool WorkflowDebugValueInspector::TryGetFrame(vint threadId, vint frameId, WorkflowDebugFrameValues& values) const
			{
				auto key = BuildFrameKey(threadId, frameId);
				if (auto index = framesByKey.Keys().IndexOf(key); index != -1)
				{
					auto source = framesByKey.Values()[index];
					values.local.Clear();
					values.argument.Clear();
					values.captured.Clear();
					values.global.Clear();
					CopyFrom(values.local, source.local);
					CopyFrom(values.argument, source.argument);
					CopyFrom(values.captured, source.captured);
					CopyFrom(values.global, source.global);
					return true;
				}

				return false;
			}

			bool WorkflowDebugValueInspector::TryGetScopes(vint threadId, vint frameId, collections::List<WorkflowDebugScope>& scopes) const
			{
				WorkflowDebugFrameValues values;
				if (!TryGetFrame(threadId, frameId, values))
				{
					return false;
				}

				scopes.Clear();

				WorkflowDebugScope localScope;
				localScope.kind = WorkflowDebugScopeKind::Local;
				localScope.name = ScopeKindToName(localScope.kind);
				localScope.variablesReference = 0;
				localScope.namedVariables = values.local.Count();
				scopes.Add(localScope);

				WorkflowDebugScope argumentScope;
				argumentScope.kind = WorkflowDebugScopeKind::Argument;
				argumentScope.name = ScopeKindToName(argumentScope.kind);
				argumentScope.variablesReference = 0;
				argumentScope.namedVariables = values.argument.Count();
				scopes.Add(argumentScope);

				WorkflowDebugScope capturedScope;
				capturedScope.kind = WorkflowDebugScopeKind::Captured;
				capturedScope.name = ScopeKindToName(capturedScope.kind);
				capturedScope.variablesReference = 0;
				capturedScope.namedVariables = values.captured.Count();
				scopes.Add(capturedScope);

				WorkflowDebugScope globalScope;
				globalScope.kind = WorkflowDebugScopeKind::Global;
				globalScope.name = ScopeKindToName(globalScope.kind);
				globalScope.variablesReference = 0;
				globalScope.namedVariables = values.global.Count();
				scopes.Add(globalScope);

				return true;
			}

			bool WorkflowDebugValueInspector::TryGetVariables(vint threadId, vint frameId, WorkflowDebugScopeKind kind, collections::List<WorkflowDebugVariable>& variables) const
			{
				WorkflowDebugFrameValues values;
				if (!TryGetFrame(threadId, frameId, values))
				{
					return false;
				}

				variables.Clear();
				switch (kind)
				{
				case WorkflowDebugScopeKind::Local:
					CopyFrom(variables, values.local);
					return true;
				case WorkflowDebugScopeKind::Argument:
					CopyFrom(variables, values.argument);
					return true;
				case WorkflowDebugScopeKind::Captured:
					CopyFrom(variables, values.captured);
					return true;
				case WorkflowDebugScopeKind::Global:
					CopyFrom(variables, values.global);
					return true;
				case WorkflowDebugScopeKind::Object:
					return true;
				default:
					return false;
				}
			}
		}
	}
}

#endif
