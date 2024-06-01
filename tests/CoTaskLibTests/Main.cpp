#define CATCH_CONFIG_MAIN
#include <Catch2/catch.hpp>
#include <CoTaskLib.hpp>

Co::Task<void> DelayFrameTest(int32* pValue)
{
	*pValue = 1;
	co_await Co::DelayFrame();
	*pValue = 2;
	co_await Co::DelayFrame(3);
	*pValue = 3;
}

TEST_CASE("DelayFrame")
{
	int32 value = 0;

	auto task = DelayFrameTest(&value);
	REQUIRE(value == 1); // 呼び出し時点で最初のsuspendまでは実行される

	const auto runner = std::move(task).runScoped();

	REQUIRE(value == 1); // runScopedの実行自体ではresumeしない
	REQUIRE(runner.isFinished() == false);

	System::Update();
	REQUIRE(value == 2); // DelayFrame()の後が実行される
	REQUIRE(runner.isFinished() == false);

	System::Update();
	REQUIRE(value == 2); // DelayFrame(3)の待機中なので3にならない
	REQUIRE(runner.isFinished() == false);

	System::Update();
	REQUIRE(value == 2); // DelayFrame(3)の待機中なので3にならない
	REQUIRE(runner.isFinished() == false);

	System::Update();
	REQUIRE(value == 3); // DelayFrame(3)の後が実行される
	REQUIRE(runner.isFinished() == true); // ここで完了

	System::Update();
	REQUIRE(value == 3); // すでに完了しているので何も起こらない
	REQUIRE(runner.isFinished() == true);
}

struct TestClock : ISteadyClock
{
	uint64 microsec = 0;

	uint64 getMicrosec() override
	{
		return microsec;
	}
};

Co::Task<void> DelayTimeTest(int32* pValue, ISteadyClock* pSteadyClock)
{
	*pValue = 1;
	co_await Co::Delay(1s, pSteadyClock);
	*pValue = 2;
	co_await Co::Delay(3s, pSteadyClock);
	*pValue = 3;
}

TEST_CASE("DelayTime")
{
	TestClock clock;
	int32 value = 0;

	const auto runner = DelayTimeTest(&value, &clock).runScoped();
	REQUIRE(value == 1); // 呼び出し時点で最初のsuspendまでは実行される

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value == 1); // 変化なし
	REQUIRE(runner.isFinished() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value == 1); // まだ1秒経過していないので変化なし
	REQUIRE(runner.isFinished() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value == 2); // 1秒経過したので2になる
	REQUIRE(runner.isFinished() == false);

	// 3.999秒
	clock.microsec = 3'999'000;
	System::Update();
	REQUIRE(value == 2); // まだ3秒経過していないので変化なし
	REQUIRE(runner.isFinished() == false);

	// 4.001秒
	clock.microsec = 4'001'000;
	System::Update();
	REQUIRE(value == 3); // 3秒経過したので3になる
	REQUIRE(runner.isFinished() == true); // ここで完了

	// 5秒
	clock.microsec = 5'000'000;
	System::Update();
	REQUIRE(value == 3); // すでに完了しているので何も起こらない
	REQUIRE(runner.isFinished() == true);
}

TEST_CASE("Finish callback")
{
	int32 finishCallbackCount = 0;
	int32 cancelCallbackCount = 0;
	auto runner = Co::DelayFrame(3).runScoped([&] { ++finishCallbackCount; }, [&] { ++cancelCallbackCount; });

	REQUIRE(finishCallbackCount == 0);
	REQUIRE(cancelCallbackCount == 0);

	System::Update();

	REQUIRE(finishCallbackCount == 0);
	REQUIRE(cancelCallbackCount == 0);

	System::Update();

	REQUIRE(finishCallbackCount == 0);
	REQUIRE(cancelCallbackCount == 0);

	System::Update();

	REQUIRE(finishCallbackCount == 1); // 3フレーム後に呼ばれる
	REQUIRE(cancelCallbackCount == 0);

	System::Update();

	REQUIRE(finishCallbackCount == 1); // 多重に呼ばれない
	REQUIRE(cancelCallbackCount == 0);
}

