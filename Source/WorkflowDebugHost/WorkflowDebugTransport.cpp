/***********************************************************************
Vczh Library++ 3.0
开发者: Zihan Chen(vczh)
Workflow::DebugHost

实现:
***********************************************************************/

#include "WorkflowDebugTransport.h"

#ifdef VCZH_DESCRIPTABLEOBJECT_WITH_METADATA

namespace vl
{
	namespace workflow
	{
		namespace debughost
		{
			WorkflowDebugTransport::WorkflowDebugTransport()
			{
			}

			WorkflowDebugTransport::~WorkflowDebugTransport()
			{
			}

			void WorkflowDebugTransport::Clear()
			{
				open = false;
				incoming.Clear();
				outgoing.Clear();
			}

			bool WorkflowDebugTransport::IsOpen() const
			{
				return open;
			}

			bool WorkflowDebugTransport::Open()
			{
				open = true;
				return true;
			}

			void WorkflowDebugTransport::Close()
			{
				open = false;
			}

			bool WorkflowDebugTransport::Send(const WorkflowDebugEnvelope& envelope)
			{
				if (!open)
				{
					return false;
				}

				outgoing.Add(envelope);
				return true;
			}

			bool WorkflowDebugTransport::TryReceive(WorkflowDebugEnvelope& envelope)
			{
				if (incoming.Count() == 0)
				{
					return false;
				}

				envelope = incoming[0];
				incoming.RemoveAt(0);
				return true;
			}

			void WorkflowDebugTransport::QueueIncoming(const WorkflowDebugEnvelope& envelope)
			{
				incoming.Add(envelope);
			}
		}
	}
}

#endif
