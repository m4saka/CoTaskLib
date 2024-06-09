#include <compare>
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
	REQUIRE(value == 0); // タスク生成時点ではまだ実行されない

	const auto runner = std::move(task).runScoped();

	REQUIRE(value == 1); // runScopedにより実行が開始される
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(value == 2); // DelayFrame()の後が実行される
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(value == 2); // DelayFrame(3)の待機中なので3にならない
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(value == 2); // DelayFrame(3)の待機中なので3にならない
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(value == 3); // DelayFrame(3)の後が実行される
	REQUIRE(runner.done() == true); // ここで完了

	System::Update();
	REQUIRE(value == 3); // すでに完了しているので何も起こらない
	REQUIRE(runner.done() == true);
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
	REQUIRE(value == 1); // runScopedで最初のsuspendまで実行される

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value == 1); // 変化なし
	REQUIRE(runner.done() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value == 1); // まだ1秒経過していないので変化なし
	REQUIRE(runner.done() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value == 2); // 1秒経過したので2になる
	REQUIRE(runner.done() == false);

	// 3.999秒
	clock.microsec = 3'999'000;
	System::Update();
	REQUIRE(value == 2); // まだ3秒経過していないので変化なし
	REQUIRE(runner.done() == false);

	// 4.001秒
	clock.microsec = 4'001'000;
	System::Update();
	REQUIRE(value == 3); // 3秒経過したので3になる
	REQUIRE(runner.done() == true); // ここで完了

	// 5秒
	clock.microsec = 5'000'000;
	System::Update();
	REQUIRE(value == 3); // すでに完了しているので何も起こらない
	REQUIRE(runner.done() == true);
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
	REQUIRE(value == 0); // タスク生成時点ではまだ実行されない
	const auto runner = std::move(task).runScoped();
	REQUIRE(value == 42); // runScopedにより実行が開始される
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
	REQUIRE(value == 0); // タスク生成時点ではまだ実行されない

	const auto runner = std::move(task).runScoped();
	REQUIRE(value == 1); // runScopedにより実行が開始される

	System::Update();
	REQUIRE(value == 42); // DelayFrame()の後が実行され、co_awaitで受け取った値が返る

	System::Update();
	REQUIRE(value == 42); // すでに完了しているので何も起こらない
}

Co::Task<std::unique_ptr<int32>> CoReturnWithMoveOnlyTypeTest()
{
	co_return std::make_unique<int32>(42);
}

Co::Task<void> CoReturnWithMoveOnlyTypeTestCaller(int32* pValue)
{
	auto ptr = co_await CoReturnWithMoveOnlyTypeTest();
	*pValue = *ptr;
}

TEST_CASE("co_return with move-only type")
{
	int32 value = 0;

	auto task = CoReturnWithMoveOnlyTypeTestCaller(&value);
	REQUIRE(value == 0); // タスク生成時点ではまだ実行されない

	const auto runner = std::move(task).runScoped();
	REQUIRE(value == 42); // runScopedにより実行が開始される
}

Co::Task<std::unique_ptr<int32>> CoReturnWithMoveOnlyTypeAndDelayTest()
{
	co_await Co::DelayFrame();
	co_return std::make_unique<int32>(42);
}

Co::Task<void> CoReturnWithMoveOnlyTypeAndDelayTestCaller(int32* pValue)
{
	auto ptr = co_await CoReturnWithMoveOnlyTypeAndDelayTest();
	*pValue = *ptr;
}

TEST_CASE("co_return with move-only type and delay")
{
	int32 value = 0;

	auto task = CoReturnWithMoveOnlyTypeAndDelayTestCaller(&value);
	REQUIRE(value == 0); // タスク生成時点ではまだ実行されない

	const auto runner = std::move(task).runScoped();
	REQUIRE(value == 0); // runScopedにより実行が開始される

	System::Update();
	REQUIRE(value == 42); // DelayFrame()の後が実行され、co_awaitで受け取った値が返る

	System::Update();
	REQUIRE(value == 42); // すでに完了しているので何も起こらない
}

Co::Task<void> ThrowExceptionTest()
{
	throw std::runtime_error("test exception");
	co_return;
}

TEST_CASE("Throw exception")
{
	int32 finishCallbackCount = 0;
	int32 cancelCallbackCount = 0;

	auto task = ThrowExceptionTest();

	REQUIRE_THROWS_WITH(std::move(task).runScoped([&] { ++finishCallbackCount; }, [&] { ++cancelCallbackCount; }), "test exception");

	REQUIRE(finishCallbackCount == 0);
	REQUIRE(cancelCallbackCount == 1);
}

Co::Task<int32> ThrowExceptionWithNonVoidResultTest()
{
	throw std::runtime_error("test exception");
	co_return 42;
}

TEST_CASE("Throw exception with non-void result")
{
	int32 finishCallbackCount = 0;
	int32 cancelCallbackCount = 0;

	auto task = ThrowExceptionWithNonVoidResultTest();

	REQUIRE_THROWS_WITH(std::move(task).runScoped([&](int32) { ++finishCallbackCount; }, [&] { ++cancelCallbackCount; }), "test exception");

	REQUIRE(finishCallbackCount == 0);
	REQUIRE(cancelCallbackCount == 1);
}

Co::Task<void> ThrowExceptionWithDelayTest()
{
	co_await Co::DelayFrame();
	throw std::runtime_error("test exception");
}

TEST_CASE("Throw exception with delay")
{
	int32 finishCallbackCount = 0;
	int32 cancelCallbackCount = 0;

	auto task = ThrowExceptionWithDelayTest();

	const auto runner = std::move(task).runScoped([&] { ++finishCallbackCount; }, [&] { ++cancelCallbackCount; });

	// System::Update内で例外が発生すると以降のテスト実行に影響が出る可能性があるため、手動resumeでテスト
	REQUIRE_THROWS_WITH(Co::detail::Backend::ManualUpdate(), "test exception");

	REQUIRE(finishCallbackCount == 0);
	REQUIRE(cancelCallbackCount == 1);
}

Co::Task<int32> ThrowExceptionWithDelayAndNonVoidResultTest()
{
	co_await Co::DelayFrame();
	throw std::runtime_error("test exception");
	co_return 42;
}