TEST_CASE("Cancel callback")
{
	int32 finishCallbackCount = 0;
	int32 cancelCallbackCount = 0;
	{
		const auto runner = Co::DelayFrame(3).runScoped([&] { ++finishCallbackCount; }, [&] { ++cancelCallbackCount; });

		REQUIRE(finishCallbackCount == 0);
		REQUIRE(cancelCallbackCount == 0);

		System::Update();

		REQUIRE(finishCallbackCount == 0);
		REQUIRE(cancelCallbackCount == 0);

		System::Update();

		REQUIRE(finishCallbackCount == 0);
		REQUIRE(cancelCallbackCount == 0);

		// 2フレーム目でキャンセル
	}

	REQUIRE(finishCallbackCount == 0);
	REQUIRE(cancelCallbackCount == 1); // キャンセルされたタイミングで即座に呼ばれる

	System::Update();

	REQUIRE(finishCallbackCount == 0); // キャンセルされたので呼ばれない
	REQUIRE(cancelCallbackCount == 1);

	System::Update();

	REQUIRE(finishCallbackCount == 0);
	REQUIRE(cancelCallbackCount == 1);
}

Co::Task<int32> CoReturnTest()
{
	co_return 42;
}

Co::Task<void> CoReturnTestCaller(int32* pValue)
{
	*pValue = co_await CoReturnTest();
}

TEST_CASE("co_return")
{
	int32 value = 0;

	auto task = CoReturnTestCaller(&value);
	REQUIRE(value == 42); // runScopedを呼ばなくても、呼び出し時点で最初のsuspendまで実行される
}

Co::Task<int32> CoReturnWithDelayTest()
{
	co_await Co::DelayFrame();
	co_return 42;
}

Co::Task<void> CoReturnWithDelayTestCaller(int32* pValue)
{
	*pValue = 1;
	*pValue = co_await CoReturnWithDelayTest();
}

TEST_CASE("co_return with delay")
{
	int32 value = 0;

	auto task = CoReturnWithDelayTestCaller(&value);
	REQUIRE(value == 1); // 1回目のsuspendまで実行される

	const auto runner = std::move(task).runScoped();
	REQUIRE(value == 1); // runScopedの実行自体ではresumeしない

	System::Update();
	REQUIRE(value == 42); // DelayFrame()の後が実行され、co_awaitで受け取った値が返る

	System::Update();
	REQUIRE(value == 42); // すでに完了しているので何も起こらない
}

Co::Task<void> WaitForeverTest(int32* pValue)
{
	*pValue = 1;
	co_await Co::WaitForever();
	*pValue = 2;
}

TEST_CASE("WaitForever")
{
	int32 value = 0;
	bool isFinished = false;
	bool isCanceled = false;

	{
		const auto runner = WaitForeverTest(&value).runScoped([&] { isFinished = true; }, [&] { isCanceled = true; });
		REQUIRE(value == 1); // 呼び出し時点で最初のsuspendまでは実行される
		REQUIRE(isFinished == false);
		REQUIRE(isCanceled == false);

		// 10回Updateしても完了しない
		for (int32 i = 0; i < 10; ++i)
		{
			System::Update();
			REQUIRE(value == 1);
			REQUIRE(runner.isFinished() == false);
		}
	}

	// キャンセルされたのでisCanceledがtrueになる
	REQUIRE(value == 1);
	REQUIRE(isFinished == false);
	REQUIRE(isCanceled == true);
}

Co::Task<void> WaitUntilTest(bool* pCondition)
{
	co_await Co::WaitUntil([&] { return *pCondition; });
}

