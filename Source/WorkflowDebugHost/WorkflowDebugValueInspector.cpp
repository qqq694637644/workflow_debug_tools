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

			static WString BuildScopeKey(vint threadId, vint frameId, WorkflowDebugScopeKind kind)
			{
				return itow(threadId) + L":" + itow(frameId) + L":" + itow((vint)kind);
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
				scopeReferences.Clear();
					scopeReferenceInfos.Clear();
				nextScopeReference = 1;
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

				keysToRemove.Clear();
				auto scopePrefix = itow(threadId) + L":";
				for (auto key : scopeReferences.Keys())
				{
					if (key.Length() >= scopePrefix.Length() && key.Left(scopePrefix.Length()) == scopePrefix)
					{
						keysToRemove.Add(key);
					}
				}

				for (auto key : keysToRemove)
				{
					scopeReferences.Remove(key);
				}
					// Remove scope reference infos for this thread.
					collections::List<vint> refsToRemove;
					for (auto ref : scopeReferenceInfos.Keys())
					{
						ScopeReferenceInfo info;
						if (scopeReferenceInfos.TryGetValue(ref, info) && info.threadId == threadId)
						{
							refsToRemove.Add(ref);
						}
					}
					for (auto ref : refsToRemove)
					{
						scopeReferenceInfos.Remove(ref);
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

					vint WorkflowDebugValueInspector::GetScopeReference(vint threadId, vint frameId, WorkflowDebugScopeKind kind) const
					{
						// A scope must have a stable non-zero variablesReference so the adapter can expand it.
						auto key = BuildScopeKey(threadId, frameId, kind);
						vint reference = 0;
						if (auto index = scopeReferences.Keys().IndexOf(key); index != -1)
						{
							reference = scopeReferences.Values()[index];
						}
						else
						{
							reference = nextScopeReference++;
							scopeReferences.Set(key, reference);
						}

						ScopeReferenceInfo info;
						info.threadId = threadId;
						info.frameId = frameId;
						info.kind = kind;
						scopeReferenceInfos.Set(reference, info);

						return reference;
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
				localScope.variablesReference = GetScopeReference(threadId, frameId, localScope.kind);
				localScope.canExpand = true;
				localScope.namedVariables = values.local.Count();
				scopes.Add(localScope);

				WorkflowDebugScope argumentScope;
				argumentScope.kind = WorkflowDebugScopeKind::Argument;
				argumentScope.name = ScopeKindToName(argumentScope.kind);
				argumentScope.variablesReference = GetScopeReference(threadId, frameId, argumentScope.kind);
				argumentScope.canExpand = true;
				argumentScope.namedVariables = values.argument.Count();
				scopes.Add(argumentScope);

				WorkflowDebugScope capturedScope;
				capturedScope.kind = WorkflowDebugScopeKind::Captured;
				capturedScope.name = ScopeKindToName(capturedScope.kind);
				capturedScope.variablesReference = GetScopeReference(threadId, frameId, capturedScope.kind);
				capturedScope.canExpand = true;
				capturedScope.namedVariables = values.captured.Count();
				scopes.Add(capturedScope);

				WorkflowDebugScope globalScope;
				globalScope.kind = WorkflowDebugScopeKind::Global;
				globalScope.name = ScopeKindToName(globalScope.kind);
				globalScope.variablesReference = GetScopeReference(threadId, frameId, globalScope.kind);
				globalScope.canExpand = true;
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
				bool WorkflowDebugValueInspector::TryGetVariables(vint variablesReference, vint& threadId, vint& frameId, WorkflowDebugScopeKind& kind, collections::List<WorkflowDebugVariable>& variables) const
				{
					ScopeReferenceInfo info;
					if (!scopeReferenceInfos.TryGetValue(variablesReference, info))
					{
						return false;
					}

					threadId = info.threadId;
					frameId = info.frameId;
					kind = info.kind;
					return TryGetVariables(threadId, frameId, kind, variables);
				}

		}
	}
}

#endif