TEST_CASE("Throw exception with delay and non-void result")
{
	int32 finishCallbackCount = 0;
	int32 cancelCallbackCount = 0;

	auto task = ThrowExceptionWithDelayAndNonVoidResultTest();

	const auto runner = std::move(task).runScoped([&](int32) { ++finishCallbackCount; }, [&] { ++cancelCallbackCount; });

	// System::Update内で例外が発生すると以降のテスト実行に影響が出る可能性があるため、手動resumeでテスト
	REQUIRE_THROWS_WITH(Co::detail::Backend::ManualUpdate(), "test exception");

	REQUIRE(finishCallbackCount == 0);
	REQUIRE(cancelCallbackCount == 1);
}

TEST_CASE("TaskFinishSource<void>")
{
	Co::TaskFinishSource<void> taskFinishSource;
	REQUIRE(taskFinishSource.isFinishRequested() == false);

	// 初回のリクエストではtrueが返る
	REQUIRE(taskFinishSource.requestFinish() == true);
	REQUIRE(taskFinishSource.isFinishRequested() == true);

	// 2回目以降のリクエストは受け付けない
	REQUIRE(taskFinishSource.requestFinish() == false);
	REQUIRE(taskFinishSource.isFinishRequested() == true);
}

TEST_CASE("TaskFinishSource<void>::waitForFinish")
{
	Co::TaskFinishSource<void> taskFinishSource;
	const auto runner = taskFinishSource.waitForFinish().runScoped();

	// まだ完了していない
	REQUIRE(runner.done() == false);
	REQUIRE(taskFinishSource.isFinishRequested() == false);

	// 完了リクエスト
	taskFinishSource.requestFinish();

	// System::Updateが呼ばれるまでは完了しない
	REQUIRE(runner.done() == false);
	REQUIRE(taskFinishSource.isFinishRequested() == true);

	System::Update();

	// System::Updateが呼ばれたら完了する
	REQUIRE(runner.done() == true);
	REQUIRE(taskFinishSource.isFinishRequested() == true);
}

TEST_CASE("TaskFinishSource<int32>")
{
	Co::TaskFinishSource<int32> taskFinishSource;
	REQUIRE(taskFinishSource.isFinishRequested() == false);
	REQUIRE(taskFinishSource.hasResult() == false);

	// 結果が入るまではresultは例外を投げる
	REQUIRE_THROWS_AS(taskFinishSource.result(), Error);

	// 初回のリクエストではtrueが返る
	REQUIRE(taskFinishSource.requestFinish(42) == true);
	REQUIRE(taskFinishSource.isFinishRequested() == true);

	// 結果が取得できる
	REQUIRE(taskFinishSource.hasResult() == true);
	REQUIRE(taskFinishSource.result() == 42);

	// 2回目以降の結果取得はできない
	REQUIRE(taskFinishSource.hasResult() == false);
	REQUIRE_THROWS_AS(taskFinishSource.result(), Error);
	REQUIRE(taskFinishSource.isFinishRequested() == true);

	// 2回目以降のリクエストは受け付けない
	REQUIRE(taskFinishSource.requestFinish(4242) == false);
	REQUIRE(taskFinishSource.hasResult() == false);
	REQUIRE_THROWS_AS(taskFinishSource.result(), Error);
	REQUIRE(taskFinishSource.isFinishRequested() == true);
}

TEST_CASE("TaskFinishSource<int32>::waitForFinish")
{
	Co::TaskFinishSource<int32> taskFinishSource;
	const auto runner = taskFinishSource.waitForFinish().runScoped();

	// まだ完了していない
	REQUIRE(runner.done() == false);
	REQUIRE(taskFinishSource.isFinishRequested() == false);

	// 完了リクエスト
	taskFinishSource.requestFinish(42);

	// System::Updateが呼ばれるまでは完了しない
	REQUIRE(runner.done() == false);
	REQUIRE(taskFinishSource.isFinishRequested() == true);

	System::Update();

	// System::Updateが呼ばれたら完了する
	REQUIRE(runner.done() == true);
	REQUIRE(taskFinishSource.isFinishRequested() == true);
	REQUIRE(taskFinishSource.hasResult() == true);
	REQUIRE(taskFinishSource.result() == 42);
}

