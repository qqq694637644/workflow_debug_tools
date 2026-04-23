/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugHost.h"
#include "WorkflowDebugSession.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			WorkflowDebugHost::WorkflowDebugHost()
			{
			}

			WorkflowDebugHost::~WorkflowDebugHost()
			{
			}

			void WorkflowDebugHost::Initialize()
			{
			}

			void WorkflowDebugHost::Shutdown()
			{
				Clear();
			}

			Ptr<WorkflowDebugSession> WorkflowDebugHost::CreateSession(const WString& sessionId)
			{
				CHECK_ERROR(sessionId.Length() > 0, L"会话标识不能为空。");

				if (auto index = sessions.Keys().IndexOf(sessionId); index != -1)
				{
					return sessions.Values()[index];
				}

				auto session = Ptr(new WorkflowDebugSession(sessionId));
				sessions.Set(sessionId, session);
				return session;
			}

			bool WorkflowDebugHost::DestroySession(const WString& sessionId)
			{
				vint index = sessions.Keys().IndexOf(sessionId);
				if (index == -1)
				{
					return false;
				}

				auto session = sessions.Values()[index];
				if (session)
				{
					session->Detach();
				}

				return sessions.Remove(sessionId);
			}

			Ptr<WorkflowDebugSession> WorkflowDebugHost::FindSession(const WString& sessionId) const
			{
				if (auto index = sessions.Keys().IndexOf(sessionId); index != -1)
				{
					return sessions.Values()[index];
				}

				return nullptr;
			}

			vint WorkflowDebugHost::GetSessionCount() const
			{
				return sessions.Count();
			}

			void WorkflowDebugHost::Clear()
			{
				for (auto session : sessions.Values())
				{
					if (session)
					{
						session->Detach();
					}
				}

				sessions.Clear();
			}
		}
	}
}

#endif