TEST_CASE("WaitUntil")
{
	bool condition = false;

	const auto runner = WaitUntilTest(&condition).runScoped();
	REQUIRE(runner.isFinished() == false);

	// 条件を満たさないのでUpdateしても完了しない
	System::Update();
	REQUIRE(runner.isFinished() == false);

	// 条件を満たしてもUpdateが呼ばれるまでは完了しない
	condition = true;
	REQUIRE(runner.isFinished() == false);

	// 条件を満たした後の初回のUpdateで完了する
	System::Update();
	REQUIRE(runner.isFinished() == true);
}


TEST_CASE("Finish WaitUntil immediately")
{
	bool condition = true;

	// 条件を満たしているので即座に完了する
	auto task = WaitUntilTest(&condition);
	REQUIRE(task.isFinished() == true);

	// runScopedを呼んでも完了済みになっている
	const auto runner = std::move(task).runScoped();
	REQUIRE(runner.isFinished() == true);
}

Co::Task<void> WaitWhileTest(bool* pCondition)
{
	co_await Co::WaitWhile([&] { return *pCondition; });
}

TEST_CASE("WaitWhile")
{
	bool condition = true;

	const auto runner = WaitWhileTest(&condition).runScoped();
	REQUIRE(runner.isFinished() == false);

	// 条件を満たした状態なのでUpdateしても完了しない
	System::Update();
	REQUIRE(runner.isFinished() == false);

	// 条件を満たさなくなったものの、Updateが呼ばれるまでは完了しない
	condition = false;
	REQUIRE(runner.isFinished() == false);

	// 条件を満たさなくなった後の初回のUpdateで完了する
	System::Update();
	REQUIRE(runner.isFinished() == true);
}

TEST_CASE("Finish WaitWhile immediately")
{
	bool condition = false;

	// 条件を満たしていないので即座に完了する
	auto task = WaitWhileTest(&condition);
	REQUIRE(task.isFinished() == true);

	// runScopedを呼んでも完了済みになっている
	const auto runner = std::move(task).runScoped();
	REQUIRE(runner.isFinished() == true);
}

Co::Task<void> WaitForResultStdOptionalTest(std::optional<int32>* pResult, int32* pRet)
{
	*pRet = co_await Co::WaitForResult(pResult);
}

TEST_CASE("WaitForResult with std::optional")
{
	std::optional<int32> result;
	int32 ret = 0;

	const auto runner = WaitForResultStdOptionalTest(&result, &ret).runScoped();
	REQUIRE(runner.isFinished() == false);
	REQUIRE(ret == 0);

	// 結果が代入されるまで完了しない
	System::Update();
	REQUIRE(runner.isFinished() == false);
	REQUIRE(ret == 0);

	result = 42;

	// 結果が代入されてもUpdateが呼ばれるまでは完了しない
	REQUIRE(runner.isFinished() == false);
	REQUIRE(ret == 0);

	// 結果が代入された後の初回のUpdateで完了する
	System::Update();
	REQUIRE(runner.isFinished() == true);
	REQUIRE(ret == 42);
}

TEST_CASE("Finish WaitForResult with std::optional immediately")
{
	std::optional<int32> result = 42;
	int32 ret = 0;

	// 結果が代入されているので即座に完了する
	auto task = WaitForResultStdOptionalTest(&result, &ret);
	REQUIRE(task.isFinished() == true);
	REQUIRE(ret == 42);

	// runScopedを呼んでも完了済みになっている
	const auto runner = std::move(task).runScoped();
	REQUIRE(runner.isFinished() == true);
	REQUIRE(ret == 42);

	// Updateを呼んでも何も起こらない
	System::Update();
	REQUIRE(runner.isFinished() == true);
	REQUIRE(ret == 42);
}

Co::Task<void> WaitForResultOptionalTest(Optional<int32>* pResult, int32* pRet)
{
	*pRet = co_await Co::WaitForResult(pResult);
}