TEST_CASE("TaskFinishSource<int32>::waitForResult")
{
	Co::TaskFinishSource<int32> taskFinishSource;
	Optional<int32> result = none;
	const auto runner = taskFinishSource.waitForResult().runScoped([&](int32 r) { result = r; });

	// まだ完了していない
	REQUIRE(runner.done() == false);
	REQUIRE(taskFinishSource.isFinishRequested() == false);
	REQUIRE(result == none);

	// 完了リクエスト
	taskFinishSource.requestFinish(42);

	// System::Updateが呼ばれるまでは完了しない
	REQUIRE(runner.done() == false);
	REQUIRE(taskFinishSource.isFinishRequested() == true);
	REQUIRE(result == none);

	System::Update();

	// System::Updateが呼ばれたら完了する
	REQUIRE(runner.done() == true);
	REQUIRE(taskFinishSource.isFinishRequested() == true);
	REQUIRE(result == 42);

	// 2回目以降の結果取得はできない
	// (waitForResult内で既に結果取得しているため2回目となる)
	REQUIRE_THROWS_AS(taskFinishSource.result(), Error);
	REQUIRE(taskFinishSource.isFinishRequested() == true);
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
		REQUIRE(value == 1); // runScoped呼び出し時点で最初のsuspendまでは実行される
		REQUIRE(isFinished == false);
		REQUIRE(isCanceled == false);

		// 10回Updateしても完了しない
		for (int32 i = 0; i < 10; ++i)
		{
			System::Update();
			REQUIRE(value == 1);
			REQUIRE(runner.done() == false);
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
	REQUIRE(runner.done() == false);

	// 条件を満たさないのでUpdateしても完了しない
	System::Update();
	REQUIRE(runner.done() == false);

	// 条件を満たしてもUpdateが呼ばれるまでは完了しない
	condition = true;
	REQUIRE(runner.done() == false);

	// 条件を満たした後の初回のUpdateで完了する
	System::Update();
	REQUIRE(runner.done() == true);
}

TEST_CASE("Finish WaitUntil immediately")
{
	bool condition = true;

	// 既に条件を満たしているが、タスク生成時点ではまだ実行されない
	auto task = WaitUntilTest(&condition);
	REQUIRE(task.done() == false);

	// 既に条件を満たしているのでrunScopedで開始すると即座に完了する
	const auto runner = std::move(task).runScoped();
	REQUIRE(runner.done() == true);
}

Co::Task<void> WaitWhileTest(bool* pCondition)
{
	co_await Co::WaitWhile([&] { return *pCondition; });
}

TEST_CASE("WaitWhile")
{
	bool condition = true;

	const auto runner = WaitWhileTest(&condition).runScoped();
	REQUIRE(runner.done() == false);

	// 条件を満たした状態なのでUpdateしても完了しない
	System::Update();
	REQUIRE(runner.done() == false);

	// 条件を満たさなくなったものの、Updateが呼ばれるまでは完了しない
	condition = false;
	REQUIRE(runner.done() == false);

	// 条件を満たさなくなった後の初回のUpdateで完了する
	System::Update();
	REQUIRE(runner.done() == true);
}

TEST_CASE("Finish WaitWhile immediately")
{
	bool condition = false;

	// 既に条件を満たしていないが、タスク生成時点ではまだ実行されない
	auto task = WaitWhileTest(&condition);
	REQUIRE(task.done() == false);

	// 既に条件を満たさなくなっているのでrunScopedで開始すると即座に完了する
	const auto runner = std::move(task).runScoped();
	REQUIRE(runner.done() == true);
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
	REQUIRE(runner.done() == false);
	REQUIRE(ret == 0);

	// 結果が代入されるまで完了しない
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(ret == 0);

	result = 42;

	// 結果が代入されてもUpdateが呼ばれるまでは完了しない
	REQUIRE(runner.done() == false);
	REQUIRE(ret == 0);

	// 結果が代入された後の初回のUpdateで完了する
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(ret == 42);
}

TEST_CASE("Finish WaitForResult with std::optional immediately")
{
	std::optional<int32> result = 42;
	int32 ret = 0;

	// 既に結果が代入されているが、タスク生成時点ではまだ実行されない
	auto task = WaitForResultStdOptionalTest(&result, &ret);
	REQUIRE(task.done() == false);
	REQUIRE(ret == 0);

	// 既に結果が代入されているのでrunScopedで開始すると即座に完了する
	const auto runner = std::move(task).runScoped();
	REQUIRE(runner.done() == true);
	REQUIRE(ret == 42);

	// Updateを呼んでも何も起こらない
	System::Update();
	REQUIRE(runner.done() == true);
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
	REQUIRE(runner.done() == false);
	REQUIRE(ret == 0);

	// 結果が代入されるまで完了しない
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(ret == 0);

	result = 42;

	// 結果が代入されてもUpdateが呼ばれるまでは完了しない
	REQUIRE(runner.done() == false);
	REQUIRE(ret == 0);

	// 結果が代入された後の初回のUpdateで完了する
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(ret == 42);
}

TEST_CASE("Finish WaitForResult with Optional immediately")
{
	Optional<int32> result = 42;
	int32 ret = 0;

	// 既に結果が代入されているが、タスク生成時点ではまだ実行されない
	auto task = WaitForResultOptionalTest(&result, &ret);
	REQUIRE(task.done() == false);
	REQUIRE(ret == 0);

	// 既に結果が代入されているのでrunScopedで開始すると即座に完了する
	const auto runner = std::move(task).runScoped();
	REQUIRE(runner.done() == true);
	REQUIRE(ret == 42);

	// Updateを呼んでも何も起こらない
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(ret == 42);
}

TEST_CASE("WaitForTimer")
{
	TestClock clock;

	Timer timer{ 1s, StartImmediately::Yes, &clock };

	const auto runner = Co::WaitForTimer(&timer).runScoped();
	REQUIRE(runner.done() == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(runner.done() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(runner.done() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(runner.done() == true);
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

	auto task = Co::All(
		AssignValueWithDelay(10, &value1, 1s, &clock),
		AssignValueWithDelay(20, &value2, 2s, &clock),
		AssignValueWithDelay(30, &value3, 3s, &clock));

	// タスク生成時点ではまだ実行されない
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);

	// runScopedで開始すると最初のsuspendまで実行される
	const auto runner = std::move(task).runScoped();
	REQUIRE(value1 == 1);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.done() == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value1 == 1);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.done() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value1 == 1);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.done() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.done() == false);

	// 1.999秒
	clock.microsec = 1'999'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.done() == false);

	// 2.001秒
	clock.microsec = 2'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 20);
	REQUIRE(value3 == 1);
	REQUIRE(runner.done() == false);

	// 2.999秒
	clock.microsec = 2'999'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 20);
	REQUIRE(value3 == 1);
	REQUIRE(runner.done() == false);

	// 3.001秒
	clock.microsec = 3'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 20);
	REQUIRE(value3 == 30);
	REQUIRE(runner.done() == true);
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
	REQUIRE(runner.done() == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.done() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.done() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value1 == 0); // 全部完了するまで代入されない
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.done() == false);

	// 1.999秒
	clock.microsec = 1'999'000;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.done() == false);

	// 2.001秒
	clock.microsec = 2'001'000;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0); // 全部完了するまで代入されない
	REQUIRE(value3 == 0);
	REQUIRE(runner.done() == false);

	// 2.999秒
	clock.microsec = 2'999'000;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.done() == false);

	// 3.001秒
	clock.microsec = 3'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 20);
	REQUIRE(value3 == 30);
	REQUIRE(runner.done() == true);
}

Co::Task<void> PushBackValueWithDelayFrame(std::vector<int32>* pVec, int32 value)
{
	pVec->push_back(value);
	co_await Co::DelayFrame();
	pVec->push_back(value * 10);
}

