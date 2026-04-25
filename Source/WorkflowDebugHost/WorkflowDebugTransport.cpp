/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <VlppGlrParser.h>
#include "WorkflowDebugTransport.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <limits>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

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
				static WString GetEnvelopeKindText(WorkflowDebugEnvelopeKind kind)
				{
					switch (kind)
					{
					case WorkflowDebugEnvelopeKind::Request:
						return L"request";
					case WorkflowDebugEnvelopeKind::Response:
						return L"response";
					case WorkflowDebugEnvelopeKind::Event:
						return L"event";
					default:
						return L"request";
					}
				}

				static bool TryParseEnvelopeKind(const WString& text, WorkflowDebugEnvelopeKind& kind)
				{
					if (text == L"request")
					{
						kind = WorkflowDebugEnvelopeKind::Request;
						return true;
					}
					if (text == L"response")
					{
						kind = WorkflowDebugEnvelopeKind::Response;
						return true;
					}
					if (text == L"event")
					{
						kind = WorkflowDebugEnvelopeKind::Event;
						return true;
					}
					return false;
				}

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

				static Ptr<JsonObject> CreateObject()
				{
					return Ptr(new JsonObject);
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

				static bool TryGetField(JsonObject* object, const WString& name, Ptr<JsonNode>& value)
				{
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

					// 调试协议里的序号和偏移值都应该落在 vint 可表示范围内；
					// 如果远端传来异常大数值，直接拒绝比静默截断更安全。
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

				static WString SerializeEnvelopeToJson(const WorkflowDebugEnvelope& envelope)
				{
					auto root = CreateObject();
					root->fields.Add(CreateField(L"type", CreateString(GetEnvelopeKindText(envelope.kind))));
					root->fields.Add(CreateField(L"seq", CreateNumber(envelope.seq)));
					if (envelope.replyTo >= 0)
					{
						root->fields.Add(CreateField(L"replyTo", CreateNumber(envelope.replyTo)));
					}
					root->fields.Add(CreateField(L"sessionId", CreateString(envelope.sessionId)));
					root->fields.Add(CreateField(L"cmd", CreateString(envelope.command)));

					Ptr<JsonNode> bodyNode = ParseJsonText(envelope.body);
					if (!bodyNode)
					{
						if (envelope.body.Length() == 0)
						{
							bodyNode = CreateObject();
						}
						else
						{
							return WString();
						}
					}

					root->fields.Add(CreateField(L"body", bodyNode));
					return JsonToString(root);
				}

				static bool DeserializeEnvelopeFromJson(const WString& text, WorkflowDebugEnvelope& envelope)
				{
					glr::json::Parser parser;
					auto node = JsonParse(text, parser);
					auto object = dynamic_cast<JsonObject*>(node.Obj());
					if (!object)
					{
						return false;
					}

					WString kindText;
					if (!TryReadStringField(object, L"type", kindText) && !TryReadStringField(object, L"kind", kindText))
					{
						return false;
					}

					if (!TryParseEnvelopeKind(kindText, envelope.kind))
					{
						return false;
					}

					vint seq = -1;
					if (!TryReadNumberField(object, L"seq", seq))
					{
						return false;
					}

					vint replyTo = -1;
					TryReadNumberField(object, L"replyTo", replyTo);

					if (!TryReadStringField(object, L"sessionId", envelope.sessionId))
					{
						return false;
					}

					if (!TryReadStringField(object, L"cmd", envelope.command) && !TryReadStringField(object, L"command", envelope.command))
					{
						return false;
					}

					envelope.seq = seq;
					envelope.replyTo = replyTo;

					Ptr<JsonNode> bodyNode;
					if (TryGetField(object, L"body", bodyNode) && bodyNode)
					{
						envelope.body = JsonToString(bodyNode);
					}
					else
					{
						envelope.body = WString();
					}
					return true;
				}

				static bool WriteAll(SOCKET socket, const char* data, vint size)
				{
					vint total = 0;
					while (total < size)
					{
						auto written = send(socket, data + total, (int)(size - total), 0);
						if (written <= 0)
						{
							return false;
						}
						total += written;
					}
					return true;
				}

			}

			struct WorkflowDebugTransport::WorkflowDebugTransportImpl
			{
				WString									host = L"127.0.0.1";
				vint									port = 0;
				bool									open = false;
				bool									wsaStarted = false;
				std::atomic<bool>						closing = false;
				SOCKET									socketHandle = INVALID_SOCKET;
				std::thread								receiveThread;
				mutable std::mutex						stateMutex;
				mutable std::mutex						queueMutex;
				collections::List<WorkflowDebugEnvelope>	incoming;
				collections::List<WorkflowDebugEnvelope>	outgoing;

				void Clear()
				{
					Close();
					std::lock_guard<std::mutex> guard(queueMutex);
					incoming.Clear();
					outgoing.Clear();
				}

				bool IsOpen() const
				{
					std::lock_guard<std::mutex> guard(stateMutex);
					return open;
				}

				void SetEndpoint(const WString& valueHost, vint valuePort)
				{
					CHECK_ERROR(valueHost.Length() > 0, L"主机名不能为空。");
					CHECK_ERROR(valuePort > 0 && valuePort <= 65535, L"端口号超出范围。");
					std::lock_guard<std::mutex> guard(stateMutex);
					host = valueHost;
					port = valuePort;
				}

				bool Open()
				{
					// 保留骨架阶段的内存队列行为，避免现有会话测试被打断。
					Close();
					std::lock_guard<std::mutex> guard(stateMutex);
					open = true;
					return true;
				}

				bool Connect()
				{
					WString localHost;
					vint localPort = 0;
					{
						std::lock_guard<std::mutex> guard(stateMutex);
						localHost = host;
						localPort = port;
					}

					CHECK_ERROR(localHost.Length() > 0, L"主机名不能为空。");
					CHECK_ERROR(localPort > 0 && localPort <= 65535, L"端口号超出范围。");

					Close();

					WSADATA wsaData;
					if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
					{
						return false;
					}
					wsaStarted = true;
					closing = false;

					addrinfoW hints = {};
					hints.ai_family = AF_UNSPEC;
					hints.ai_socktype = SOCK_STREAM;
					hints.ai_protocol = IPPROTO_TCP;

					addrinfoW* addressInfo = nullptr;
					auto portText = itow(localPort);
					auto result = GetAddrInfoW(localHost.Buffer(), portText.Buffer(), &hints, &addressInfo);
					if (result != 0)
					{
						WSACleanup();
						wsaStarted = false;
						return false;
					}

					SOCKET connectedSocket = INVALID_SOCKET;
					// 调试会话是先起适配器再起宿主的，VSCode 任务和宿主启动之间会有短暂竞态。
					// 这里做一个有限重试，避免把“端口稍晚才开始监听”误判成连接失败。
					for (vint retry = 0; retry < 40 && connectedSocket == INVALID_SOCKET && !closing; retry++)
					{
						for (auto current = addressInfo; current != nullptr; current = current->ai_next)
						{
							connectedSocket = socket((int)current->ai_family, (int)current->ai_socktype, (int)current->ai_protocol);
							if (connectedSocket == INVALID_SOCKET)
							{
								continue;
							}

							if (connect(connectedSocket, current->ai_addr, (int)current->ai_addrlen) == 0)
							{
								break;
							}

							closesocket(connectedSocket);
							connectedSocket = INVALID_SOCKET;
						}

						if (connectedSocket != INVALID_SOCKET || closing)
						{
							break;
						}

						std::this_thread::sleep_for(std::chrono::milliseconds(50));
					}
					FreeAddrInfoW(addressInfo);

					if (connectedSocket == INVALID_SOCKET)
					{
						WSACleanup();
						wsaStarted = false;
						return false;
					}

					{
						std::lock_guard<std::mutex> guard(stateMutex);
						socketHandle = connectedSocket;
						open = true;
					}

					receiveThread = std::thread([this, connectedSocket]()
					{
						this->ReceiveLoop(connectedSocket);
					});
					return true;
				}

				bool Send(const WorkflowDebugEnvelope& envelope)
				{
					{
						std::lock_guard<std::mutex> guard(queueMutex);
						outgoing.Add(envelope);
					}

					SOCKET socketToWrite = INVALID_SOCKET;
					{
						std::lock_guard<std::mutex> guard(stateMutex);
						if (!open)
						{
							return false;
						}
						if (socketHandle == INVALID_SOCKET)
						{
							return !wsaStarted;
						}
						socketToWrite = socketHandle;
					}

					auto json = SerializeEnvelopeToJson(envelope);
					if (json.Length() == 0 && envelope.body.Length() > 0)
					{
						return false;
					}

					auto utf8 = wtou8(json + L"\n");
					return WriteAll(socketToWrite, reinterpret_cast<const char*>(utf8.Buffer()), utf8.Length());
				}

				bool TryReceive(WorkflowDebugEnvelope& envelope)
				{
					std::lock_guard<std::mutex> guard(queueMutex);
					if (incoming.Count() == 0)
					{
						return false;
					}

					envelope = incoming[0];
					incoming.RemoveAt(0);
					return true;
				}

				bool TryPopOutgoing(WorkflowDebugEnvelope& envelope)
				{
					std::lock_guard<std::mutex> guard(queueMutex);
					if (outgoing.Count() == 0)
					{
						return false;
					}

					envelope = outgoing[0];
					outgoing.RemoveAt(0);
					return true;
				}

				void QueueIncoming(const WorkflowDebugEnvelope& envelope)
				{
					std::lock_guard<std::mutex> guard(queueMutex);
					incoming.Add(envelope);
				}

				void Close()
				{
					closing = true;

					SOCKET socketToClose = INVALID_SOCKET;
					{
						std::lock_guard<std::mutex> guard(stateMutex);
						socketToClose = socketHandle;
						socketHandle = INVALID_SOCKET;
						open = false;
					}

					if (socketToClose != INVALID_SOCKET)
					{
						shutdown(socketToClose, SD_BOTH);
						closesocket(socketToClose);
					}

					if (receiveThread.joinable())
					{
						receiveThread.join();
					}

					if (wsaStarted)
					{
						WSACleanup();
						wsaStarted = false;
					}

					closing = false;
				}

				void ReceiveLoop(SOCKET socketToRead)
				{
					std::u8string buffer;
					char readBuffer[4096];

					while (!closing)
					{
						auto readBytes = recv(socketToRead, readBuffer, sizeof(readBuffer), 0);
						if (readBytes > 0)
						{
							buffer.append(reinterpret_cast<const char8_t*>(readBuffer), readBytes);
							while (true)
							{
								auto newline = buffer.find(u8'\n');
								if (newline == std::u8string::npos)
								{
									break;
								}

								auto line = buffer.substr(0, newline);
								buffer.erase(0, newline + 1);
								if (!line.empty() && line.back() == u8'\r')
								{
									line.pop_back();
								}
								if (line.empty())
								{
									continue;
								}

								WorkflowDebugEnvelope envelope;
								auto text = u8tow(U8String::Unmanaged(line.c_str()));
								if (DeserializeEnvelopeFromJson(text, envelope))
								{
									std::lock_guard<std::mutex> guard(queueMutex);
									incoming.Add(envelope);
								}
							}
						}
						else if (readBytes == 0)
						{
							break;
						}
						else
						{
							break;
						}
					}

					// 远端断开时，如果还持有这个 socket，就在这里收尾，避免泄漏。
					bool shouldClose = false;
					{
						std::lock_guard<std::mutex> guard(stateMutex);
						if (socketHandle == socketToRead)
						{
							socketHandle = INVALID_SOCKET;
							open = false;
							shouldClose = true;
						}
					}

					if (shouldClose)
					{
						closesocket(socketToRead);
					}
				}
			};

			WorkflowDebugTransport::WorkflowDebugTransport()
				:impl(new WorkflowDebugTransportImpl)
			{
			}

			WorkflowDebugTransport::~WorkflowDebugTransport()
			{
				Close();
			}

			void WorkflowDebugTransport::Clear()
			{
				impl->Clear();
			}

			bool WorkflowDebugTransport::IsOpen() const
			{
				return impl->IsOpen();
			}

			void WorkflowDebugTransport::SetEndpoint(const WString& host, vint port)
			{
				impl->SetEndpoint(host, port);
			}

			WString WorkflowDebugTransport::GetEndpointHost() const
			{
				std::lock_guard<std::mutex> guard(impl->stateMutex);
				return impl->host;
			}

			vint WorkflowDebugTransport::GetEndpointPort() const
			{
				std::lock_guard<std::mutex> guard(impl->stateMutex);
				return impl->port;
			}

			bool WorkflowDebugTransport::Open()
			{
				return impl->Open();
			}

			bool WorkflowDebugTransport::Connect()
			{
				return impl->Connect();
			}

			bool WorkflowDebugTransport::Connect(const WString& host, vint port)
			{
				SetEndpoint(host, port);
				return Connect();
			}

			void WorkflowDebugTransport::Close()
			{
				impl->Close();
			}

			bool WorkflowDebugTransport::Send(const WorkflowDebugEnvelope& envelope)
			{
				return impl->Send(envelope);
			}

			bool WorkflowDebugTransport::TryReceive(WorkflowDebugEnvelope& envelope)
			{
				return impl->TryReceive(envelope);
			}

			bool WorkflowDebugTransport::TryPopOutgoing(WorkflowDebugEnvelope& envelope)
			{
				return impl->TryPopOutgoing(envelope);
			}

			void WorkflowDebugTransport::QueueIncoming(const WorkflowDebugEnvelope& envelope)
			{
				impl->QueueIncoming(envelope);
			}
		}
	}
}

#endif
