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

				static Ptr<JsonLiteral> CreateLiteral(bool value)
				{
					auto node = Ptr(new JsonLiteral);
					node->value = value ? JsonLiteralValue::True : JsonLiteralValue::False;
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

				static Ptr<JsonObject> ParseJsonObject(const WString& text)
				{
					auto node = ParseJsonText(text);
					return node.Cast<JsonObject>();
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

				static bool TryGetArrayField(JsonObject* object, const WString& name, JsonArray*& value)
				{
					Ptr<JsonNode> node;
					if (!TryGetField(object, name, node))
					{
						return false;
					}

					value = dynamic_cast<JsonArray*>(node.Obj());
					return value != nullptr;
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

				static bool TryReadOptionalBooleanField(JsonObject* object, const WString& name, bool& value)
				{
					Ptr<JsonNode> node;
					if (!TryGetField(object, name, node))
					{
						return true;
					}

					if (auto literalNode = dynamic_cast<JsonLiteral*>(node.Obj()))
					{
						if (literalNode->value == JsonLiteralValue::True)
						{
							value = true;
							return true;
						}
						if (literalNode->value == JsonLiteralValue::False)
						{
							value = false;
							return true;
						}
					}
					else if (auto stringNode = dynamic_cast<JsonString*>(node.Obj()))
					{
						auto text = stringNode->content.value;
						if (text == L"true" || text == L"True" || text == L"TRUE")
						{
							value = true;
							return true;
						}
						if (text == L"false" || text == L"False" || text == L"FALSE")
						{
							value = false;
							return true;
						}
					}

					return false;
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
					auto row = frame.row >= 0 ? frame.row : 0;
					AddField(object.Obj(), L"frameId", CreateNumber(frame.frameId));
					AddField(object.Obj(), L"callStackIndex", CreateNumber(frame.frameId));
					AddField(object.Obj(), L"threadId", CreateNumber(frame.threadId));
					AddField(object.Obj(), L"sourceId", CreateNumber(frame.sourceId));
					auto sourcePath = frame.sourcePath.Length() > 0
						? frame.sourcePath
						: L"unknown://source/" + itow(frame.sourceId >= 0 ? frame.sourceId : 0);
					auto functionName = frame.functionName.Length() > 0
						? frame.functionName
						: L"frame-" + itow(frame.frameId);
					AddField(object.Obj(), L"sourcePath", CreateString(sourcePath));
					AddField(object.Obj(), L"functionName", CreateString(functionName));
					// 调试协议里 row 以 0 基保存，line 以 1 基显示；未知帧统一归一到第 0 行，避免两者脱节。
					AddField(object.Obj(), L"line", CreateNumber(row + 1));
					AddField(object.Obj(), L"row", CreateNumber(row));
					AddField(object.Obj(), L"column", CreateNumber(frame.column >= 0 ? frame.column + 1 : 1));
					AddField(object.Obj(), L"canRequestVariables", CreateLiteral(true));
					return object;
				}

				static Ptr<JsonObject> BuildScope(const WorkflowDebugScope& scope)
				{
					auto object = CreateObject();
					AddField(object.Obj(), L"kind", CreateString(ScopeKindToText(scope.kind)));
					AddField(object.Obj(), L"name", CreateString(scope.name));
					AddField(object.Obj(), L"variablesReference", CreateNumber(scope.variablesReference));
					AddField(object.Obj(), L"canExpand", CreateLiteral(scope.canExpand));
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
					AddField(object.Obj(), L"canExpand", CreateLiteral(variable.canExpand));
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
						TryReadNumberField(body.Obj(), L"threadId", threadId);
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
						TryReadNumberField(body.Obj(), L"frameId", frameId);
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
						TryReadNumberField(body.Obj(), L"startFrame", startFrame);
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
						if (!TryReadNumberField(body.Obj(), L"levels", levels))
						{
							levels = -1;
						}
					}
					return levels;
				}

				static WorkflowDebugScopeKind ResolveScopeKind(const WorkflowDebugEnvelope& envelope, WorkflowDebugSessionState* state)
				{
					WorkflowDebugScopeKind kind = WorkflowDebugScopeKind::Local;
					auto body = ParseJsonObject(envelope.body);
					if (body)
					{
						TryReadScopeKindField(body.Obj(), L"scopeKind", kind);
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

					collections::List<WorkflowDebugStackFrame> orderedFrames;
					for (vint i = frames.Count() - 1; i >= 0; --i)
					{
						orderedFrames.Add(frames[i]);
					}

					auto array = CreateArray();
					vint endFrame = orderedFrames.Count();
					if (levels >= 0 && startFrame + levels < endFrame)
					{
						endFrame = startFrame + levels;
					}
					for (vint i = startFrame; i < endFrame; i++)
					{
						array->items.Add(BuildStackFrame(orderedFrames[i]));
					}

					vint returnedLevels = levels >= 0
						? levels
						: (orderedFrames.Count() > startFrame ? orderedFrames.Count() - startFrame : 0);

					AddField(body.Obj(), L"threadId", CreateNumber(threadId));
					AddField(body.Obj(), L"startFrame", CreateNumber(startFrame));
					AddField(body.Obj(), L"levels", CreateNumber(returnedLevels));
					AddField(body.Obj(), L"totalFrames", CreateNumber(orderedFrames.Count()));
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
					vint variablesReference = 0;
					auto kind = ResolveScopeKind(envelope, state);

					auto bodyObject = ParseJsonObject(envelope.body);
					if (bodyObject)
					{
						TryReadNumberField(bodyObject.Obj(), L"variablesReference", variablesReference);
					}

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
					// 适配器需要把响应里的远程句柄映射回本地句柄，否则后续变量展开会丢失父节点关系。
					AddField(body.Obj(), L"variablesReference", CreateNumber(variablesReference));
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

				static bool SendEvent(
					WorkflowDebugSessionState* state,
					WorkflowDebugTransport* transport,
					const WString& command,
					Ptr<JsonNode> body,
					vint replyTo = -1
				)
				{
					if (!transport || !state)
					{
						return false;
					}

					auto snapshot = state->Snapshot();
					WorkflowDebugEnvelope event;
					event.kind = WorkflowDebugEnvelopeKind::Event;
					event.command = command;
					event.sessionId = snapshot.sessionId;
					event.replyTo = replyTo > 0 ? replyTo : (snapshot.lastInboundSeq > 0 ? snapshot.lastInboundSeq : -1);
					event.body = SerializeJsonNode(body);

					auto nextSeq = state->GetLastOutboundSeq() + 1;
					state->SetLastOutboundSeq(nextSeq);
					event.seq = nextSeq;
					return transport->Send(event);
				}

				static Ptr<JsonNode> BuildBreakpointValidatedBody(const WString& breakpointId, bool verified, const WString& reason)
				{
					auto body = CreateObject();
					AddField(body.Obj(), L"breakpointId", CreateString(breakpointId));
					AddField(body.Obj(), L"verified", CreateLiteral(verified));
					if (reason.Length() > 0)
					{
						AddField(body.Obj(), L"reason", CreateString(reason));
					}
					return body;
				}

				static Ptr<JsonNode> BuildStoppedBody(WorkflowDebugSessionState* state)
				{
					auto body = CreateObject();
					if (!state)
					{
						return body;
					}

					auto snapshot = state->Snapshot();
					AddField(body.Obj(), L"reason", CreateString(snapshot.lastStoppedReason.Length() > 0 ? snapshot.lastStoppedReason : L"pause"));
					AddField(body.Obj(), L"threadId", CreateNumber(snapshot.lastStoppedThreadId));
					AddField(body.Obj(), L"frameId", CreateNumber(snapshot.lastStoppedFrameId));
					AddField(body.Obj(), L"sourceId", CreateNumber(snapshot.lastStoppedSourceId));
					AddField(body.Obj(), L"row", CreateNumber(snapshot.lastStoppedRow));
					return body;
				}

				static Ptr<JsonNode> BuildDisconnectBody(const WString& reason)
				{
					auto body = CreateObject();
					AddField(body.Obj(), L"reason", CreateString(reason.Length() > 0 ? reason : L"会话关闭"));
					return body;
				}

				static Ptr<JsonNode> BuildExceptionBody(
					const WString& message,
					bool fatal,
					WorkflowDebugSessionState* state,
					WorkflowDebugStackInspector* stackInspector
				)
				{
					auto body = CreateObject();
					AddField(body.Obj(), L"message", CreateString(message));
					AddField(body.Obj(), L"fatal", CreateLiteral(fatal));

					vint threadId = -1;
					if (state)
					{
						threadId = state->Snapshot().lastStoppedThreadId;
					}

					collections::List<WorkflowDebugStackFrame> frames;
					if (stackInspector && threadId >= 0)
					{
						stackInspector->TryGetFrames(threadId, frames);
					}

					auto array = CreateArray();
					for (vint i = frames.Count() - 1; i >= 0; --i)
					{
						array->items.Add(BuildStackFrame(frames[i]));
					}
					AddField(body.Obj(), L"callStack", array);
					return body;
				}

				static Ptr<JsonNode> BuildOutputBody(const WString& level, const WString& message)
				{
					auto body = CreateObject();
					AddField(body.Obj(), L"level", CreateString(level.Length() > 0 ? level : L"info"));
					AddField(body.Obj(), L"message", CreateString(message));
					return body;
				}

				static Ptr<JsonObject> BuildCapabilitiesBody()
				{
					auto body = CreateObject();
					AddField(body.Obj(), L"supportsRemoteAttach", CreateLiteral(true));
					AddField(body.Obj(), L"supportsBreakpoints", CreateLiteral(true));
					AddField(body.Obj(), L"supportsContinue", CreateLiteral(true));
					AddField(body.Obj(), L"supportsStepOver", CreateLiteral(true));
					AddField(body.Obj(), L"supportsStepIn", CreateLiteral(true));
					AddField(body.Obj(), L"supportsStepOut", CreateLiteral(true));
					AddField(body.Obj(), L"supportsStackTrace", CreateLiteral(true));
					AddField(body.Obj(), L"supportsVariables", CreateLiteral(true));
					return body;
				}

				static Ptr<JsonArray> BuildSourceMapArray(const collections::List<WorkflowDebugSourceRecord>& sourceMap)
				{
					auto array = CreateArray();
					for (auto source : sourceMap)
					{
						auto object = CreateObject();
						AddField(object.Obj(), L"codeIndex", CreateNumber(source.codeIndex));
						AddField(object.Obj(), L"sourcePath", CreateString(source.sourcePath));
						AddField(object.Obj(), L"row", CreateNumber(source.row));
						array->items.Add(object);
					}
					return array;
				}

				static Ptr<JsonNode> BuildHelloBody(const WString& runtimeVersion, const collections::List<WorkflowDebugSourceRecord>& sourceMap)
				{
					auto body = CreateObject();
					AddField(body.Obj(), L"runtimeVersion", CreateString(runtimeVersion));
					AddField(body.Obj(), L"protocolVersion", CreateNumber(1));
					AddField(body.Obj(), L"capabilities", BuildCapabilitiesBody());
					AddField(body.Obj(), L"sourceMap", BuildSourceMapArray(sourceMap));
					return body;
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
				if (envelope.command == L"stepOut")
				{
					return HandleStepOut(envelope);
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
if (envelope.command == L"disconnect")
				{
					return HandleDisconnect(envelope);
				}

				return false;
			}

				bool WorkflowDebugBridge::HandleInitialize(const WorkflowDebugEnvelope& envelope)
			{
				auto body = ParseJsonObject(envelope.body);
				if (body)
				{
					bool stopOnEntry = true;
					if (!TryReadOptionalBooleanField(body.Obj(), L"stopOnEntry", stopOnEntry))
					{
						return false;
					}

					if (stopOnEntry && runtimeBinding)
					{
						// stopOnEntry 不是停在宿主初始化帧，而是等到第一条能映射到源码的可执行语句再停住。
						if (!runtimeBinding->RequestStopOnEntry())
						{
							return false;
						}
					}
				}

				if (!NotifyReady(L"workflow-runtime"))
				{
					return false;
				}

				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Ready);
				}
				return true;
			}

			bool WorkflowDebugBridge::HandleSetBreakpoints(const WorkflowDebugEnvelope& envelope)
			{
				auto body = ParseJsonObject(envelope.body);
				if (!body)
				{
					return false;
				}

				WString sourcePath;
				vint codeIndex = -1;
				JsonArray* breakpointsArray = nullptr;
				if (!TryReadStringField(body.Obj(), L"sourcePath", sourcePath)
					|| !TryReadNumberField(body.Obj(), L"codeIndex", codeIndex)
					|| !TryGetArrayField(body.Obj(), L"breakpoints", breakpointsArray))
				{
					return false;
				}

				if (breakpointRegistry)
				{
					breakpointRegistry->ClearSource(sourcePath);
				}

				for (auto breakpointNode : breakpointsArray->items)
				{
					auto breakpointObject = dynamic_cast<JsonObject*>(breakpointNode.Obj());
					if (!breakpointObject)
					{
						return false;
					}

					WString breakpointId;
					vint row = -1;
					vint column = -1;
					WString condition;
					WString logMessage;
					if (!TryReadStringField(breakpointObject, L"breakpointId", breakpointId)
						|| !TryReadNumberField(breakpointObject, L"row", row))
					{
						return false;
					}

					TryReadNumberField(breakpointObject, L"column", column);
					TryReadStringField(breakpointObject, L"condition", condition);
					TryReadStringField(breakpointObject, L"logMessage", logMessage);

					WorkflowDebugBreakpointRecord record;
					record.breakpointId = breakpointId;
					record.sourcePath = sourcePath;
					record.codeIndex = codeIndex;
					record.row = row;
					record.column = column;
					record.condition = condition;
					record.logMessage = logMessage;

					if (!breakpointRegistry)
					{
						return false;
					}

					auto breakpointIndex = breakpointRegistry->RegisterBreakpoint(record);
					const auto& storedBreakpoint = breakpointRegistry->GetBreakpoints()[breakpointIndex];
					if (!NotifyBreakpointValidated(storedBreakpoint.breakpointId, storedBreakpoint.verified, storedBreakpoint.reason, envelope.seq))
					{
						return false;
					}
				}

				if (runtimeBinding)
				{
					runtimeBinding->RefreshBreakpoints();
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

			bool WorkflowDebugBridge::HandleStepOut(const WorkflowDebugEnvelope& envelope)
			{
				(void)envelope;
				if (runtimeBinding)
				{
					auto debugger = runtimeBinding->GetRemoteDebugger();
					if (!debugger || !debugger->RequestStepOut())
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

				bool WorkflowDebugBridge::HandleDisconnect(const WorkflowDebugEnvelope& envelope)
			{
				auto body = ParseJsonObject(envelope.body);
				WString reason = L"disconnect";
				if (body)
				{
					TryReadStringField(body.Obj(), L"reason", reason);
				}

				if (runtimeBinding)
				{
					auto debugger = runtimeBinding->GetRemoteDebugger();
					if (debugger)
					{
						debugger->RequestStop();
					}
				}

				return NotifyDisconnect(reason);
			}

			bool WorkflowDebugBridge::NotifyStopped()
			{
				return SendEvent(state, transport, L"stopped", BuildStoppedBody(state));
			}

			bool WorkflowDebugBridge::NotifyOutput(const WString& level, const WString& message)
			{
				return SendEvent(state, transport, L"output", BuildOutputBody(level, message));
			}

			bool WorkflowDebugBridge::NotifyDisconnect(const WString& reason)
			{
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Closed);
				}
				// 正常结束时先通知适配器，再关闭底层连接，避免 VSCode 把收尾当成异常断开。
				return SendEvent(state, transport, L"disconnect", BuildDisconnectBody(reason));
			}

			bool WorkflowDebugBridge::NotifyHello(const WString& runtimeVersion, const collections::List<WorkflowDebugSourceRecord>& sourceMap)
			{
				if (state)
				{
					state->SetPhase(WorkflowDebugSessionPhase::Negotiating);
				}
				return SendEvent(state, transport, L"hello", BuildHelloBody(runtimeVersion, sourceMap));
			}

			bool WorkflowDebugBridge::NotifyReady(const WString& runtimeVersion)
			{
				auto body = CreateObject();
				auto snapshot = state ? state->Snapshot() : WorkflowDebugSessionSnapshot();
				AddField(body.Obj(), L"sessionId", CreateString(snapshot.sessionId));
				AddField(body.Obj(), L"runtimeVersion", CreateString(runtimeVersion));
				AddField(body.Obj(), L"accepted", CreateLiteral(true));
				AddField(body.Obj(), L"capabilities", BuildCapabilitiesBody());
				return SendEvent(state, transport, L"ready", body);
			}

			bool WorkflowDebugBridge::NotifyException(const WString& message, bool fatal)
			{
				return SendEvent(state, transport, L"exception", BuildExceptionBody(message, fatal, state, stackInspector));
			}

			bool WorkflowDebugBridge::NotifyBreakpointValidated(const WString& breakpointId, bool verified, const WString& reason, vint replyTo)
			{
				return SendEvent(state, transport, L"breakpointValidated", BuildBreakpointValidatedBody(breakpointId, verified, reason), replyTo);
			}
		}
	}
}

#endif