TEST_CASE("Co::All execution order")
{
	std::vector<int32> vec;

	const auto runner = Co::All(
		PushBackValueWithDelayFrame(&vec, 1),
		PushBackValueWithDelayFrame(&vec, 2),
		PushBackValueWithDelayFrame(&vec, 3)).runScoped();

	// runScopedを呼んだ時点で最初のsuspendまでは実行される
	// 渡した順番でresumeされる
	REQUIRE(vec.size() == 3);
	REQUIRE(vec == std::vector<int32>{ 1, 2, 3 });
	REQUIRE(runner.done() == false);

	System::Update();

	// 渡した順番でresumeされる
	REQUIRE(vec.size() == 6);
	REQUIRE(vec == std::vector<int32>{ 1, 2, 3, 10, 20, 30 });
	REQUIRE(runner.done() == true);
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
	REQUIRE(runner.done() == true);
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
	REQUIRE(runner.done() == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value1 == 1);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.done() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value1 == 1);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.done() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == 1);
	REQUIRE(value3 == 1);
	REQUIRE(runner.done() == true);
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
	REQUIRE(runner.done() == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.done() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value1 == 0);
	REQUIRE(value2 == 0);
	REQUIRE(value3 == 0);
	REQUIRE(runner.done() == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value1 == 10);
	REQUIRE(value2 == none);
	REQUIRE(value3 == none);
	REQUIRE(runner.done() == true);
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

	REQUIRE(runner.done() == false);
	System::Update();
	REQUIRE(runner.done() == true);
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

	REQUIRE(runner.done() == false);
	System::Update();
	REQUIRE(runner.done() == false);
	System::Update();
	REQUIRE(runner.done() == true);
}

TEST_CASE("Co::Any execution order")
{
	std::vector<int32> vec;

	const auto runner = Co::Any(
		PushBackValueWithDelayFrame(&vec, 1),
		PushBackValueWithDelayFrame(&vec, 2),
		PushBackValueWithDelayFrame(&vec, 3)).runScoped();

	// runScopedを呼んだ時点で最初のsuspendまでは実行される
	// 渡した順番でresumeされる
	REQUIRE(vec.size() == 3);
	REQUIRE(vec == std::vector<int32>{ 1, 2, 3 });
	REQUIRE(runner.done() == false);

	System::Update();

	// 渡した順番でresumeされる
	REQUIRE(vec.size() == 6);
	REQUIRE(vec == std::vector<int32>{ 1, 2, 3, 10, 20, 30 });
	REQUIRE(runner.done() == true);
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
	REQUIRE(runner.done() == true);
}

Co::Task<void> AnyWithNonCopyableResult()
{
	const auto [a, b, c] = co_await Co::Any(
		CoReturnWithMoveOnlyTypeAndDelayTest().discardResult(),
		Co::DelayFrame(),
		Co::DelayFrame(2));

	REQUIRE((bool)a == true);
	REQUIRE((bool)b == true);
	REQUIRE((bool)c == false);
}

TEST_CASE("Co::Any with non-copyable result")
{
	const auto runner = AnyWithNonCopyableResult().runScoped();
	REQUIRE(runner.done() == false);
	System::Update();
	REQUIRE(runner.done() == true);
}

struct SequenceProgress
{
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

	auto operator<=>(const SequenceProgress&) const = default;

	bool allFalse() const
	{
		return *this == SequenceProgress{};
	}
};

class TestSequence : public Co::SequenceBase<int32>
{
public:
	explicit TestSequence(int32 argValue, SequenceProgress* pProgress)
		: m_argValue(argValue)
		, m_pProgress(pProgress)
	{
	}

private:
	int32 m_argValue;
	SequenceProgress* m_pProgress;

	Co::Task<void> preStart() override
	{
		m_pProgress->isPreStartStarted = true;
		co_await Co::DelayFrame();
		m_pProgress->isPreStartFinished = true;
	}

	Co::Task<void> fadeIn() override
	{
		m_pProgress->isFadeInStarted = true;
		co_await Co::DelayFrame();
		m_pProgress->isFadeInFinished = true;
	}

	Co::Task<int32> start() override
	{
		m_pProgress->isStartStarted = true;
		co_await Co::DelayFrame();
		m_pProgress->isStartFinished = true;
		co_return m_argValue;
	}

	Co::Task<void> fadeOut() override
	{
		m_pProgress->isFadeOutStarted = true;
		co_await Co::DelayFrame();
		m_pProgress->isFadeOutFinished = true;
	}

	Co::Task<void> postFadeOut() override
	{
		m_pProgress->isPostFadeOutStarted = true;
		co_await Co::DelayFrame();
		m_pProgress->isPostFadeOutFinished = true;
	}
};

TEST_CASE("Sequence")
{
	SequenceProgress progress;
	TestSequence sequence{ 42, &progress };
	const auto runner = sequence.playScoped();
	REQUIRE(sequence.done() == false);
	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true); // 呼び出し時点で最初のsuspendまでは実行される
	REQUIRE(progress.isPreStartFinished == false);
	REQUIRE(progress.isFadeInStarted == false);
	REQUIRE(progress.isFadeInFinished == false);
	REQUIRE(progress.isStartStarted == false);
	REQUIRE(progress.isStartFinished == false);
	REQUIRE(progress.isFadeOutStarted == false);
	REQUIRE(progress.isFadeOutFinished == false);
	REQUIRE(progress.isPostFadeOutStarted == false);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(sequence.done() == false);
	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(progress.isFadeInFinished == false);
	REQUIRE(progress.isStartStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(progress.isStartFinished == false);
	REQUIRE(progress.isFadeOutStarted == false);
	REQUIRE(progress.isFadeOutFinished == false);
	REQUIRE(progress.isPostFadeOutStarted == false);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(sequence.done() == false);
	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true);
	REQUIRE(progress.isFadeInFinished == true);
	REQUIRE(progress.isStartStarted == true);
	REQUIRE(progress.isStartFinished == true);
	REQUIRE(progress.isFadeOutStarted == true);
	REQUIRE(progress.isFadeOutFinished == false);
	REQUIRE(progress.isPostFadeOutStarted == false);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(sequence.done() == false);
	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true);
	REQUIRE(progress.isFadeInFinished == true);
	REQUIRE(progress.isStartStarted == true);
	REQUIRE(progress.isStartFinished == true);
	REQUIRE(progress.isFadeOutStarted == true);
	REQUIRE(progress.isFadeOutFinished == true);
	REQUIRE(progress.isPostFadeOutStarted == true);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(sequence.done() == true);
	REQUIRE(runner.done() == true);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true);
	REQUIRE(progress.isFadeInFinished == true);
	REQUIRE(progress.isStartStarted == true);
	REQUIRE(progress.isStartFinished == true);
	REQUIRE(progress.isFadeOutStarted == true);
	REQUIRE(progress.isFadeOutFinished == true);
	REQUIRE(progress.isPostFadeOutStarted == true);
	REQUIRE(progress.isPostFadeOutFinished == true);
}