TEST_CASE("WaitForResult with Optional")
{
	Optional<int32> result;
	int32 ret = 0;

	const auto runner = WaitForResultOptionalTest(&result, &ret).runScoped();
	REQUIRE(runner.isFinished() == false);
	REQUIRE(ret == 0);

	// 結果が代入されるまで完了しない
	System::Update();
	REQUIRE(runner.isFinished() == false);
	REQUIRE(ret == 0);

	result = 42;

	// 結果が代入されてもUpdateが呼ばれるまでは完了しない
	REQUIRE(runner.isFinished() == false);
	REQUIRE(ret == 0);

	// 結果が代入された後の初回のUpdateで完了する
	System::Update();
	REQUIRE(runner.isFinished() == true);
	REQUIRE(ret == 42);
}

TEST_CASE("Finish WaitForResult with Optional immediately")
{
	Optional<int32> result = 42;
	int32 ret = 0;

	// 結果が代入されているので即座に完了する
	auto task = WaitForResultOptionalTest(&result, &ret);
	REQUIRE(task.isFinished() == true);
	REQUIRE(ret == 42);

	// runScopedを呼んでも完了済みになっている
	const auto runner = std::move(task).runScoped();
	REQUIRE(runner.isFinished() == true);
	REQUIRE(ret == 42);

	// Updateを呼んでも何も起こらない
	System::Update();
	REQUIRE(runner.isFinished() == true);
	REQUIRE(ret == 42);
}

TEST_CASE("WaitForTimer")
{
	TestClock clock;

	Timer timer{ 1s, StartImmediately::Yes, &clock };

	const auto runner = Co::WaitForTimer(&timer).runScoped();
	REQUIRE(runner.isFinished() == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(runner.isFinished() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(runner.isFinished() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(runner.isFinished() == true);
}

Co::Task<void> AssignValueWithDelay(int32 value, int32* pDest, Duration delay, ISteadyClock* pSteadyClock)
{
	*pDest = 1;
	co_await Co::Delay(delay, pSteadyClock);
	*pDest = value;
}

TEST_CASE("Co::All running tasks")
{
	TestClock clock;
	int32 value1 = 0;
	int32 value2 = 0;
	int32 value3 = 0;

	const auto runner = Co::All(
		AssignValueWithDelay(10, &value1, 1s, &clock),
		AssignValueWithDelay(20, &value2, 2s, &clock),
		AssignValueWithDelay(30, &value3, 3s, &clock)).runScoped();

	// 呼び出し時点で最初のsuspendまでは実行される
	REQUIRE(value1 == 1);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.isFinished() == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value1 == 1);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.isFinished() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value1 == 1);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.isFinished() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.isFinished() == false);

	// 1.999秒
	clock.microsec = 1'999'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.isFinished() == false);

	// 2.001秒
	clock.microsec = 2'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 20);
	REQUIRE(value3 == 1);
	REQUIRE(runner.isFinished() == false);

	// 2.999秒
	clock.microsec = 2'999'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 20);
	REQUIRE(value3 == 1);
	REQUIRE(runner.isFinished() == false);

	// 3.001秒
	clock.microsec = 3'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 20);
	REQUIRE(value3 == 30);
	REQUIRE(runner.isFinished() == true);
}

Co::Task<int32> GetValueWithDelay(int32 value, Duration delay, ISteadyClock* pSteadyClock)
{
	co_await Co::Delay(delay, pSteadyClock);
	co_return value;
}

Co::Task<void> GetValueWithDelayCallerWithAll(int32* pDest1, int32* pDest2, int32* pDest3, ISteadyClock* pSteadyClock)
{
	std::tie(*pDest1, *pDest2, *pDest3) = co_await Co::All(
		GetValueWithDelay(10, 1s, pSteadyClock),
		GetValueWithDelay(20, 2s, pSteadyClock),
		GetValueWithDelay(30, 3s, pSteadyClock));
}

