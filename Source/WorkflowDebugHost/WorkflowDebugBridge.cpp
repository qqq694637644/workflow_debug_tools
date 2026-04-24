/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugBridge.h"
#include "WorkflowDebugBreakpointRegistry.h"
#include "WorkflowDebugRuntimeBinding.h"
#include "WorkflowDebugSessionState.h"
#include "WorkflowDebugSourceCatalog.h"
#include "WorkflowDebugStackInspector.h"
#include "WorkflowDebugTransport.h"
#include "WorkflowDebugValueInspector.h"
#include <VlppGlrParser.h>
#include <limits>

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			using namespace glr::json;

			namespace
			{
				static Ptr<JsonObjectField> CreateField(const WString& name, Ptr<JsonNode> value)
				{
					auto field = Ptr(new JsonObjectField);
					field->name.value = name;
					field->value = value;
					return field;
				}

				static Ptr<JsonString> CreateString(const WString& value)
				{
					auto node = Ptr(new JsonString);
					node->content.value = value;
					return node;
				}

				static Ptr<JsonNumber> CreateNumber(vint value)
				{
					auto node = Ptr(new JsonNumber);
					node->content.value = itow(value);
					return node;
				}

				static Ptr<JsonArray> CreateArray()
				{
					return Ptr(new JsonArray);
				}

				static Ptr<JsonObject> CreateObject()
				{
					return Ptr(new JsonObject);
				}

				static void AddField(JsonObject* object, const WString& name, Ptr<JsonNode> value)
				{
					object->fields.Add(CreateField(name, value));
				}

				static Ptr<JsonNode> ParseJsonText(const WString& text)
				{
					if (text.Length() == 0)
					{
						return nullptr;
					}

					glr::json::Parser parser;
					return JsonParse(text, parser);
				}

				static JsonObject* ParseJsonObject(const WString& text)
				{
					auto node = ParseJsonText(text);
					return dynamic_cast<JsonObject*>(node.Obj());
				}

				static bool TryGetField(JsonObject* object, const WString& name, Ptr<JsonNode>& value)
				{
					if (!object)
					{
						return false;
					}

					for (auto field : object->fields)
					{
						if (field->name.value == name)
						{
							value = field->value;
							return true;
						}
					}

					return false;
				}

				static bool TryReadStringField(JsonObject* object, const WString& name, WString& value)
				{
					Ptr<JsonNode> node;
					if (!TryGetField(object, name, node))
					{
						return false;
					}

					if (auto stringNode = dynamic_cast<JsonString*>(node.Obj()))
					{
						value = stringNode->content.value;
						return true;
					}

					return false;
				}

				static bool TryReadNumberField(JsonObject* object, const WString& name, vint& value)
				{
					Ptr<JsonNode> node;
					if (!TryGetField(object, name, node))
					{
						return false;
					}

					vint64_t parsedValue = 0;
					if (auto numberNode = dynamic_cast<JsonNumber*>(node.Obj()))
					{
						parsedValue = wtoi64(numberNode->content.value);
					}
					else if (auto stringNode = dynamic_cast<JsonString*>(node.Obj()))
					{
						parsedValue = wtoi64(stringNode->content.value);
					}
					else
					{
						return false;
					}

					if (parsedValue < (std::numeric_limits<vint>::min)() || parsedValue > (std::numeric_limits<vint>::max)())
					{
						return false;
					}

					value = (vint)parsedValue;
					return true;
				}

				static bool TryReadScopeKindField(JsonObject* object, const WString& name, WorkflowDebugScopeKind& kind)
				{
					WString text;
					if (!TryReadStringField(object, name, text))
					{
						return false;
					}

					if (text == L"Local")
					{
						kind = WorkflowDebugScopeKind::Local;
						return true;
					}
					if (text == L"Argument")
					{
						kind = WorkflowDebugScopeKind::Argument;
						return true;
					}
					if (text == L"Captured")
					{
						kind = WorkflowDebugScopeKind::Captured;
						return true;
					}
					if (text == L"Global")
					{
						kind = WorkflowDebugScopeKind::Global;
						return true;
					}
					if (text == L"Object")
					{
						kind = WorkflowDebugScopeKind::Object;
						return true;
					}

					return false;
				}

				static WString ScopeKindToText(WorkflowDebugScopeKind kind)
				{
					switch (kind)
					{
					case WorkflowDebugScopeKind::Local:
						return L"Local";
					case WorkflowDebugScopeKind::Argument:
						return L"Argument";
					case WorkflowDebugScopeKind::Captured:
						return L"Captured";
					case WorkflowDebugScopeKind::Global:
						return L"Global";
					case WorkflowDebugScopeKind::Object:
						return L"Object";
					default:
						return L"Unknown";
					}
				}

				static Ptr<JsonObject> BuildStackFrame(const WorkflowDebugStackFrame& frame)
				{
					auto object = CreateObject();
					AddField(object.Obj(), L"frameId", CreateNumber(frame.frameId));
					AddField(object.Obj(), L"threadId", CreateNumber(frame.threadId));
					AddField(object.Obj(), L"sourceId", CreateNumber(frame.sourceId));
					AddField(object.Obj(), L"sourcePath", CreateString(frame.sourcePath));
					AddField(object.Obj(), L"functionName", CreateString(frame.functionName));
					AddField(object.Obj(), L"row", CreateNumber(frame.row));
					AddField(object.Obj(), L"column", CreateNumber(frame.column));
					return object;
				}

				static Ptr<JsonObject> BuildScope(const WorkflowDebugScope& scope)
				{
					auto object = CreateObject();
					AddField(object.Obj(), L"kind", CreateString(ScopeKindToText(scope.kind)));
					AddField(object.Obj(), L"name", CreateString(scope.name));
					AddField(object.Obj(), L"variablesReference", CreateNumber(scope.variablesReference));
					AddField(object.Obj(), L"namedVariables", CreateNumber(scope.namedVariables));
					AddField(object.Obj(), L"indexedVariables", CreateNumber(scope.indexedVariables));
					return object;
				}

				static Ptr<JsonObject> BuildVariable(const WorkflowDebugVariable& variable)
				{
					auto object = CreateObject();
					AddField(object.Obj(), L"name", CreateString(variable.name));
					AddField(object.Obj(), L"type", CreateString(variable.type));
					AddField(object.Obj(), L"value", CreateString(variable.value));
					AddField(object.Obj(), L"variablesReference", CreateNumber(variable.variablesReference));
					AddField(object.Obj(), L"canExpand", CreateNumber(variable.canExpand ? 1 : 0));
					AddField(object.Obj(), L"namedVariables", CreateNumber(variable.namedVariables));
					AddField(object.Obj(), L"indexedVariables", CreateNumber(variable.indexedVariables));
					return object;
				}

				static WString SerializeJsonNode(Ptr<JsonNode> node)
				{
					if (!node)
					{
						return WString();
					}
					return JsonToString(node);
				}

				static vint ResolveThreadId(const WorkflowDebugEnvelope& envelope, WorkflowDebugSessionState* state)
				{
					vint threadId = -1;
					auto body = ParseJsonObject(envelope.body);
					if (body)
					{
						TryReadNumberField(body, L"threadId", threadId);
					}

					if (threadId < 0 && state)
					{
						threadId = state->Snapshot().lastStoppedThreadId;
					}

					if (threadId < 0)
					{
						threadId = 0;
					}
					return threadId;
				}

				static vint ResolveFrameId(const WorkflowDebugEnvelope& envelope, WorkflowDebugSessionState* state)
				{
					vint frameId = -1;
					auto body = ParseJsonObject(envelope.body);
					if (body)
					{
						TryReadNumberField(body, L"frameId", frameId);
					}

					if (frameId < 0 && state)
					{
						frameId = state->Snapshot().lastStoppedFrameId;
					}

					if (frameId < 0)
					{
						frameId = 0;
					}
					return frameId;
				}

				static vint ResolveStartFrame(const WorkflowDebugEnvelope& envelope)
				{
					vint startFrame = 0;
					auto body = ParseJsonObject(envelope.body);
					if (body)
					{
						TryReadNumberField(body, L"startFrame", startFrame);
					}
					if (startFrame < 0)
					{
						startFrame = 0;
					}
					return startFrame;
				}

				static vint ResolveLevels(const WorkflowDebugEnvelope& envelope)
				{
					vint levels = -1;
					auto body = ParseJsonObject(envelope.body);
					if (body)
					{
						TryReadNumberField(body, L"levels", levels);
					}
					return levels;
				}

				static WorkflowDebugScopeKind ResolveScopeKind(const WorkflowDebugEnvelope& envelope, WorkflowDebugSessionState* state)
				{
					WorkflowDebugScopeKind kind = WorkflowDebugScopeKind::Local;
					auto body = ParseJsonObject(envelope.body);
					if (body)
					{
						TryReadScopeKindField(body, L"scopeKind", kind);
					}
					(void)state;
					return kind;
				}

				static Ptr<JsonNode> BuildStackTraceBody(
					const WorkflowDebugEnvelope& envelope,
					WorkflowDebugSessionState* state,
					WorkflowDebugStackInspector* stackInspector
				)
				{
					auto body = CreateObject();
					auto threadId = ResolveThreadId(envelope, state);
					auto startFrame = ResolveStartFrame(envelope);
					auto levels = ResolveLevels(envelope);

					collections::List<WorkflowDebugStackFrame> frames;
					if (stackInspector)
					{
						stackInspector->TryGetFrames(threadId, frames);
					}

					auto array = CreateArray();
					vint endFrame = frames.Count();
					if (levels >= 0 && startFrame + levels < endFrame)
					{
						endFrame = startFrame + levels;
					}
					for (vint i = startFrame; i < endFrame; i++)
					{
						array->items.Add(BuildStackFrame(frames[i]));
					}

					AddField(body.Obj(), L"threadId", CreateNumber(threadId));
					AddField(body.Obj(), L"startFrame", CreateNumber(startFrame));
					AddField(body.Obj(), L"totalFrames", CreateNumber(frames.Count()));
					AddField(body.Obj(), L"frames", array);
					return body;
				}

				static Ptr<JsonNode> BuildScopesBody(
					const WorkflowDebugEnvelope& envelope,
					WorkflowDebugSessionState* state,
					WorkflowDebugValueInspector* valueInspector
				)
				{
					auto body = CreateObject();
					auto threadId = ResolveThreadId(envelope, state);
					auto frameId = ResolveFrameId(envelope, state);

					collections::List<WorkflowDebugScope> scopes;
					if (valueInspector)
					{
						valueInspector->TryGetScopes(threadId, frameId, scopes);
					}

					auto array = CreateArray();
					for (auto scope : scopes)
					{
						array->items.Add(BuildScope(scope));
					}

					AddField(body.Obj(), L"threadId", CreateNumber(threadId));
					AddField(body.Obj(), L"frameId", CreateNumber(frameId));
					AddField(body.Obj(), L"scopes", array);
					return body;
				}

				static Ptr<JsonNode> BuildVariablesBody(
					const WorkflowDebugEnvelope& envelope,
					WorkflowDebugSessionState* state,
					WorkflowDebugValueInspector* valueInspector
				)
				{
					auto body = CreateObject();
					auto threadId = ResolveThreadId(envelope, state);
					auto frameId = ResolveFrameId(envelope, state);
					auto kind = ResolveScopeKind(envelope, state);

					collections::List<WorkflowDebugVariable> variables;
					if (valueInspector)
					{
						valueInspector->TryGetVariables(threadId, frameId, kind, variables);
					}

					auto array = CreateArray();
					for (auto variable : variables)
					{
						array->items.Add(BuildVariable(variable));
					}

					AddField(body.Obj(), L"threadId", CreateNumber(threadId));
					AddField(body.Obj(), L"frameId", CreateNumber(frameId));
					AddField(body.Obj(), L"scopeKind", CreateString(ScopeKindToText(kind)));
					AddField(body.Obj(), L"variables", array);
					return body;
				}

				static bool SendResponse(
					const WorkflowDebugEnvelope& request,
					WorkflowDebugSessionState* state,
					WorkflowDebugTransport* transport,
					const WString& command,
					Ptr<JsonNode> body
				)
				{
					if (!transport)
					{
						return false;
					}

					WorkflowDebugEnvelope response;
					response.kind = WorkflowDebugEnvelopeKind::Response;
					response.command = command;
					response.sessionId = request.sessionId;
					response.replyTo = request.seq;
					response.body = SerializeJsonNode(body);
					if (state)
					{
						auto nextSeq = state->GetLastOutboundSeq() + 1;
						state->SetLastOutboundSeq(nextSeq);
						response.seq = nextSeq;
					}
					else
					{
						response.seq = 0;
					}

					return transport->Send(response);
				}
			}

			WorkflowDebugBridge::WorkflowDebugBridge()
			{
			}

			WorkflowDebugBridge::~WorkflowDebugBridge()
			{
			}

			void WorkflowDebugBridge::Bind(
				WorkflowDebugSessionState* valueState,
				WorkflowDebugTransport* valueTransport,
				WorkflowDebugSourceCatalog* valueSourceCatalog,
				WorkflowDebugBreakpointRegistry* valueBreakpointRegistry,
				WorkflowDebugStackInspector* valueStackInspector,
				WorkflowDebugValueInspector* valueValueInspector,
				WorkflowDebugRuntimeBinding* valueRuntimeBinding
			)
			{
				state = valueState;
				transport = valueTransport;
				sourceCatalog = valueSourceCatalog;
				breakpointRegistry = valueBreakpointRegistry;
				stackInspector = valueStackInspector;
				valueInspector = valueValueInspector;
				runtimeBinding = valueRuntimeBinding;
			}

			void WorkflowDebugBridge::Unbind()
			{
				state = nullptr;
				transport = nullptr;
				sourceCatalog = nullptr;
				breakpointRegistry = nullptr;
				stackInspector = nullptr;
				valueInspector = nullptr;
				runtimeBinding = nullptr;
			}

			bool WorkflowDebugBridge::Dispatch(const WorkflowDebugEnvelope& envelope)
			{
				if (envelope.command.Length() == 0)
				{
					return false;
				}

				if (state)
				{
					if (envelope.seq >= 0)
					{
						state->SetLastInboundSeq(envelope.seq);
					}
				}

				if (envelope.command == L"hello")
				{
					return HandleHello(envelope);
				}
				if (envelope.command == L"initialize")
				{
					return HandleInitialize(envelope);
				}
				if (envelope.command == L"setBreakpoints")
				{
					return HandleSetBreakpoints(envelope);
				}
				if (envelope.command == L"continue")
				{
					return HandleContinue(envelope);
				}
				if (envelope.command == L"next")
				{
					return HandleNext(envelope);
				}
				if (envelope.command == L"stepIn")
				{
					return HandleStepIn(envelope);
				}
				if (envelope.command == L"stackTrace")
				{
					return HandleStackTrace(envelope);
				}
				if (envelope.command == L"scopes")
				{
					return HandleScopes(envelope);
				}
				if (envelope.command == L"variables")
				{
					return HandleVariables(envelope);
				}
				if (envelope.command == L"exception")
				{
					return HandleException(envelope);
				}

				return false;
			}

			bool WorkflowDebugBridge::HandleHello(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (sourceCatalog)
				{
					sourceCatalog->Clear();
				}
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Negotiating);
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleInitialize(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (transport)
				{
					transport->Open();
				}
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Ready);
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleSetBreakpoints(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (breakpointRegistry)
				{
					breakpointRegistry->Clear();
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleContinue(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (runtimeBinding)
				{
					auto debugger = runtimeBinding->GetRemoteDebugger();
					if (!debugger || !debugger->RequestRun())
					{
						return false;
					}
				}
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Running);
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleNext(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (runtimeBinding)
				{
					auto debugger = runtimeBinding->GetRemoteDebugger();
					if (!debugger || !debugger->RequestStepOver())
					{
						return false;
					}
				}
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Running);
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleStepIn(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (runtimeBinding)
				{
					auto debugger = runtimeBinding->GetRemoteDebugger();
					if (!debugger || !debugger->RequestStepInto())
					{
						return false;
					}
				}
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Running);
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleStackTrace(const WorkflowDebugEnvelope& envelope)
			{
				auto body = BuildStackTraceBody(envelope, state, stackInspector);
				return SendResponse(envelope, state, transport, L"stackTrace", body);
			}

			bool WorkflowDebugBridge::HandleScopes(const WorkflowDebugEnvelope& envelope)
			{
				auto body = BuildScopesBody(envelope, state, valueInspector);
				return SendResponse(envelope, state, transport, L"scopes", body);
			}

			bool WorkflowDebugBridge::HandleVariables(const WorkflowDebugEnvelope& envelope)
			{
				auto body = BuildVariablesBody(envelope, state, valueInspector);
				return SendResponse(envelope, state, transport, L"variables", body);
			}

			bool WorkflowDebugBridge::HandleException(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Paused);
					state->SetLastStopped(L"exception", -1, -1, -1, -1);
				}
				return true;
			}
		}
	}
}

#endif