Co::Task<void> PlaySequenceCaller(int32 value, int32* pDest, SequenceProgress* pProgress)
{
	*pDest = co_await Co::Play<TestSequence>(value, pProgress);
}

TEST_CASE("Co::Play<TSequence>")
{
	int32 value = 0;
	SequenceProgress progress;
	const auto runner = PlaySequenceCaller(42, &value, &progress).runScoped();
	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true); // 呼び出し時点で最初のsuspendまでは実行される
	REQUIRE(progress.isPreStartFinished == false);
	REQUIRE(progress.isFadeInStarted == false);
	REQUIRE(progress.isFadeInFinished == false);
	REQUIRE(progress.isStartStarted == false);
	REQUIRE(progress.isStartFinished == false);
	REQUIRE(progress.isFadeOutStarted == false);
	REQUIRE(progress.isFadeOutFinished == false);
	REQUIRE(progress.isPostFadeOutStarted == false);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(progress.isFadeInFinished == false);
	REQUIRE(progress.isStartStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(progress.isStartFinished == false);
	REQUIRE(progress.isFadeOutStarted == false);
	REQUIRE(progress.isFadeOutFinished == false);
	REQUIRE(progress.isPostFadeOutStarted == false);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true);
	REQUIRE(progress.isFadeInFinished == true);
	REQUIRE(progress.isStartStarted == true);
	REQUIRE(progress.isStartFinished == true);
	REQUIRE(progress.isFadeOutStarted == true);
	REQUIRE(progress.isFadeOutFinished == false);
	REQUIRE(progress.isPostFadeOutStarted == false);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true);
	REQUIRE(progress.isFadeInFinished == true);
	REQUIRE(progress.isStartStarted == true);
	REQUIRE(progress.isStartFinished == true);
	REQUIRE(progress.isFadeOutStarted == true);
	REQUIRE(progress.isFadeOutFinished == true);
	REQUIRE(progress.isPostFadeOutStarted == true);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == true);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true);
	REQUIRE(progress.isFadeInFinished == true);
	REQUIRE(progress.isStartStarted == true);
	REQUIRE(progress.isStartFinished == true);
	REQUIRE(progress.isFadeOutStarted == true);
	REQUIRE(progress.isFadeOutFinished == true);
	REQUIRE(progress.isPostFadeOutStarted == true);
	REQUIRE(progress.isPostFadeOutFinished == true);

	REQUIRE(value == 42);
}

class SequenceWithVoidResult : public Co::SequenceBase<void>
{
private:
	Co::Task<void> start() override
	{
		co_await Co::DelayFrame();
	}
};

TEST_CASE("Sequence with void result")
{
	const auto runner = Co::Play<SequenceWithVoidResult>().runScoped();
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(runner.done() == true);
}

class SequenceWithMoveOnlyResult : public Co::SequenceBase<std::unique_ptr<int32>>
{
private:
	Co::Task<std::unique_ptr<int32>> start() override
	{
		co_await Co::DelayFrame();
		co_return std::make_unique<int32>(42);
	}

public:
	SequenceWithMoveOnlyResult() = default;
};

TEST_CASE("Sequence with move-only result")
{
	std::unique_ptr<int32> value;
	const auto runner = Co::Play<SequenceWithMoveOnlyResult>()
		.runScoped([&](std::unique_ptr<int32>&& result) { value = std::move(result); });
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(*value == 42);
}

class TestUpdaterSequence : public Co::UpdaterSequenceBase<void>
{
private:
	int m_count = 0;

	void update() override
	{
		if (m_count == 3)
		{
			requestFinish();
		}
		++m_count;
	}
};

TEST_CASE("UpdaterSequence")
{
	const auto runner = Co::Play<TestUpdaterSequence>().runScoped();
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(runner.done() == true);
}

class TestUpdaterSequenceWithResult : public Co::UpdaterSequenceBase<int32>
{
private:
	int m_count = 0;

	void update() override
	{
		if (m_count == 3)
		{
			requestFinish(42);
		}
		++m_count;
	}
};

TEST_CASE("UpdaterSequence with result")
{
	int32 value = 0;
	const auto runner = Co::Play<TestUpdaterSequenceWithResult>().runScoped([&](int32 result) { value = result; });
	REQUIRE(runner.done() == false);
	REQUIRE(value == 0);

	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == 0);

	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == 0);

	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(value == 42);
}

class TestUpdaterSequenceWithMoveOnlyResult : public Co::UpdaterSequenceBase<std::unique_ptr<int32>>
{
private:
	int m_count = 0;

	void update() override
	{
		if (m_count == 3)
		{
			requestFinish(std::make_unique<int32>(42));
		}
		++m_count;
	}
};

TEST_CASE("UpdaterSequence with move-only result")
{
	std::unique_ptr<int32> value;
	const auto runner = Co::Play<TestUpdaterSequenceWithMoveOnlyResult>()
		.runScoped([&](std::unique_ptr<int32>&& result) { value = std::move(result); });
	REQUIRE(runner.done() == false);
	REQUIRE(value == nullptr);

	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == nullptr);

	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == nullptr);

	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(value != nullptr);
	REQUIRE(*value == 42);
}

class TestScene : public Co::SceneBase
{
public:
	explicit TestScene(SequenceProgress* pProgress)
		: m_pProgress(pProgress)
	{
	}

private:
	SequenceProgress* m_pProgress;

	Co::Task<void> preStart() override
	{
		m_pProgress->isPreStartStarted = true;
		co_await Co::DelayFrame();
		m_pProgress->isPreStartFinished = true;
	}

	Co::Task<void> fadeIn() override
	{
		m_pProgress->isFadeInStarted = true;
		co_await Co::DelayFrame();
		m_pProgress->isFadeInFinished = true;
	}

	Co::Task<void> start() override
	{
		m_pProgress->isStartStarted = true;
		co_await Co::DelayFrame();
		m_pProgress->isStartFinished = true;
	}

	Co::Task<void> fadeOut() override
	{
		m_pProgress->isFadeOutStarted = true;
		co_await Co::DelayFrame();
		m_pProgress->isFadeOutFinished = true;
	}

	Co::Task<void> postFadeOut() override
	{
		m_pProgress->isPostFadeOutStarted = true;
		co_await Co::DelayFrame();
		m_pProgress->isPostFadeOutFinished = true;
	}
};