TEST_CASE("Co::All return value")
{
	TestClock clock;
	int32 value1 = 0;
	int32 value2 = 0;
	int32 value3 = 0;

	const auto runner = GetValueWithDelayCallerWithAll(&value1, &value2, &value3, &clock).runScoped();

	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.isFinished() == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.isFinished() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.isFinished() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value1 == 0); // 全部完了するまで代入されない
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.isFinished() == false);

	// 1.999秒
	clock.microsec = 1'999'000;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.isFinished() == false);

	// 2.001秒
	clock.microsec = 2'001'000;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0); // 全部完了するまで代入されない
	REQUIRE(value3 == 0);
	REQUIRE(runner.isFinished() == false);

	// 2.999秒
	clock.microsec = 2'999'000;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.isFinished() == false);

	// 3.001秒
	clock.microsec = 3'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 20);
	REQUIRE(value3 == 30);
	REQUIRE(runner.isFinished() == true);
}

Co::Task<void> PushBackValueWithDelayFrame(std::vector<int32>* pVec, int32 value)
{
	co_await Co::DelayFrame();
	pVec->push_back(value);
}

TEST_CASE("Co::All execution order")
{
	std::vector<int32> vec;

	const auto runner = Co::All(
		PushBackValueWithDelayFrame(&vec, 1),
		PushBackValueWithDelayFrame(&vec, 2),
		PushBackValueWithDelayFrame(&vec, 3)).runScoped();

	REQUIRE(runner.isFinished() == false);
	System::Update();
	REQUIRE(runner.isFinished() == true);

	// 渡した順番でresumeされる
	// (ただし、引数の評価順序自体は不定なので、最初のsuspendまでの処理の順番は保証されない点に注意)
	REQUIRE(vec.size() == 3);
	REQUIRE(vec == std::vector<int32>{ 1, 2, 3 });
}

Co::Task<void> AllWithImmediateTasks()
{
	const auto [a, b] = co_await Co::All(
		CoReturnTest(),
		CoReturnTest());

	REQUIRE(a == 42);
	REQUIRE(b == 42);
}

TEST_CASE("Co::All with immediate tasks")
{
	const auto runner = AllWithImmediateTasks().runScoped();
	REQUIRE(runner.isFinished() == true);
}

TEST_CASE("Co::Any running tasks")
{
	TestClock clock;
	int32 value1 = 0;
	int32 value2 = 0;
	int32 value3 = 0;

	const auto runner = Co::Any(
		AssignValueWithDelay(10, &value1, 1s, &clock),
		AssignValueWithDelay(20, &value2, 2s, &clock),
		AssignValueWithDelay(30, &value3, 3s, &clock)).runScoped();

	// 呼び出し時点で最初のsuspendまでは実行される
	REQUIRE(value1 == 1);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.isFinished() == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value1 == 1);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.isFinished() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value1 == 1);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.isFinished() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.isFinished() == true);
}

Co::Task<void> GetValueWithDelayCallerWithAny(Optional<int32>* pDest1, Optional<int32>* pDest2, Optional<int32>* pDest3, ISteadyClock* pSteadyClock)
{
	std::tie(*pDest1, *pDest2, *pDest3) = co_await Co::Any(
		GetValueWithDelay(10, 1s, pSteadyClock),
		GetValueWithDelay(20, 2s, pSteadyClock),
		GetValueWithDelay(30, 3s, pSteadyClock));
}

TEST_CASE("Co::Any return value")
{
	TestClock clock;
	Optional<int32> value1 = 0;
	Optional<int32> value2 = 0;
	Optional<int32> value3 = 0;

	const auto runner = GetValueWithDelayCallerWithAny(&value1, &value2, &value3, &clock).runScoped();

	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.isFinished() == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.isFinished() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.isFinished() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == none);
	REQUIRE(value3 == none);
	REQUIRE(runner.isFinished() == true);
}

Co::Task<void> AnyReturnsOptionalVoidResultTest()
{
	const auto [a, b, c] = co_await Co::Any(
		Co::DelayFrame(3),
		Co::DelayFrame(1),
		Co::DelayFrame(2));

	REQUIRE((bool)a == false);
	REQUIRE((bool)b == true);
	REQUIRE((bool)c == false);
}

TEST_CASE("Co::Any returns VoidResult")
{
	const auto runner = AnyReturnsOptionalVoidResultTest().runScoped();

	REQUIRE(runner.isFinished() == false);
	System::Update();
	REQUIRE(runner.isFinished() == true);
}

