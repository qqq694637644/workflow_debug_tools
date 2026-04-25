#include "../../Source/Helper.h"

TEST_FILE
{
	TEST_CASE(L"Test WfBreakPoint::Ins")
	{
		auto debugger = Ptr(new WfDebugger);
		auto callback = GetDebuggerCallback(debugger.Obj());

		auto assembly = Ptr(new WfAssembly);

		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Ins(assembly.Obj(), 0)) == 0);
		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Ins(assembly.Obj(), 0)) == -1);
		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Ins(assembly.Obj(), 1)) == 1);
		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Ins(assembly.Obj(), 1)) == -1);

		TEST_ASSERT(callback->BreakIns(assembly.Obj(), 0) == true);
		TEST_ASSERT(callback->BreakIns(assembly.Obj(), 1) == true);
		TEST_ASSERT(callback->BreakIns(assembly.Obj(), 2) == false);

		TEST_ASSERT(debugger->RemoveBreakPoint(0) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(0) == false);
		TEST_ASSERT(debugger->RemoveBreakPoint(1) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(1) == false);

		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Ins(assembly.Obj(), 3)) == 1);
		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Ins(assembly.Obj(), 3)) == -1);
		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Ins(assembly.Obj(), 4)) == 0);
		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Ins(assembly.Obj(), 4)) == -1);

		TEST_ASSERT(callback->BreakIns(assembly.Obj(), 1) == false);
		TEST_ASSERT(callback->BreakIns(assembly.Obj(), 3) == true);
		TEST_ASSERT(callback->BreakIns(assembly.Obj(), 5) == false);

		TEST_ASSERT(debugger->RemoveBreakPoint(0) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(0) == false);
		TEST_ASSERT(debugger->RemoveBreakPoint(1) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(1) == false);
	});

	TEST_CASE(L"Test WfBreakPoint::Read")
	{
		auto debugger = Ptr(new WfDebugger);
		auto callback = GetDebuggerCallback(debugger.Obj());

		auto assembly = Ptr(new WfAssembly);
		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Read(assembly.Obj(), 0)) == 0);
		TEST_ASSERT(callback->BreakRead(assembly.Obj(), 0) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(0) == true);
		TEST_ASSERT(callback->BreakRead(assembly.Obj(), 0) == false);
	});

	TEST_CASE(L"Test WfBreakPoint::Write")
	{
		auto debugger = Ptr(new WfDebugger);
		auto callback = GetDebuggerCallback(debugger.Obj());

		auto assembly = Ptr(new WfAssembly);
		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Write(assembly.Obj(), 0)) == 0);
		TEST_ASSERT(callback->BreakWrite(assembly.Obj(), 0) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(0) == true);
		TEST_ASSERT(callback->BreakWrite(assembly.Obj(), 0) == false);
	});

	TEST_CASE(L"Test WfBreakPoint::Get")
	{
		auto debugger = Ptr(new WfDebugger);
		auto callback = GetDebuggerCallback(debugger.Obj());

		auto td = GetTypeDescriptor(L"test::ObservableValue");
		auto prop1 = td->GetPropertyByName(L"Name", true);
		auto prop2 = td->GetPropertyByName(L"DisplayName", true);

		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Get(nullptr, prop1)) == 0);
		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Get(nullptr, prop2)) == 1);
		TEST_ASSERT(callback->BreakGet(nullptr, prop1) == true);
		TEST_ASSERT(callback->BreakGet(nullptr, prop2) == true);
		TEST_ASSERT(callback->BreakInvoke(nullptr, prop1->GetGetter()) == true);
		TEST_ASSERT(callback->BreakInvoke(nullptr, prop2->GetGetter()) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(0) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(1) == true);
		TEST_ASSERT(callback->BreakGet(nullptr, prop1) == false);
		TEST_ASSERT(callback->BreakGet(nullptr, prop2) == false);
		TEST_ASSERT(callback->BreakInvoke(nullptr, prop1->GetGetter()) == false);
		TEST_ASSERT(callback->BreakInvoke(nullptr, prop2->GetGetter()) == false);
	});

	TEST_CASE(L"Test WfBreakPoint::Set")
	{
		auto debugger = Ptr(new WfDebugger);
		auto callback = GetDebuggerCallback(debugger.Obj());

		auto td = GetTypeDescriptor(L"test::ObservableValue");
		auto prop1 = td->GetPropertyByName(L"Name", true);
		auto prop2 = td->GetPropertyByName(L"DisplayName", true);

		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Set(nullptr, prop1)) == 0);
		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Set(nullptr, prop2)) == 1);
		TEST_ASSERT(callback->BreakSet(nullptr, prop1) == true);
		TEST_ASSERT(callback->BreakSet(nullptr, prop2) == true);
		TEST_ASSERT(callback->BreakInvoke(nullptr, prop1->GetSetter()) == true);
		TEST_ASSERT(callback->BreakInvoke(nullptr, prop2->GetSetter()) == false);
		TEST_ASSERT(debugger->RemoveBreakPoint(0) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(1) == true);
		TEST_ASSERT(callback->BreakSet(nullptr, prop1) == false);
		TEST_ASSERT(callback->BreakSet(nullptr, prop2) == false);
		TEST_ASSERT(callback->BreakInvoke(nullptr, prop1->GetSetter()) == false);
		TEST_ASSERT(callback->BreakInvoke(nullptr, prop2->GetSetter()) == false);
	});

	TEST_CASE(L"Test WfBreakPoint::Attach")
	{
		auto debugger = Ptr(new WfDebugger);
		auto callback = GetDebuggerCallback(debugger.Obj());

		auto td = GetTypeDescriptor(L"test::ObservableValue");
		auto ev = td->GetEventByName(L"ValueChanged", true);

		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Attach(nullptr, ev)) == 0);
		TEST_ASSERT(callback->BreakAttach(nullptr, ev) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(0) == true);
		TEST_ASSERT(callback->BreakAttach(nullptr, ev) == false);
	});

	TEST_CASE(L"Test WfBreakPoint::Detach")
	{
		auto debugger = Ptr(new WfDebugger);
		auto callback = GetDebuggerCallback(debugger.Obj());

		auto td = GetTypeDescriptor(L"test::ObservableValue");
		auto ev = td->GetEventByName(L"ValueChanged", true);

		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Detach(nullptr, ev)) == 0);
		TEST_ASSERT(callback->BreakDetach(nullptr, ev) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(0) == true);
		TEST_ASSERT(callback->BreakDetach(nullptr, ev) == false);
	});

	TEST_CASE(L"Test WfBreakPoint::Invoke")
	{
		auto debugger = Ptr(new WfDebugger);
		auto callback = GetDebuggerCallback(debugger.Obj());

		auto td = GetTypeDescriptor(L"test::ObservableValue");
		auto m = td->GetMethodGroupByName(L"Create", true)->GetMethod(0);

		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Invoke(nullptr, m)) == 0);
		TEST_ASSERT(callback->BreakInvoke(nullptr, m) == true);
		TEST_ASSERT(debugger->RemoveBreakPoint(0) == true);
		TEST_ASSERT(callback->BreakInvoke(nullptr, m) == false);
	});

	TEST_CASE(L"Test WfBreakPoint::Create")
	{
		auto debugger = Ptr(new WfDebugger);
		auto callback = GetDebuggerCallback(debugger.Obj());

		auto td = GetTypeDescriptor(L"test::ObservableValue");
		auto mg = td->GetConstructorGroup();
		auto count = mg->GetMethodCount();

		TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Create(td)) == 0);
		TEST_ASSERT(callback->BreakCreate(td) == true);
		for (vint i = 0; i < count; i++)
		{
			TEST_ASSERT(callback->BreakInvoke(nullptr, mg->GetMethod(i)) == true);
		}

		TEST_ASSERT(debugger->RemoveBreakPoint(0) == true);
		TEST_ASSERT(callback->BreakCreate(td) == false);
		for (vint i = 0; i < count; i++)
		{
			TEST_ASSERT(callback->BreakInvoke(nullptr, mg->GetMethod(i)) == false);
		}
	});

		auto CreateThreadContextFromSample = [](const WString& name)
		{
			List<WString> moduleCodes;
			moduleCodes.Add(LoadSample(L"Debugger", name));
			List<glr::ParsingError> errors;
			auto assembly = Compile(GetWorkflowParser(), WfCpuArchitecture::AsExecutable, moduleCodes, errors);
			TEST_ASSERT(assembly && errors.Count() == 0);
			return Ptr(new WfRuntimeGlobalContext(assembly));
		};

		auto CreateThreadContextFromCodegenSample = [](const WString& name)
		{
			List<WString> moduleCodes;
			moduleCodes.Add(LoadSample(L"Codegen", name));
			List<glr::ParsingError> errors;
			auto assembly = Compile(GetWorkflowParser(), WfCpuArchitecture::AsExecutable, moduleCodes, errors);
			TEST_ASSERT(assembly && errors.Count() == 0);
			return Ptr(new WfRuntimeGlobalContext(assembly));
		};

	TEST_CASE(L"Test debugger: no break point")
	{
		SetDebuggerForCurrentThread(Ptr(new WfDebugger));
		auto context = CreateThreadContextFromSample(L"HelloWorld");
		LoadFunction<void()>(context, L"<initialize>")();
		auto result = LoadFunction<WString()>(context, L"Main")();
		TEST_ASSERT(result == L"Hello, world!");
		ResetDebuggerForCurrentThread();
	});

	class MultithreadDebugger : public WfDebugger
	{
	protected:
		Ptr<Thread>				debuggerOperatorThread;
		EventObject				blockOperatorEvent;
		EventObject				blockDebuggerEvent;
		bool					requiredToPause = false;

		void OnBlockExecution()override
		{
			blockOperatorEvent.Signal();
			blockDebuggerEvent.Wait();
		}

		void OnStartExecution()override
		{
			blockDebuggerEvent.Wait();
			if (requiredToPause)
			{
				Pause();
			}
		}

		void OnStopExecution()override
		{
			blockOperatorEvent.Signal();
		}
	public:
		MultithreadDebugger(Func<void(MultithreadDebugger*)> debuggerOperator)
		{
			blockOperatorEvent.CreateAutoUnsignal(false);
			blockDebuggerEvent.CreateAutoUnsignal(false);
			debuggerOperatorThread = Ptr(Thread::CreateAndStart(
				[=, this]()
				{
					debuggerOperator(this);
				}, false));
		}

		~MultithreadDebugger()
		{
			debuggerOperatorThread->Wait();
		}

		void Continue()
		{
			TEST_ASSERT(state != Stopped);
			blockDebuggerEvent.Signal();
			blockOperatorEvent.Wait();
		}

		void BeginExecution(bool pause)
		{
			TEST_ASSERT(state == Stopped);
			requiredToPause = pause;
			blockDebuggerEvent.Signal();
			blockOperatorEvent.Wait();
		}
	};

	TEST_CASE(L"Test debugger: break by code line")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->BeginExecution(false);

				vint rows[] = { 5, 6, 7 };
				vint breakPoints[] = { 0, 1, 2 };
				WString values[] = { L"zero", L"one", L"two" };

				for (vint i = 0; i < sizeof(rows) / sizeof(*rows); i++)
				{
					TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByBreakPoint);
					TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == breakPoints[i]);
					TEST_ASSERT(debugger->GetCurrentPosition().start.row == rows[i]);
					TEST_ASSERT(UnboxValue<WString>(debugger->GetValueByName(L"s")) == values[i]);
					TEST_ASSERT(debugger->Run());
					TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
					debugger->Continue();
				}

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"Assignment");
		auto assembly = context->assembly.Obj();
		TEST_ASSERT(debugger->AddCodeLineBreakPoint(assembly, 0, 5) == 0);
		TEST_ASSERT(debugger->AddCodeLineBreakPoint(assembly, 0, 6) == 1);
		TEST_ASSERT(debugger->AddCodeLineBreakPoint(assembly, 0, 7) == 2);

		LoadFunction<void()>(context, L"<initialize>")();
		auto result = LoadFunction<WString()>(context, L"Main")();
		TEST_ASSERT(result == L"three");
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: stop")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->BeginExecution(false);

				TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByBreakPoint);
				TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == 0);
				TEST_ASSERT(debugger->Stop());
				TEST_ASSERT(debugger->GetState() == WfDebugger::RequiredToStop);
				debugger->Continue();
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"Assignment");
		auto assembly = context->assembly.Obj();
		TEST_ASSERT(debugger->AddCodeLineBreakPoint(assembly, 0, 5) == 0);

		LoadFunction<void()>(context, L"<initialize>")();
		try
		{
			LoadFunction<WString()>(context, L"Main")();
			TEST_ASSERT(false);
		}
		catch (const WfRuntimeException& ex)
		{
			TEST_ASSERT(ex.Message() == L"Internal error: Debugger stopped the program.");
		}
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: stop while running")
	{
		// TEST_ASSERT(false);
	});

	TEST_CASE(L"Test debugger: pause while running")
	{
		// TEST_ASSERT(false);
	});

	TEST_CASE(L"Test debugger: step over 1")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->BeginExecution(true);

				vint rows[] = { 4, 5, 6, 7, 8 };
				WString values[] = { L"", L"zero", L"one", L"two", L"three" };

				for (vint i = 0; i < sizeof(rows) / sizeof(*rows); i++)
				{
					TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
					TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == WfDebugger::PauseBreakPoint);
					TEST_ASSERT(debugger->GetCurrentPosition().start.row == rows[i]);
					auto s = debugger->GetValueByName(L"s");
					TEST_ASSERT((s.IsNull() && values[i] == L"") || UnboxValue<WString>(s) == values[i]);
					TEST_ASSERT(debugger->StepOver());
					TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
					debugger->Continue();
				}

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"Assignment");
		auto assembly = context->assembly.Obj();

		LoadFunction<void()>(context, L"<initialize>")();
		auto result = LoadFunction<WString()>(context, L"Main")();
		TEST_ASSERT(result == L"three");
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: step over 2")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->BeginExecution(true);

				vint rows[] = { 19, 20, 21, 22, 23 };
				WString values[] = { L"", L"zero", L"one", L"two", L"three" };

				for (vint i = 0; i < sizeof(rows) / sizeof(*rows); i++)
				{
					TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
					TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == WfDebugger::PauseBreakPoint);
					TEST_ASSERT(debugger->GetCurrentPosition().start.row == rows[i]);
					auto s = debugger->GetValueByName(L"s");
					TEST_ASSERT((s.IsNull() && values[i] == L"") || UnboxValue<WString>(s) == values[i]);
					TEST_ASSERT(debugger->StepOver());
					TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
					debugger->Continue();
				}

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"Function");
		auto assembly = context->assembly.Obj();

		LoadFunction<void()>(context, L"<initialize>")();
		auto result = LoadFunction<WString()>(context, L"Main")();
		TEST_ASSERT(result == L"three");
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: step over 3")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->BeginExecution(true);

				vint rows[] = { 13, 14, 15, 16, 17, 18 };
				vint values[] = { 0, 0, 0, 1, 2, 3 };

				for (vint i = 0; i < sizeof(rows) / sizeof(*rows); i++)
				{
					TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
					TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == WfDebugger::PauseBreakPoint);
					TEST_ASSERT(debugger->GetCurrentPosition().start.row == rows[i]);
					auto s = debugger->GetValueByName(L"s");
					TEST_ASSERT(UnboxValue<vint>(s) == values[i]);
					TEST_ASSERT(debugger->StepOver());
					TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
					debugger->Continue();
				}

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"Event");
		auto assembly = context->assembly.Obj();

		LoadFunction<void()>(context, L"<initialize>")();
		auto result = LoadFunction<vint()>(context, L"Main")();
		TEST_ASSERT(result == 3);
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: step into 1")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->BeginExecution(true);

				vint rows[] = { 4, 5, 6, 7, 8 };
				WString values[] = { L"", L"zero", L"one", L"two", L"three" };

				for (vint i = 0; i < sizeof(rows) / sizeof(*rows); i++)
				{
					TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
					TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == WfDebugger::PauseBreakPoint);
					TEST_ASSERT(debugger->GetCurrentPosition().start.row == rows[i]);
					auto s = debugger->GetValueByName(L"s");
					TEST_ASSERT((s.IsNull() && values[i] == L"") || UnboxValue<WString>(s) == values[i]);
					TEST_ASSERT(debugger->StepInto());
					TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
					debugger->Continue();
				}

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"Assignment");
		auto assembly = context->assembly.Obj();

		LoadFunction<void()>(context, L"<initialize>")();
		auto result = LoadFunction<WString()>(context, L"Main")();
		TEST_ASSERT(result == L"three");
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: step into 2")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->BeginExecution(true);

				vint rows[] = { 19, 20, 4, 20, 21, 9, 21, 22, 14, 22, 23 };
				WString values[] = { L"", L"zero", L"", L"zero", L"one", L"", L"one", L"two", L"", L"two", L"three" };

				for (vint i = 0; i < sizeof(rows) / sizeof(*rows); i++)
				{
					TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
					TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == WfDebugger::PauseBreakPoint);
					TEST_ASSERT(debugger->GetCurrentPosition().start.row == rows[i]);
					auto s = debugger->GetValueByName(L"s");
					TEST_ASSERT((s.IsNull() && values[i] == L"") || UnboxValue<WString>(s) == values[i]);
					TEST_ASSERT(debugger->StepInto());
					TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
					debugger->Continue();
				}

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"Function");
		auto assembly = context->assembly.Obj();

		LoadFunction<void()>(context, L"<initialize>")();
		auto result = LoadFunction<WString()>(context, L"Main")();
		TEST_ASSERT(result == L"three");
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: step into 3")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->BeginExecution(true);

				vint rows[] = { 13, 14, 15, 8, 6, 15, 16, 8, 6, 16, 17, 8, 6, 17, 18 };
				vint values[] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3 };

				for (vint i = 0; i < sizeof(rows) / sizeof(*rows); i++)
				{
					TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
					TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == WfDebugger::PauseBreakPoint);
					TEST_ASSERT(debugger->GetCurrentPosition().start.row == rows[i]);
					auto s = debugger->GetValueByName(L"s");
					TEST_ASSERT(UnboxValue<vint>(s) == values[i]);
					TEST_ASSERT(debugger->StepInto());
					TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
					debugger->Continue();
				}

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"Event");
		auto assembly = context->assembly.Obj();

		LoadFunction<void()>(context, L"<initialize>")();
		auto result = LoadFunction<vint()>(context, L"Main")();
		TEST_ASSERT(result == 3);
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"调试器：单步跳出")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->BeginExecution(true);

				TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
				TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == WfDebugger::PauseBreakPoint);
				TEST_ASSERT(debugger->GetCurrentPosition().start.row == 19);
				TEST_ASSERT(debugger->GetCurrentThreadContext()->stackFrames.Count() == 1);

				auto s = debugger->GetValueByName(L"s");
				TEST_ASSERT((s.IsNull()));

				TEST_ASSERT(debugger->StepOver());
				TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
				debugger->Continue();

				TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
				TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == WfDebugger::PauseBreakPoint);
				TEST_ASSERT(debugger->GetCurrentPosition().start.row == 20);
				TEST_ASSERT(debugger->GetCurrentThreadContext()->stackFrames.Count() == 1);

				s = debugger->GetValueByName(L"s");
				TEST_ASSERT(UnboxValue<WString>(s) == L"zero");

				TEST_ASSERT(debugger->StepInto());
				TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
				debugger->Continue();

				TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
				TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == WfDebugger::PauseBreakPoint);
				TEST_ASSERT(debugger->GetCurrentPosition().start.row == 4);
				TEST_ASSERT(debugger->GetCurrentThreadContext()->stackFrames.Count() == 2);

				TEST_ASSERT(debugger->StepOut());
				TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
				debugger->Continue();

				TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
				TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == WfDebugger::PauseBreakPoint);
				TEST_ASSERT(debugger->GetCurrentPosition().start.row == 21);
				TEST_ASSERT(debugger->GetCurrentThreadContext()->stackFrames.Count() == 1);

				s = debugger->GetValueByName(L"s");
				TEST_ASSERT(UnboxValue<WString>(s) == L"one");

				TEST_ASSERT(debugger->Run());
				TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
				debugger->Continue();

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"Function");

		LoadFunction<void()>(context, L"<initialize>")();
		auto result = LoadFunction<WString()>(context, L"Main")();
		TEST_ASSERT(result == L"three");
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: integration 1")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);

				debugger->BeginExecution(true);
				auto assembly = debugger->GetCurrentThreadContext()->globalContext->assembly.Obj();
				auto variable = assembly->variableNames.IndexOf(L"s");
				auto type = GetTypeDescriptor(L"test::ObservableValue");
				auto prop = type->GetPropertyByName(L"Value", true);
				auto ev = type->GetEventByName(L"ValueChanged", true);
				auto method = type->GetMethodGroupByName(L"GetDisplayName", true)->GetMethod(0);
				TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Read(assembly, variable)) == 0);
				TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Write(assembly, variable)) == 1);
				TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Create(type)) == 2);
				TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Get(nullptr, prop)) == 3);
				TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Set(nullptr, prop)) == 4);
				TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Attach(nullptr, ev)) == 5);
				TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Detach(nullptr, ev)) == 6);
				TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Invoke(nullptr, method)) == 7);
				debugger->Run();
				debugger->Continue();

				vint rows[] = { 8, 9, 10, 11, 12, 13, 14, 15 };
				vint breakPoints[] = { 0, 1, 2, 3, 4, 5, 6, 7 };

				for (vint i = 0; i < sizeof(rows) / sizeof(*rows); i++)
				{
					TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByBreakPoint);
					TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == breakPoints[i]);
					TEST_ASSERT(debugger->GetCurrentPosition().start.row == rows[i]);
					TEST_ASSERT(debugger->Run());
					TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
					debugger->Continue();
				}

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"Operation");

		LoadFunction<void()>(context, L"<initialize>")();
		LoadFunction<void()>(context, L"Main")();
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: integration 2")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);

				debugger->BeginExecution(true);
				auto assembly = debugger->GetCurrentThreadContext()->globalContext->assembly.Obj();
				auto variable = assembly->variableNames.IndexOf(L"s");
				auto type = GetTypeDescriptor(L"test::ObservableValue");
				auto prop = type->GetPropertyByName(L"Value", true);
				auto ev = type->GetEventByName(L"ValueChanged", true);
				auto method = type->GetMethodGroupByName(L"GetDisplayName", true)->GetMethod(0);
				TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Read(assembly, variable)) == 0);
				TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Write(assembly, variable)) == 1);
				TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Create(type)) == 2);
				debugger->Run();
				debugger->Continue();

				vint rows[] = { 8, 9, 10, 11, 12, 13, 14, 15 };
				vint breakPoints[] = { 0, 1, 2, 3, 4, 5, 6, 7 };

				for (vint i = 0; i < sizeof(rows) / sizeof(*rows); i++)
				{
					TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByBreakPoint);
					TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == breakPoints[i]);
					TEST_ASSERT(debugger->GetCurrentPosition().start.row == rows[i]);

					if (debugger->GetLastActivatedBreakPoint() == 2)
					{
						debugger->StepOver();
						TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
						debugger->Continue();

						TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
						TEST_ASSERT(debugger->GetLastActivatedBreakPoint() == WfDebugger::PauseBreakPoint);
						TEST_ASSERT(debugger->GetCurrentPosition().start.row == rows[i] + 1);

						auto value = debugger->GetValueByName(L"o");
						TEST_ASSERT(value.GetTypeDescriptor()->GetTypeName() == L"test::ObservableValue");
						TEST_ASSERT(value.GetRawPtr() != nullptr);

						TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Get(value.GetRawPtr(), prop)) == 3);
						TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Set(value.GetRawPtr(), prop)) == 4);
						TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Attach(value.GetRawPtr(), ev)) == 5);
						TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Detach(value.GetRawPtr(), ev)) == 6);
						TEST_ASSERT(debugger->AddBreakPoint(WfBreakPoint::Invoke(value.GetRawPtr(), method)) == 7);
					}

					TEST_ASSERT(debugger->Run());
					TEST_ASSERT(debugger->GetState() == WfDebugger::Continue);
					debugger->Continue();
				}

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"Operation");

		LoadFunction<void()>(context, L"<initialize>")();
		LoadFunction<void()>(context, L"Main")();
		ResetDebuggerForCurrentThread();
	});

	auto AssertException = [](Ptr<WfRuntimeExceptionInfo> info, bool uncatch)
	{
		TEST_ASSERT(info->GetMessage() == L"Exception");
		TEST_ASSERT(info->GetFatal() == false);
		TEST_ASSERT(info->callStack.Count() == 3);
		{
			auto callStack = info->callStack[0];
			TEST_ASSERT(callStack->GetFunctionName() == L"Update");
			TEST_ASSERT(callStack->GetRowBeforeCodegen() == 8);
			auto globalVariables = callStack->GetGlobalVariables();
			TEST_ASSERT(globalVariables);
			TEST_ASSERT(globalVariables->GetCount() == 1);
			TEST_ASSERT(UnboxValue<vint>(globalVariables->Get(BoxValue(WString(L"s")))) == 0);

			auto capturedVariables = callStack->GetCapturedVariables();
			TEST_ASSERT(capturedVariables);
			TEST_ASSERT(capturedVariables->GetCount() == 0);

			auto arguments = callStack->GetLocalArguments();
			TEST_ASSERT(arguments);
			TEST_ASSERT(arguments->GetCount() == 2);
			TEST_ASSERT(UnboxValue<vint>(arguments->Get(BoxValue(WString(L"a")))) == 0);
			TEST_ASSERT(UnboxValue<vint>(arguments->Get(BoxValue(WString(L"b")))) == 1);

			auto localVariables = callStack->GetLocalVariables();
			TEST_ASSERT(localVariables);
			TEST_ASSERT(localVariables->GetCount() == 0);
		}
		{
			auto callStack = info->callStack[1];
			TEST_ASSERT(callStack->assembly == nullptr);
		}
		{
			auto callStack = info->callStack[2];
			TEST_ASSERT(callStack->GetFunctionName() == (uncatch ? L"Main" : L"Main2"));
			TEST_ASSERT(callStack->GetRowBeforeCodegen() == (uncatch ? 15 : 25));
			auto globalVariables = callStack->GetGlobalVariables();
			TEST_ASSERT(globalVariables);
			TEST_ASSERT(globalVariables->GetCount() == 1);
			TEST_ASSERT(UnboxValue<vint>(globalVariables->Get(BoxValue(WString(L"s")))) == 0);

			auto capturedVariables = callStack->GetCapturedVariables();
			TEST_ASSERT(capturedVariables);
			TEST_ASSERT(capturedVariables->GetCount() == 0);

			auto arguments = callStack->GetLocalArguments();
			TEST_ASSERT(arguments);
			TEST_ASSERT(arguments->GetCount() == 0);

			auto localVariables = callStack->GetLocalVariables();
			TEST_ASSERT(localVariables);
			TEST_ASSERT(localVariables->GetCount() == (uncatch ? 1 : 2));
			TEST_ASSERT(localVariables->Get(BoxValue(WString(L"o"))).GetTypeDescriptor()->GetTypeName() == L"test::ObservableValue");
			if (!uncatch)
			{
				TEST_ASSERT(localVariables->Get(BoxValue(WString(L"<catch>ex"))).IsNull());
			}
		}
	};

	TEST_CASE(L"Test debugger: exception 1")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->BeginExecution(false);
				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"RaiseException");

		LoadFunction<void()>(context, L"<initialize>")();
		try
		{
			LoadFunction<vint()>(context, L"Main")();
			TEST_ASSERT(false);
		}
		catch (const WfRuntimeException& ex)
		{
			TEST_ASSERT(ex.Message() == L"Exception");
			TEST_ASSERT(ex.IsFatal() == false);
			AssertException(ex.GetInfo(), true);
		}
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: exception 2")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[&](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->SetBreakException(true);
				debugger->BeginExecution(false);

				TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
				TEST_ASSERT(debugger->GetCurrentThreadContext()->exceptionInfo);
				AssertException(debugger->GetCurrentThreadContext()->exceptionInfo, true);
				debugger->Run();
				debugger->Continue();

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"RaiseException");

		LoadFunction<void()>(context, L"<initialize>")();
		try
		{
			LoadFunction<vint()>(context, L"Main")();
			TEST_ASSERT(false);
		}
		catch (const WfRuntimeException& ex)
		{
			TEST_ASSERT(ex.Message() == L"Exception");
			TEST_ASSERT(ex.IsFatal() == false);
			AssertException(ex.GetInfo(), true);
		}
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: exception 3")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[&](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->SetBreakException(true);
				debugger->BeginExecution(false);

				TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
				TEST_ASSERT(debugger->GetCurrentThreadContext()->exceptionInfo);
				AssertException(debugger->GetCurrentThreadContext()->exceptionInfo, true);
				debugger->Stop();
				debugger->Continue();

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"RaiseException");

		LoadFunction<void()>(context, L"<initialize>")();
		try
		{
			LoadFunction<vint()>(context, L"Main")();
			TEST_ASSERT(false);
		}
		catch (const WfRuntimeException& ex)
		{
			TEST_ASSERT(ex.Message() == L"Internal error: Debugger stopped the program.");
			TEST_ASSERT(ex.IsFatal() == true);
			AssertException(ex.GetInfo(), true);
		}
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: exception 4")
	{
		auto debugger = Ptr(new MultithreadDebugger(
			[&](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->SetBreakException(true);
				debugger->BeginExecution(false);

				TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
				TEST_ASSERT(debugger->GetCurrentThreadContext()->exceptionInfo);
				AssertException(debugger->GetCurrentThreadContext()->exceptionInfo, false);
				debugger->Stop();
				debugger->Continue();

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromSample(L"RaiseException");

		LoadFunction<void()>(context, L"<initialize>")();
		try
		{
			LoadFunction<vint()>(context, L"Main2")();
			TEST_ASSERT(false);
		}
		catch (const WfRuntimeException& ex)
		{
			TEST_ASSERT(ex.Message() == L"Internal error: Debugger stopped the program.");
		}
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: caught exception keeps info but resumes execution")
	{
		auto AssertCaughtTryCatchException = [](Ptr<WfRuntimeExceptionInfo> info)
		{
			TEST_ASSERT(info);
			TEST_ASSERT(info->GetMessage() == L"Test1::catch");
			TEST_ASSERT(info->GetFatal() == false);
			TEST_ASSERT(info->callStack.Count() > 0);
		};

		auto debugger = Ptr(new MultithreadDebugger(
			[&](MultithreadDebugger* debugger)
			{
				debugger->BeginExecution(false);
				debugger->SetBreakException(true);
				debugger->BeginExecution(false);

				TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
				TEST_ASSERT(debugger->GetCurrentThreadContext()->status == WfRuntimeExecutionStatus::RaisedException);
				TEST_ASSERT(debugger->GetCurrentThreadContext()->exceptionInfo);
				AssertCaughtTryCatchException(debugger->GetCurrentThreadContext()->exceptionInfo);

				TEST_ASSERT(debugger->StepOver());
				debugger->Continue();

				TEST_ASSERT(debugger->GetState() == WfDebugger::PauseByOperation);
				TEST_ASSERT(debugger->GetCurrentThreadContext()->status == WfRuntimeExecutionStatus::Executing);
				TEST_ASSERT(debugger->GetCurrentThreadContext()->exceptionInfo);
				AssertCaughtTryCatchException(debugger->GetCurrentThreadContext()->exceptionInfo);
				{
					auto currentContext = debugger->GetCurrentThreadContext();
					auto currentFrame = currentContext->stackFrames.Count() - 1;
					auto beforePosition = debugger->GetCurrentPosition(true, currentContext, currentFrame);
					auto afterPosition = debugger->GetCurrentPosition(false, currentContext, currentFrame);
					TEST_ASSERT(beforePosition.codeIndex >= 0 || afterPosition.codeIndex >= 0);
					TEST_ASSERT(beforePosition.start.row >= 0 || afterPosition.start.row >= 0);
				}
				debugger->SetBreakException(false);
				TEST_ASSERT(debugger->Run());
				debugger->Continue();

				TEST_ASSERT(debugger->GetState() == WfDebugger::Stopped);
			}));
		SetDebuggerForCurrentThread(debugger);

		auto context = CreateThreadContextFromCodegenSample(L"TryCatch");

		LoadFunction<void()>(context, L"<initialize>")();
		auto result = LoadFunction<WString()>(context, L"main")();
		TEST_ASSERT(result == L"[Test1::catch][Test2::catch][Test2::finally][Test3::catch1][Test3::finally1][Test3::catch2][Test3::finally2][Test4::finally1][Test4::finally2]");
		ResetDebuggerForCurrentThread();
	});

	TEST_CASE(L"Test debugger: duplicated variable names are preserved in call stack dictionaries")
	{
		class TestCallStackInfo : public WfRuntimeCallStackInfo
		{
		public:
			using WfRuntimeCallStackInfo::GetVariables;
		};

		TestCallStackInfo callStackInfo;
		List<WString> names;
		names.Add(L"ex");
		names.Add(L"ex");
		names.Add(L"ex");

		auto context = Ptr(new WfRuntimeVariableContext);
		context->variables.Resize(names.Count());
		context->variables[0] = BoxValue(WString(L"first"));
		context->variables[1] = BoxValue(WString(L"second"));
		context->variables[2] = BoxValue(WString(L"third"));

		Ptr<IValueReadonlyDictionary> cache;
		auto dictionary = callStackInfo.GetVariables(names, context, cache);
		TEST_ASSERT(dictionary);
		TEST_ASSERT(dictionary->GetCount() == 3);
		TEST_ASSERT(UnboxValue<WString>(dictionary->GetKeys()->Get(0)) == L"ex");
		TEST_ASSERT(UnboxValue<WString>(dictionary->GetKeys()->Get(1)) == L"ex");
		TEST_ASSERT(UnboxValue<WString>(dictionary->GetKeys()->Get(2)) == L"ex");
		TEST_ASSERT(UnboxValue<WString>(dictionary->GetValues()->Get(0)) == L"first");
		TEST_ASSERT(UnboxValue<WString>(dictionary->GetValues()->Get(1)) == L"second");
		TEST_ASSERT(UnboxValue<WString>(dictionary->GetValues()->Get(2)) == L"third");
		TEST_ASSERT(UnboxValue<WString>(dictionary->Get(BoxValue(WString(L"ex")))) == L"first");
	});
}