TEST_CASE("Co::EnterScene<TScene>")
{
	SequenceProgress progress;
	const auto runner = Co::EnterScene<TestScene>(&progress).runScoped();
	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true); // 呼び出し時点で最初のsuspendまでは実行される
	REQUIRE(progress.isPreStartFinished == false);
	REQUIRE(progress.isFadeInStarted == false);
	REQUIRE(progress.isFadeInFinished == false);
	REQUIRE(progress.isStartStarted == false);
	REQUIRE(progress.isStartFinished == false);
	REQUIRE(progress.isFadeOutStarted == false);
	REQUIRE(progress.isFadeOutFinished == false);
	REQUIRE(progress.isPostFadeOutStarted == false);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(progress.isFadeInFinished == false);
	REQUIRE(progress.isStartStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(progress.isStartFinished == false);
	REQUIRE(progress.isFadeOutStarted == false);
	REQUIRE(progress.isFadeOutFinished == false);
	REQUIRE(progress.isPostFadeOutStarted == false);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true);
	REQUIRE(progress.isFadeInFinished == true);
	REQUIRE(progress.isStartStarted == true);
	REQUIRE(progress.isStartFinished == true);
	REQUIRE(progress.isFadeOutStarted == true);
	REQUIRE(progress.isFadeOutFinished == false);
	REQUIRE(progress.isPostFadeOutStarted == false);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true);
	REQUIRE(progress.isFadeInFinished == true);
	REQUIRE(progress.isStartStarted == true);
	REQUIRE(progress.isStartFinished == true);
	REQUIRE(progress.isFadeOutStarted == true);
	REQUIRE(progress.isFadeOutFinished == true);
	REQUIRE(progress.isPostFadeOutStarted == true);
	REQUIRE(progress.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == true);
	REQUIRE(progress.isPreStartStarted == true);
	REQUIRE(progress.isPreStartFinished == true);
	REQUIRE(progress.isFadeInStarted == true);
	REQUIRE(progress.isFadeInFinished == true);
	REQUIRE(progress.isStartStarted == true);
	REQUIRE(progress.isStartFinished == true);
	REQUIRE(progress.isFadeOutStarted == true);
	REQUIRE(progress.isFadeOutFinished == true);
	REQUIRE(progress.isPostFadeOutStarted == true);
	REQUIRE(progress.isPostFadeOutFinished == true);
}

class ChainedTestScene : public Co::SceneBase
{
public:
	explicit ChainedTestScene(SequenceProgress* pProgress1, SequenceProgress* pProgress2)
		: m_pProgress1(pProgress1)
		, m_pProgress2(pProgress2)
	{
	}

private:
	SequenceProgress* m_pProgress1;
	SequenceProgress* m_pProgress2;

	Co::Task<void> preStart() override
	{
		m_pProgress1->isPreStartStarted = true;
		co_await Co::DelayFrame();
		m_pProgress1->isPreStartFinished = true;
	}

	Co::Task<void> fadeIn() override
	{
		m_pProgress1->isFadeInStarted = true;
		co_await Co::DelayFrame();
		m_pProgress1->isFadeInFinished = true;
	}

	Co::Task<void> start() override
	{
		m_pProgress1->isStartStarted = true;
		co_await Co::DelayFrame();
		REQUIRE(isRequested() == false);
		REQUIRE(requestNextScene<TestScene>(m_pProgress2) == true);
		REQUIRE(isRequested() == true);
		m_pProgress1->isStartFinished = true;
	}

	Co::Task<void> fadeOut() override
	{
		m_pProgress1->isFadeOutStarted = true;
		co_await Co::DelayFrame();
		m_pProgress1->isFadeOutFinished = true;
	}

	Co::Task<void> postFadeOut() override
	{
		m_pProgress1->isPostFadeOutStarted = true;
		co_await Co::DelayFrame();
		m_pProgress1->isPostFadeOutFinished = true;
	}
};