Co::Task<void> AnyReturnsOptionalVoidResultMultipleTest()
{
	const auto [a, b, c, d, e] = co_await Co::Any(
		Co::DelayFrame(4),
		Co::DelayFrame(2),
		Co::DelayFrame(3),
		Co::DelayFrame(2),
		Co::DelayFrame(2));

	REQUIRE((bool)a == false);
	REQUIRE((bool)b == true);
	REQUIRE((bool)c == false);
	REQUIRE((bool)d == true);
	REQUIRE((bool)e == true);
}

TEST_CASE("Co::Any returns VoidResult multiple")
{
	const auto runner = AnyReturnsOptionalVoidResultMultipleTest().runScoped();

	REQUIRE(runner.isFinished() == false);
	System::Update();
	REQUIRE(runner.isFinished() == false);
	System::Update();
	REQUIRE(runner.isFinished() == true);
}

TEST_CASE("Co::Any execution order")
{
	std::vector<int32> vec;

	const auto runner = Co::Any(
		PushBackValueWithDelayFrame(&vec, 1),
		PushBackValueWithDelayFrame(&vec, 2),
		PushBackValueWithDelayFrame(&vec, 3)).runScoped();

	REQUIRE(runner.isFinished() == false);
	System::Update();
	REQUIRE(runner.isFinished() == true);

	// 渡した順番でresumeされる
	// (ただし、引数の評価順序自体は不定なので、最初のsuspendまでの処理の順番は保証されない点に注意)
	REQUIRE(vec.size() == 3);
	REQUIRE(vec == std::vector<int32>{ 1, 2, 3 });
}

Co::Task<void> AnyWithImmediateTasks()
{
	const auto [a, b] = co_await Co::Any(
		CoReturnTest(),
		Co::DelayFrame());

	REQUIRE(a == 42);
	REQUIRE((bool)b == false);
}

TEST_CASE("Co::Any with immediate tasks")
{
	const auto runner = AnyWithImmediateTasks().runScoped();
	REQUIRE(runner.isFinished() == true);
}

class TestSequence : public Co::SequenceBase<int32>
{
public:
	int32 argValue;
	bool isPreStartStarted = false;
	bool isPreStartFinished = false;
	bool isStartStarted = false;
	bool isStartFinished = false;
	bool isFadeInStarted = false;
	bool isFadeInFinished = false;
	bool isFadeOutStarted = false;
	bool isFadeOutFinished = false;
	bool isPostFadeOutStarted = false;
	bool isPostFadeOutFinished = false;

	explicit TestSequence(int32 argValue)
		: argValue(argValue)
	{
	}

private:
	Co::Task<void> preStart() override
	{
		isPreStartStarted = true;
		co_await Co::DelayFrame();
		isPreStartFinished = true;
	}

	Co::Task<void> fadeIn() override
	{
		isFadeInStarted = true;
		co_await Co::DelayFrame();
		isFadeInFinished = true;
	}

	Co::Task<int32> start() override
	{
		isStartStarted = true;
		co_await Co::DelayFrame();
		isStartFinished = true;
		co_return argValue;
	}

	Co::Task<void> fadeOut() override
	{
		isFadeOutStarted = true;
		co_await Co::DelayFrame();
		isFadeOutFinished = true;
	}

	Co::Task<void> postFadeOut() override
	{
		isPostFadeOutStarted = true;
		co_await Co::DelayFrame();
		isPostFadeOutFinished = true;
	}
};