TEST_CASE("requestNextScene")
{
	SequenceProgress progress1;
	SequenceProgress progress2;
	const auto runner = Co::EnterScene<ChainedTestScene>(&progress1, &progress2).runScoped();
	REQUIRE(runner.done() == false);
	REQUIRE(progress1.isPreStartStarted == true); // 呼び出し時点で最初のsuspendまでは実行される
	REQUIRE(progress1.isPreStartFinished == false);
	REQUIRE(progress1.isFadeInStarted == false);
	REQUIRE(progress1.isFadeInFinished == false);
	REQUIRE(progress1.isStartStarted == false);
	REQUIRE(progress1.isStartFinished == false);
	REQUIRE(progress1.isFadeOutStarted == false);
	REQUIRE(progress1.isFadeOutFinished == false);
	REQUIRE(progress1.isPostFadeOutStarted == false);
	REQUIRE(progress1.isPostFadeOutFinished == false);
	REQUIRE(progress2.allFalse());
	
	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress1.isPreStartStarted == true);
	REQUIRE(progress1.isPreStartFinished == true);
	REQUIRE(progress1.isFadeInStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(progress1.isFadeInFinished == false);
	REQUIRE(progress1.isStartStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(progress1.isStartFinished == false);
	REQUIRE(progress1.isFadeOutStarted == false);
	REQUIRE(progress1.isFadeOutFinished == false);
	REQUIRE(progress1.isPostFadeOutStarted == false);
	REQUIRE(progress1.isPostFadeOutFinished == false);
	REQUIRE(progress2.allFalse());

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress1.isPreStartStarted == true);
	REQUIRE(progress1.isPreStartFinished == true);
	REQUIRE(progress1.isFadeInStarted == true);
	REQUIRE(progress1.isFadeInFinished == true);
	REQUIRE(progress1.isStartStarted == true);
	REQUIRE(progress1.isStartFinished == true);
	REQUIRE(progress1.isFadeOutStarted == true);
	REQUIRE(progress1.isFadeOutFinished == false);
	REQUIRE(progress1.isPostFadeOutStarted == false);
	REQUIRE(progress1.isPostFadeOutFinished == false);
	REQUIRE(progress2.allFalse());

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress1.isPreStartStarted == true);
	REQUIRE(progress1.isPreStartFinished == true);
	REQUIRE(progress1.isFadeInStarted == true);
	REQUIRE(progress1.isFadeInFinished == true);
	REQUIRE(progress1.isStartStarted == true);
	REQUIRE(progress1.isStartFinished == true);
	REQUIRE(progress1.isFadeOutStarted == true);
	REQUIRE(progress1.isFadeOutFinished == true);
	REQUIRE(progress1.isPostFadeOutStarted == true);
	REQUIRE(progress1.isPostFadeOutFinished == false);
	REQUIRE(progress2.allFalse());

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress1.isPreStartStarted == true);
	REQUIRE(progress1.isPreStartFinished == true);
	REQUIRE(progress1.isFadeInStarted == true);
	REQUIRE(progress1.isFadeInFinished == true);
	REQUIRE(progress1.isStartStarted == true);
	REQUIRE(progress1.isStartFinished == true);
	REQUIRE(progress1.isFadeOutStarted == true);
	REQUIRE(progress1.isFadeOutFinished == true);
	REQUIRE(progress1.isPostFadeOutStarted == true);
	REQUIRE(progress1.isPostFadeOutFinished == true);
	// ここから2番目のシーンに遷移
	REQUIRE(progress2.isPreStartStarted == true);
	REQUIRE(progress2.isPreStartFinished == false);
	REQUIRE(progress2.isFadeInStarted == false);
	REQUIRE(progress2.isFadeInFinished == false);
	REQUIRE(progress2.isStartStarted == false);
	REQUIRE(progress2.isStartFinished == false);
	REQUIRE(progress2.isFadeOutStarted == false);
	REQUIRE(progress2.isFadeOutFinished == false);
	REQUIRE(progress2.isPostFadeOutStarted == false);
	REQUIRE(progress2.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress2.isPreStartStarted == true);
	REQUIRE(progress2.isPreStartFinished == true);
	REQUIRE(progress2.isFadeInStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(progress2.isFadeInFinished == false);
	REQUIRE(progress2.isStartStarted == true); // fadeInとstartは同時に実行される
	REQUIRE(progress2.isStartFinished == false);
	REQUIRE(progress2.isFadeOutStarted == false);
	REQUIRE(progress2.isFadeOutFinished == false);
	REQUIRE(progress2.isPostFadeOutStarted == false);
	REQUIRE(progress2.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress2.isPreStartStarted == true);
	REQUIRE(progress2.isPreStartFinished == true);
	REQUIRE(progress2.isFadeInStarted == true);
	REQUIRE(progress2.isFadeInFinished == true);
	REQUIRE(progress2.isStartStarted == true);
	REQUIRE(progress2.isStartFinished == true);
	REQUIRE(progress2.isFadeOutStarted == true);
	REQUIRE(progress2.isFadeOutFinished == false);
	REQUIRE(progress2.isPostFadeOutStarted == false);
	REQUIRE(progress2.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(progress2.isPreStartStarted == true);
	REQUIRE(progress2.isPreStartFinished == true);
	REQUIRE(progress2.isFadeInStarted == true);
	REQUIRE(progress2.isFadeInFinished == true);
	REQUIRE(progress2.isStartStarted == true);
	REQUIRE(progress2.isStartFinished == true);
	REQUIRE(progress2.isFadeOutStarted == true);
	REQUIRE(progress2.isFadeOutFinished == true);
	REQUIRE(progress2.isPostFadeOutStarted == true);
	REQUIRE(progress2.isPostFadeOutFinished == false);

	System::Update();

	REQUIRE(runner.done() == true);
	REQUIRE(progress2.isPreStartStarted == true);
	REQUIRE(progress2.isPreStartFinished == true);
	REQUIRE(progress2.isFadeInStarted == true);
	REQUIRE(progress2.isFadeInFinished == true);
	REQUIRE(progress2.isStartStarted == true);
	REQUIRE(progress2.isStartFinished == true);
	REQUIRE(progress2.isFadeOutStarted == true);
	REQUIRE(progress2.isFadeOutFinished == true);
	REQUIRE(progress2.isPostFadeOutStarted == true);
	REQUIRE(progress2.isPostFadeOutFinished == true);
}

class TestUpdaterScene : public Co::UpdaterSceneBase
{
private:
	int m_count = 0;

	void update() override
	{
		if (m_count == 3)
		{
			requestSceneFinish();
		}
		++m_count;
	}
};

TEST_CASE("UpdaterScene")
{
	const auto runner = Co::EnterScene<TestUpdaterScene>().runScoped();
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(runner.done() == true);
}

TEST_CASE("Co::Ease")
{
	TestClock clock;

	double value = -1.0;
	auto easeTask = Co::Ease(&value, 1s)
		.setClock(&clock)
		.from(0.0)
		.to(100.0)
		.play();

	// Task生成時点ではまだ実行されない
	REQUIRE(easeTask.done() == false);
	REQUIRE(value == -1.0);

	const auto runner = std::move(easeTask).runScoped();

	// runScopedで開始すると初期値が代入される
	REQUIRE(runner.done() == false);
	REQUIRE(value == 0.0);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == 0.0);

	// 0.5秒
	clock.microsec = 500'000;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == Approx(EaseOutQuad(0.5) * 100.0));

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == Approx(EaseOutQuad(0.999) * 100.0));

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(value == 100.0);
}

TEST_CASE("Co::Ease with zero duration")
{
	double value = -1.0;
	const auto runner = Co::Ease(&value, 0s)
		.from(0.0)
		.to(100.0)
		.play()
		.runScoped();

	// 即座に終了
	REQUIRE(runner.done() == true);
	REQUIRE(value == 100.0);
}

TEST_CASE("Co::Ease and Co::Delay ends at the same time")
{
	TestClock clock;

	double value = -1.0;
	auto easeTask = Co::Ease(&value, 1s).setClock(&clock).play();

	Optional<Co::VoidResult> easeResult;
	Optional<Co::VoidResult> delayResult;

	const auto runner = Co::Any(
		std::move(easeTask),
		Co::Delay(1s, &clock)).runScoped([&](const auto& result) { std::tie(easeResult, delayResult) = result; });

	REQUIRE(runner.done() == false);
	REQUIRE((bool)easeResult == false);
	REQUIRE((bool)delayResult == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE((bool)easeResult == false);
	REQUIRE((bool)delayResult == false);

	// 0.5秒
	clock.microsec = 500'000;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE((bool)easeResult == false);
	REQUIRE((bool)delayResult == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE((bool)easeResult == false);
	REQUIRE((bool)delayResult == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE((bool)easeResult == true);
	REQUIRE((bool)delayResult == true);
	REQUIRE(value == 1.0);
}

TEST_CASE("Co::Ease::setEase")
{
	TestClock clock;

	double value = -1.0;
	auto easeTask = Co::Ease(&value, 1s)
		.setClock(&clock)
		.setEase(EaseInBounce)
		.from(0.0)
		.to(100.0)
		.play();

	const auto runner = std::move(easeTask).runScoped();

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == 0.0);

	// 0.25秒
	clock.microsec = 250'000;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == Approx(EaseInBounce(0.25) * 100.0));

	// 0.5秒
	clock.microsec = 500'000;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == Approx(EaseInBounce(0.5) * 100.0));

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == Approx(EaseInBounce(0.999) * 100.0));

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(value == 100.0);
}

TEST_CASE("Co::LinearEase")
{
	TestClock clock;

	double value = -1.0;
	auto easeTask = Co::LinearEase(&value, 1s)
		.setClock(&clock)
		.from(0.0)
		.to(100.0)
		.play();

	// Task生成時点ではまだ実行されない
	REQUIRE(easeTask.done() == false);
	REQUIRE(value == -1.0);

	const auto runner = std::move(easeTask).runScoped();

	// runScopedで開始すると初期値が代入される
	REQUIRE(runner.done() == false);
	REQUIRE(value == 0.0);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == 0.0);

	// 0.5秒
	clock.microsec = 500'000;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == Approx(50.0));

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == Approx(99.9));

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(value == 100.0);
}

TEST_CASE("Co::LinearEase with zero duration")
{
	double value = -1.0;
	const auto runner = Co::LinearEase(&value, 0s)
		.from(0.0)
		.to(100.0)
		.play()
		.runScoped();

	// 即座に終了
	REQUIRE(runner.done() == true);
	REQUIRE(value == 100.0);
}

TEST_CASE("Co::LinearEase and Co::Delay ends at the same time")
{
	TestClock clock;

	double value = -1.0;
	auto easeTask = Co::LinearEase(&value, 1s).setClock(&clock).play();

	Optional<Co::VoidResult> easeResult;
	Optional<Co::VoidResult> delayResult;

	const auto runner = Co::Any(
		std::move(easeTask),
		Co::Delay(1s, &clock)).runScoped([&](const auto& result) { std::tie(easeResult, delayResult) = result; });

	REQUIRE(runner.done() == false);
	REQUIRE((bool)easeResult == false);
	REQUIRE((bool)delayResult == false);

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE((bool)easeResult == false);
	REQUIRE((bool)delayResult == false);

	// 0.5秒
	clock.microsec = 500'000;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE((bool)easeResult == false);
	REQUIRE((bool)delayResult == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE((bool)easeResult == false);
	REQUIRE((bool)delayResult == false);

	// 1.001秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE((bool)easeResult == true);
	REQUIRE((bool)delayResult == true);
	REQUIRE(value == 1.0);
}

TEST_CASE("Co::Typewriter")
{
	TestClock clock;

	String value;
	auto typewriterTask = Co::Typewriter(&value, 0.25s, U"TEST") // ここで指定するのは1文字あたりの時間
		.setClock(&clock)
		.play();

	// Task生成時点ではまだ実行されない
	REQUIRE(typewriterTask.done() == false);
	REQUIRE(value.isEmpty());

	const auto runner = std::move(typewriterTask).runScoped();

	// runScopedで開始すると初期値が代入される
	// 1文字目は最初から表示される仕様にしている
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"T");

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"T");

	// 0.2501秒
	clock.microsec = 250'100;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"TE");

	// 0.5001秒
	clock.microsec = 500'100;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"TES");

	// 0.7501秒
	// 最後の文字が見える時間を設ける必要があるため、この時点で最後の文字まで表示される仕様にしている
	clock.microsec = 750'100;
	System::Update();
	REQUIRE(runner.done() == false); // タスク自体はまだ終了していない
	REQUIRE(value == U"TEST");

	// 1.0001秒
	clock.microsec = 1'000'100;
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(value == U"TEST");
}

TEST_CASE("Co::Typewriter with zero duration")
{
	String value;
	const auto runner = Co::Typewriter(&value, 0s, U"TEST")
		.play()
		.runScoped();

	// 即座に終了
	REQUIRE(runner.done() == true);
	REQUIRE(value == U"TEST");
}

TEST_CASE("Co::Typewriter with total duration")
{
	TestClock clock;

	String value;
	const auto runner = Co::Typewriter(&value)
		.text(U"TEST")
		.totalDuration(1s)
		.setClock(&clock)
		.play()
		.runScoped();

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"T");

	// 0.2501秒
	clock.microsec = 250'100;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"TE");

	// 0.5001秒
	clock.microsec = 500'100;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"TES");

	// 0.7501秒
	clock.microsec = 750'100;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"TEST");

	// 1.0001秒
	clock.microsec = 1'000'100;
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(value == U"TEST");
}

TEST_CASE("Co::TypewriterChar with zero duration")
{
	String value;
	const auto runner = Co::TypewriterChar([&value](String::value_type c) { value.push_back(c); }, 0s, U"TEST")
		.play()
		.runScoped();

	// 即座に終了
	REQUIRE(runner.done() == true);
	REQUIRE(value == U"TEST");
}

TEST_CASE("Co::TypewriterChar with total duration")
{
	TestClock clock;

	String value;
	const auto runner = Co::TypewriterChar([&value](String::value_type c) { value.push_back(c); })
		.text(U"TEST")
		.totalDuration(1s)
		.setClock(&clock)
		.play()
		.runScoped();

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"T");

	// 0.2501秒
	clock.microsec = 250'100;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"TE");

	// 0.5001秒
	clock.microsec = 500'100;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"TES");

	// 0.7501秒
	clock.microsec = 750'100;
	System::Update();
	REQUIRE(runner.done() == false);
	REQUIRE(value == U"TEST");

	// 1.0001秒
	clock.microsec = 1'000'100;
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(value == U"TEST");
}

void Main()
{
	Co::Init();

	Console.open();
	Catch::Session().run();
	(void)std::getchar();
}