TEST_CASE("Sequence")
{
	TestSequence sequence{ 42 };
	const auto runner = sequence.playScoped();
	REQUIRE(runner.isFinished() == false);
	REQUIRE(sequence.isPreStartStarted == true); // 呼び出し時点で最初のsuspendまでは実行される
	REQUIRE(sequence.isPreStartFinished == false);
	REQUIRE(sequence.isFadeInStarted == false);
	REQUIRE(sequence.isFadeInFinished == false);
	REQUIRE(sequence.isStartStarted == false);
	REQUIRE(sequence.isStartFinished == false);
	REQUIRE(sequence.isFadeOutStarted == false);
	REQUIRE(sequence.isFadeOutFinished == false);
	REQUIRE(sequence.isPostFadeOutStarted == false);
	REQUIRE(sequence.isPostFadeOutFinished == false);
	REQUIRE(sequence.hasResult() == false);
	REQUIRE(sequence.resultOpt() == none);

	System::Update();

	REQUIRE(runner.isFinished() == false);
	REQUIRE(sequence.isPreStartStarted == true);
	REQUIRE(sequence.isPreStartFinished == true);
	REQUIRE(sequence.isFadeInStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(sequence.isFadeInFinished == false);
	REQUIRE(sequence.isStartStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(sequence.isStartFinished == false);
	REQUIRE(sequence.isFadeOutStarted == false);
	REQUIRE(sequence.isFadeOutFinished == false);
	REQUIRE(sequence.isPostFadeOutStarted == false);
	REQUIRE(sequence.isPostFadeOutFinished == false);
	REQUIRE(sequence.hasResult() == false);
	REQUIRE(sequence.resultOpt() == none);

	System::Update();

	REQUIRE(runner.isFinished() == false);
	REQUIRE(sequence.isPreStartStarted == true);
	REQUIRE(sequence.isPreStartFinished == true);
	REQUIRE(sequence.isFadeInStarted == true);
	REQUIRE(sequence.isFadeInFinished == true);
	REQUIRE(sequence.isStartStarted == true);
	REQUIRE(sequence.isStartFinished == true);
	REQUIRE(sequence.isFadeOutStarted == true);
	REQUIRE(sequence.isFadeOutFinished == false);
	REQUIRE(sequence.isPostFadeOutStarted == false);
	REQUIRE(sequence.isPostFadeOutFinished == false);

	// startが完了した時点で結果が取得できる
	REQUIRE(sequence.hasResult() == true);
	REQUIRE(sequence.resultOpt() == 42);
	REQUIRE(sequence.result() == 42);

	System::Update();

	REQUIRE(runner.isFinished() == false);
	REQUIRE(sequence.isPreStartStarted == true);
	REQUIRE(sequence.isPreStartFinished == true);
	REQUIRE(sequence.isFadeInStarted == true);
	REQUIRE(sequence.isFadeInFinished == true);
	REQUIRE(sequence.isStartStarted == true);
	REQUIRE(sequence.isStartFinished == true);
	REQUIRE(sequence.isFadeOutStarted == true);
	REQUIRE(sequence.isFadeOutFinished == true);
	REQUIRE(sequence.isPostFadeOutStarted == true);
	REQUIRE(sequence.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.isFinished() == true);
	REQUIRE(sequence.isPreStartStarted == true);
	REQUIRE(sequence.isPreStartFinished == true);
	REQUIRE(sequence.isFadeInStarted == true);
	REQUIRE(sequence.isFadeInFinished == true);
	REQUIRE(sequence.isStartStarted == true);
	REQUIRE(sequence.isStartFinished == true);
	REQUIRE(sequence.isFadeOutStarted == true);
	REQUIRE(sequence.isFadeOutFinished == true);
	REQUIRE(sequence.isPostFadeOutStarted == true);
	REQUIRE(sequence.isPostFadeOutFinished == true);
}

Co::Task<void> PlaySequenceCaller(int32 value, int32* pDest)
{
	*pDest = co_await Co::Play<TestSequence>(value);
}

TEST_CASE("Co::Play<TSequence>")
{
	int32 value = 0;
	const auto runner = PlaySequenceCaller(42, &value).runScoped();
	while (!runner.isFinished())
	{
		System::Update();
	}
	REQUIRE(value == 42);
}

void Main()
{
	Co::Init();

	Console.open();
	Catch::Session().run();
	(void)std::getchar();
}
