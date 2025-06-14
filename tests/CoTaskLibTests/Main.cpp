#include <compare>
#define CATCH_CONFIG_RUNNER
#include <catch2/catch.hpp>
#include <CoTaskLib.hpp>

#ifdef __linux__
SIV3D_SET(EngineOption::Renderer::Headless)
#endif

Co::Task<void> FromResultTest(int32* pValue)
{
	*pValue = co_await Co::FromResult(42);
}

TEST_CASE("FromResult")
{
	int32 value = 0;

	auto task = FromResultTest(&value);
	REQUIRE(value == 0); // タスク生成時点ではまだ実行されない
	const auto runner = std::move(task).runScoped();
	REQUIRE(value == 42); // runScopedにより実行が開始される
}

Co::Task<void> FromResultWithMoveOnlyTypeTest(std::unique_ptr<int32>* pValue)
{
	*pValue = co_await Co::FromResult(std::make_unique<int32>(42));
}

TEST_CASE("FromResult with move-only type")
{
	std::unique_ptr<int32> value;

	auto task = FromResultWithMoveOnlyTypeTest(&value);
	REQUIRE(value == nullptr); // タスク生成時点ではまだ実行されない
	const auto runner = std::move(task).runScoped();
	REQUIRE(value != nullptr); // runScopedにより実行が開始される
	REQUIRE(*value == 42);
}

Co::Task<void> DelayFrameTest(int32* pValue)
{
	*pValue = 1;
	co_await Co::NextFrame();
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
	REQUIRE(value == 2); // NextFrame()の後が実行される
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

Co::Task<void> DelayFrameZeroTest(int32* pValue)
{
	*pValue = 1;
	co_await Co::DelayFrame(0);
	*pValue = 2;
}

Co::Task<void> DelayFrameNegativeTest(int32* pValue)
{
	*pValue = 1;
	co_await Co::DelayFrame(-1);
	*pValue = 2;
}

TEST_CASE("DelayFrame with zero and negative values")
{
	int32 value = 0;
	
	// 0フレーム待機のテスト
	auto zeroFrameTask = DelayFrameZeroTest(&value);
	const auto zeroRunner = std::move(zeroFrameTask).runScoped();
	REQUIRE(zeroRunner.done() == true); // 0フレーム待機は即座に完了する
	REQUIRE(value == 2);
	
	// 負の値でのDelayFrameは即座に完了する
	value = 0;
	auto negativeFrameTask = DelayFrameNegativeTest(&value);
	const auto negativeRunner = std::move(negativeFrameTask).runScoped();
	REQUIRE(negativeRunner.done() == true);
	REQUIRE(value == 2); // 負の値でも即座に完了する
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

TEST_CASE("Delay time")
{
	TestClock clock;
	int32 value = 0;

	const auto runner = DelayTimeTest(&value, &clock).runScoped();
	REQUIRE(value == 1); // runScopedで最初のsuspendまで実行される

	// 0秒
	// Delay(1s)内時間: 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value == 1); // 変化なし
	REQUIRE(runner.done() == false);

	// 0.999秒
	// Delay(1s)内時間: 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value == 1); // まだ1秒経過していないので変化なし
	REQUIRE(runner.done() == false);

	// 1.001秒
	// Delay(1s)内時間: 1.001秒
	// Delay(3s)内時間: 0秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value == 2); // 1秒経過したので2になる
	REQUIRE(runner.done() == false);

	// 4.000秒
	// Delay(3s)内時間: 2.999秒
	clock.microsec = 4'000'000;
	System::Update();
	REQUIRE(value == 2); // まだ3秒経過していないので変化なし
	REQUIRE(runner.done() == false);

	// 4.002秒
	// Delay(3s)内時間: 3.001秒
	clock.microsec = 4'002'000;
	System::Update();
	REQUIRE(value == 3); // 3秒経過したので3になる
	REQUIRE(runner.done() == true); // ここで完了

	// 5秒
	clock.microsec = 5'000'000;
	System::Update();
	REQUIRE(value == 3); // すでに完了しているので何も起こらない
	REQUIRE(runner.done() == true);
}

TEST_CASE("Delay time not including pause time")
{
	TestClock clock;
	int32 value = 0;
	bool isPaused = false;

	const auto runner = DelayTimeTest(&value, &clock).pausedWhile([&isPaused] { return isPaused; }).runScoped();
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
	// Delay(3s)内時間: 0.000秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value == 2); // 1秒経過したので2になる
	REQUIRE(runner.done() == false);

	// 3.990秒
	// Delay(3s)内時間: 2.989秒
	clock.microsec = 3'990'000;
	System::Update();
	REQUIRE(value == 2); // まだ3秒経過していないので変化なし
	REQUIRE(runner.done() == false);

	// 4.001秒(ポーズ開始)
	// Delay(3s)内時間: 2.989秒
	clock.microsec = 4'001'000;
	isPaused = true;
	System::Update();
	REQUIRE(value == 2); // ポーズ中なので変化なし
	REQUIRE(runner.done() == false);

	// 5秒(ポーズ中)
	// Delay(3s)内時間: 2.989秒
	clock.microsec = 5'000'000;
	System::Update();
	REQUIRE(value == 2); // ポーズ中なので変化なし
	REQUIRE(runner.done() == false);

	// 5.990秒(ポーズ解除)
	// Delay(3s)内時間: 2.989秒
	clock.microsec = 5'990'000;
	isPaused = false;
	System::Update();
	REQUIRE(value == 2); // ポーズ解除直後のフレームでは時間は進まないので変化なし
	REQUIRE(runner.done() == false);

	// 6.000秒
	// Delay(3s)内時間: 2.999秒
	clock.microsec = 6'000'000;
	System::Update();
	REQUIRE(value == 2); // まだ3秒経過していないので変化なし
	REQUIRE(runner.done() == false);

	// 6.002秒
	// Delay(3s)内時間: 3.001秒
	clock.microsec = 6'002'000;
	System::Update();
	REQUIRE(value == 3); // 3秒経過したので3になる
	REQUIRE(runner.done() == true); // ここで完了

	// 7秒
	clock.microsec = 7'000'000;
	System::Update();
	REQUIRE(value == 3); // すでに完了しているので何も起こらない
	REQUIRE(runner.done() == true);
}

TEST_CASE("Task::delayed")
{
	TestClock clock;
	int32 value = 0;

	const auto runner = DelayTimeTest(&value, &clock).delayed(1s, &clock).runScoped();
	REQUIRE(value == 0); // 1秒遅れて実行されるため、何も起こらない

	// 0秒
	clock.microsec = 0;
	System::Update();
	REQUIRE(value == 0); // まだ1秒経過していないので変化なし
	REQUIRE(runner.done() == false);

	// 0.999秒
	clock.microsec = 999'000;
	System::Update();
	REQUIRE(value == 0); // まだ1秒経過していないので変化なし
	REQUIRE(runner.done() == false);

	// 1.001秒
	// Delay(1s)内時間: 0秒
	clock.microsec = 1'001'000;
	System::Update();
	REQUIRE(value == 1); // 1秒経過したので実行開始される
	REQUIRE(runner.done() == false);

	// 2.000秒
	// Delay(1s)内時間: 0.999秒
	clock.microsec = 2'000'000;
	System::Update();
	REQUIRE(value == 1); // まだ1秒経過していないので変化なし
	REQUIRE(runner.done() == false);

	// 2.002秒
	// Delay(1s)内時間: 1.001秒
	// Delay(3s)内時間: 0秒
	clock.microsec = 2'002'000;
	System::Update();
	REQUIRE(value == 2); // 1秒経過したので2になる
	REQUIRE(runner.done() == false);

	// 5.001秒
	// Delay(3s)内時間: 2.999秒
	clock.microsec = 5'001'000;
	System::Update();
	REQUIRE(value == 2); // まだ3秒経過していないので変化なし
	REQUIRE(runner.done() == false);

	// 5.003秒
	// Delay(3s)内時間: 3.001秒
	clock.microsec = 5'003'000;
	System::Update();
	REQUIRE(value == 3); // 3秒経過したので3になる
	REQUIRE(runner.done() == true); // ここで完了

	// 6秒
	clock.microsec = 6'000'000;
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

TEST_CASE("Finish callback with empty task")
{
	int32 finishCallbackCount = 0;
	int32 cancelCallbackCount = 0;
	auto runner = Co::EmptyTask().runScoped([&] { ++finishCallbackCount; }, [&] { ++cancelCallbackCount; });

	// 空のタスクは即座に完了扱いとなる
	REQUIRE(finishCallbackCount == 1);
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

TEST_CASE("co_return with pausedWhile")
{
	int32 value = 0;
	bool isPaused = true; // 初めからポーズにする

	auto task = CoReturnTestCaller(&value).pausedWhile([&isPaused] { return isPaused; });

	const auto runner = std::move(task).runScoped();
	REQUIRE(value == 0); // ポーズ中なのでrunScopedにより実行されない

	// ポーズ中なので変化しない 1フレーム目
	System::Update();
	REQUIRE(value == 0); // 変化なし

	// ポーズ中なので変化しない 2フレーム目
	System::Update();
	REQUIRE(value == 0); // 変化なし

	// ポーズ解除 3フレーム目
	isPaused = false;
	System::Update();
	REQUIRE(value == 42); // ポーズ解除されたので実行される
}

Co::Task<int32> CoReturnWithDelayTest()
{
	co_await Co::NextFrame();
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
	REQUIRE(value == 42); // NextFrame()の後が実行され、co_awaitで受け取った値が返る

	System::Update();
	REQUIRE(value == 42); // すでに完了しているので何も起こらない
}

TEST_CASE("co_return with delay and pausedWhile #1")
{
	int32 value = 0;
	bool isPaused = false;

	auto task = CoReturnWithDelayTestCaller(&value).pausedWhile([&isPaused] { return isPaused; });

	const auto runner = std::move(task).runScoped();
	REQUIRE(value == 1); // runScopedにより実行が開始される

	// ポーズ中なので変化しない 1フレーム目
	isPaused = true;
	System::Update();
	REQUIRE(value == 1); // 変化なし

	// ポーズ中なので変化しない 2フレーム目
	isPaused = true;
	System::Update();
	REQUIRE(value == 1); // 変化なし

	// ポーズ解除 3フレーム目
	// NextFrame()の後が実行され、co_awaitで受け取った値が返る
	isPaused = false;
	System::Update();
	REQUIRE(value == 42);

	// すでに完了しているので何も起こらない
	System::Update();
	REQUIRE(value == 42);
}

TEST_CASE("co_return with delay and pausedWhile #2")
{
	int32 value = 0;
	bool isPaused = true; // 初めからポーズにする

	auto task = CoReturnWithDelayTestCaller(&value).pausedWhile([&isPaused] { return isPaused; });

	const auto runner = std::move(task).runScoped();
	REQUIRE(value == 0); // ポーズ中なのでrunScopedにより実行されない

	// ポーズ中なので変化しない 1フレーム目
	System::Update();
	REQUIRE(value == 0); // 変化なし

	// ポーズ中なので変化しない 2フレーム目
	System::Update();
	REQUIRE(value == 0); // 変化なし

	// ポーズ解除 3フレーム目
	isPaused = false;
	System::Update();
	REQUIRE(value == 1); // ポーズ解除されたので実行が開始される

	// NextFrame()の後が実行され、co_awaitで受け取った値が返る
	System::Update();
	REQUIRE(value == 42);

	// すでに完了しているので何も起こらない
	System::Update();
	REQUIRE(value == 42);
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

Co::Task<void> CoReturnWithMoveOnlyTypeTestCallerWithPausedWhile(int32* pValue, std::function<bool()> fnIsPaused)
{
	auto ptr = co_await CoReturnWithMoveOnlyTypeTest().pausedWhile(std::move(fnIsPaused));
	*pValue = *ptr;
}

TEST_CASE("co_return with move-only type and pausedWhile")
{
	int32 value = 0;
	bool isPaused = true; // 初めからポーズにする

	auto task = CoReturnWithMoveOnlyTypeTestCallerWithPausedWhile(&value, [&isPaused] { return isPaused; });

	const auto runner = std::move(task).runScoped();
	REQUIRE(value == 0); // ポーズ中なのでrunScopedにより実行されない

	// ポーズ中なので変化しない 1フレーム目
	System::Update();
	REQUIRE(value == 0); // 変化なし

	// ポーズ中なので変化しない 2フレーム目
	System::Update();
	REQUIRE(value == 0); // 変化なし

	// ポーズ解除 3フレーム目
	isPaused = false;
	System::Update();
	REQUIRE(value == 42); // ポーズ解除されたので実行される
}

Co::Task<std::unique_ptr<int32>> CoReturnWithMoveOnlyTypeAndDelayTest()
{
	co_await Co::NextFrame();
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
	REQUIRE(value == 42); // NextFrame()の後が実行され、co_awaitで受け取った値が返る

	System::Update();
	REQUIRE(value == 42); // すでに完了しているので何も起こらない
}

Co::Task<void> CoReturnWithMoveOnlyTypeAndDelayTestCallerWithPausedWhile(int32* pValue, std::function<bool()> fnIsPaused)
{
	auto ptr = co_await CoReturnWithMoveOnlyTypeAndDelayTest().pausedWhile(std::move(fnIsPaused));
	*pValue = *ptr;
}

TEST_CASE("co_return with move-only type and delay and pausedWhile")
{
	int32 value = 0;
	bool isPaused = true; // 初めからポーズにする

	auto task = CoReturnWithMoveOnlyTypeAndDelayTestCallerWithPausedWhile(&value, [&isPaused] { return isPaused; });

	const auto runner = std::move(task).runScoped();
	REQUIRE(value == 0); // ポーズ中なのでrunScopedにより実行されない

	// ポーズ中なので変化しない 1フレーム目
	System::Update();
	REQUIRE(value == 0); // 変化なし

	// ポーズ中なので変化しない 2フレーム目
	System::Update();
	REQUIRE(value == 0); // 変化なし

	// ポーズ解除 3フレーム目
	isPaused = false;
	System::Update();
	REQUIRE(value == 0); // ポーズ解除されたので実行が開始され、NextFrame()が実行されるため変化なし

	// NextFrame()の後が実行され、co_awaitで受け取った値が返る
	System::Update();
	REQUIRE(value == 42);
}

Co::Task<int32> TaskAwaiterLifetimeTestInner()
{
	co_await Co::DelayFrame(1);
	co_return 100;
}

Co::Task<int32> TaskAwaiterLifetimeTestOuter()
{
	// TaskAwaiterオブジェクトのライフタイム管理をテスト
	int32 result = co_await TaskAwaiterLifetimeTestInner();
	co_return result + 1;
}

TEST_CASE("TaskAwaiter lifetime management")
{
	auto task = TaskAwaiterLifetimeTestOuter();
	int32 result = 0;
	const auto runner = std::move(task).runScoped([&](int32 r) { result = r; });
	
	// フレームを進めてタスクを完了させる
	System::Update();
	System::Update();
	
	REQUIRE(runner.done() == true);
	REQUIRE(result == 101);
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
	co_await Co::NextFrame();
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
	co_await Co::NextFrame();
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

Co::Task<int32> CatchExceptionFromNestedTaskTest()
{
	int32 result;
	try
	{
		result = co_await ThrowExceptionWithDelayAndNonVoidResultTest();
	}
	catch (const std::runtime_error& error)
	{
		if (error.what() == "test exception"sv)
		{
			result = -1;
		}
		else
		{
			result = -2;
		}
	}
	co_return result;
}

TEST_CASE("Catch exception from nested task")
{
	Optional<int32> result;

	auto task = CatchExceptionFromNestedTaskTest();
	REQUIRE(result == none); // タスク生成時点ではまだ実行されない

	const auto runner = std::move(task).runScoped([&](int32 r) { result = r; });
	REQUIRE(result == none); // まだ例外送出していない

	System::Update();
	REQUIRE(result == -1); // 例外をcatchし、resultに-1が入る
}

Co::Task<int32> ExceptionThrowingTaskTest(int32 id)
{
	throw std::runtime_error("Task " + std::to_string(id) + " exception");
	co_return id; // 到達しないが、戻り値型のため必要
}

TEST_CASE("Multiple exception handling")
{
	// 複数のタスクが例外を投げる状況をテスト
	bool exception1Caught = false;
	bool exception2Caught = false;
	bool exception3Caught = false;

	try
	{
		auto task1 = ExceptionThrowingTaskTest(1);
		const auto runner1 = std::move(task1).runScoped();
	}
	catch (const std::runtime_error& e)
	{
		exception1Caught = true;
		REQUIRE(std::string(e.what()).find("Task 1") != std::string::npos);
	}

	try
	{
		auto task2 = ExceptionThrowingTaskTest(2);
		const auto runner2 = std::move(task2).runScoped();
	}
	catch (const std::runtime_error& e)
	{
		exception2Caught = true;
		REQUIRE(std::string(e.what()).find("Task 2") != std::string::npos);
	}

	try
	{
		auto task3 = ExceptionThrowingTaskTest(3);
		const auto runner3 = std::move(task3).runScoped();
	}
	catch (const std::runtime_error& e)
	{
		exception3Caught = true;
		REQUIRE(std::string(e.what()).find("Task 3") != std::string::npos);
	}

	// 各例外が適切に捕捉されることを確認
	REQUIRE(exception1Caught == true);
	REQUIRE(exception2Caught == true);
	REQUIRE(exception3Caught == true);
}

TEST_CASE("TaskFinishSource<void>")
{
	Co::TaskFinishSource<void> taskFinishSource;
	REQUIRE(taskFinishSource.done() == false);

	// 初回のリクエストではtrueが返る
	REQUIRE(taskFinishSource.requestFinish() == true);
	REQUIRE(taskFinishSource.done() == true);

	// 2回目以降のリクエストは受け付けない
	REQUIRE(taskFinishSource.requestFinish() == false);
	REQUIRE(taskFinishSource.done() == true);
}

TEST_CASE("TaskFinishSource<void>::waitUntilDone")
{
	Co::TaskFinishSource<void> taskFinishSource;
	const auto runner = taskFinishSource.waitUntilDone().runScoped();

	// まだ完了していない
	REQUIRE(runner.done() == false);
	REQUIRE(taskFinishSource.done() == false);

	// 完了リクエスト
	taskFinishSource.requestFinish();

	// System::Updateが呼ばれるまでは完了しない
	REQUIRE(runner.done() == false);
	REQUIRE(taskFinishSource.done() == true);

	System::Update();

	// System::Updateが呼ばれたら完了する
	REQUIRE(runner.done() == true);
	REQUIRE(taskFinishSource.done() == true);
}

TEST_CASE("TaskFinishSource<int32>")
{
	Co::TaskFinishSource<int32> taskFinishSource;
	REQUIRE(taskFinishSource.done() == false);
	REQUIRE(taskFinishSource.hasResult() == false);

	// 結果が入るまではresultは例外を投げる
	REQUIRE_THROWS_AS(taskFinishSource.result(), Error);

	// 初回のリクエストではtrueが返る
	REQUIRE(taskFinishSource.requestFinish(42) == true);
	REQUIRE(taskFinishSource.done() == true);

	// 結果が取得できる
	REQUIRE(taskFinishSource.hasResult() == true);
	REQUIRE(taskFinishSource.result() == 42);

	// 2回目以降の結果取得はできない
	REQUIRE(taskFinishSource.hasResult() == false);
	REQUIRE_THROWS_AS(taskFinishSource.result(), Error);
	REQUIRE(taskFinishSource.done() == true);

	// 2回目以降のリクエストは受け付けない
	REQUIRE(taskFinishSource.requestFinish(4242) == false);
	REQUIRE(taskFinishSource.hasResult() == false);
	REQUIRE_THROWS_AS(taskFinishSource.result(), Error);
	REQUIRE(taskFinishSource.done() == true);
}

TEST_CASE("TaskFinishSource<int32>::waitUntilDone")
{
	Co::TaskFinishSource<int32> taskFinishSource;
	const auto runner = taskFinishSource.waitUntilDone().runScoped();

	// まだ完了していない
	REQUIRE(runner.done() == false);
	REQUIRE(taskFinishSource.done() == false);

	// 完了リクエスト
	taskFinishSource.requestFinish(42);

	// System::Updateが呼ばれるまでは完了しない
	REQUIRE(runner.done() == false);
	REQUIRE(taskFinishSource.done() == true);

	System::Update();

	// System::Updateが呼ばれたら完了する
	REQUIRE(runner.done() == true);
	REQUIRE(taskFinishSource.done() == true);
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
	REQUIRE(taskFinishSource.done() == false);
	REQUIRE(result == none);

	// 完了リクエスト
	taskFinishSource.requestFinish(42);

	// System::Updateが呼ばれるまでは完了しない
	REQUIRE(runner.done() == false);
	REQUIRE(taskFinishSource.done() == true);
	REQUIRE(result == none);

	System::Update();

	// System::Updateが呼ばれたら完了する
	REQUIRE(runner.done() == true);
	REQUIRE(taskFinishSource.done() == true);
	REQUIRE(result == 42);

	// 2回目以降の結果取得はできない
	// (waitForResult内で既に結果取得しているため2回目となる)
	REQUIRE_THROWS_AS(taskFinishSource.result(), Error);
	REQUIRE(taskFinishSource.done() == true);
}

TEST_CASE("ScopedTaskRunner::requestCancel")
{
	int32 finishCallbackCount = 0;
	int32 cancelCallbackCount = 0;

	auto runner = Co::DelayFrame(3).runScoped([&] { ++finishCallbackCount; }, [&] { ++cancelCallbackCount; });

	REQUIRE(runner.done() == false);

	// キャンセル
	const bool canceled = runner.requestCancel();
	REQUIRE(canceled == true);

	// 即時でキャンセルされる
	REQUIRE(runner.done() == true);
	REQUIRE(finishCallbackCount == 0);
	REQUIRE(cancelCallbackCount == 1);

	// 2回目以降のキャンセルは通らない
	const bool canceled2 = runner.requestCancel();
	REQUIRE(canceled2 == false);
	REQUIRE(finishCallbackCount == 0);
	REQUIRE(cancelCallbackCount == 1);
}

TEST_CASE("ScopedTaskRunner::requestCancel after finished")
{
	int32 finishCallbackCount = 0;
	int32 cancelCallbackCount = 0;

	auto runner = Co::DelayFrame(3).runScoped([&] { ++finishCallbackCount; }, [&] { ++cancelCallbackCount; });
	for (int32 i = 0; i < 3; ++i)
	{
		REQUIRE(runner.done() == false);
		System::Update();
	}

	// 完了済みである
	REQUIRE(runner.done() == true);
	REQUIRE(finishCallbackCount == 1);
	REQUIRE(cancelCallbackCount == 0);

	// 完了済みの場合はキャンセル要求は通らない
	const bool canceled = runner.requestCancel();
	REQUIRE(canceled == false);
	REQUIRE(finishCallbackCount == 1);
	REQUIRE(cancelCallbackCount == 0);
}

TEST_CASE("ScopedTaskRunner::waitUntilDone")
{
	const auto runner = Co::DelayFrame(3).runScoped();
	const auto runner2 = runner.waitUntilDone().runScoped();

	REQUIRE(runner.done() == false);
	REQUIRE(runner2.done() == false);

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(runner2.done() == false);

	System::Update();

	REQUIRE(runner.done() == false);
	REQUIRE(runner2.done() == false);

	System::Update();

	REQUIRE(runner.done() == true);
	REQUIRE(runner2.done() == true);
}

TEST_CASE("ScopedTaskRunner::waitUntilDone immediate")
{
	const auto runner = Co::DelayFrame(0).runScoped();
	const auto runner2 = runner.waitUntilDone().runScoped();

	REQUIRE(runner.done() == true);
	REQUIRE(runner2.done() == true);
}

TEST_CASE("ScopedTaskRunner::waitUntilDone canceled")
{
	Optional<Co::ScopedTaskRunner> runner = Co::DelayFrame(3).runScoped();
	const auto runner2 = runner->waitUntilDone().runScoped();

	REQUIRE(runner->done() == false);
	REQUIRE(runner2.done() == false);

	System::Update();

	// キャンセル
	runner = none;

	// キャンセルされてもSystem::Updateが呼ばれるまでは完了しない
	REQUIRE(runner2.done() == false);

	System::Update();

	// System::Updateが呼ばれたら完了する(doneとは、finishまたはcancelのことを指す)
	REQUIRE(runner2.done() == true);
}

TEST_CASE("ScopedTaskRunner move assignment")
{
	int32 runner1FinishCount = 0;
	int32 runner1CancelCount = 0;
	int32 runner2FinishCount = 0;
	int32 runner2CancelCount = 0;

	Co::ScopedTaskRunner runner = Co::DelayFrame(2).runScoped([&] { ++runner1FinishCount; }, [&] { ++runner1CancelCount; });

	REQUIRE(runner.done() == false);
	REQUIRE(runner1FinishCount == 0);
	REQUIRE(runner1CancelCount == 0);
	REQUIRE(runner2FinishCount == 0);
	REQUIRE(runner2CancelCount == 0);

	// 別のタスク実行をムーブ代入
	runner = Co::DelayFrame(1).runScoped([&] { ++runner2FinishCount; }, [&] { ++runner2CancelCount; });

	// 1個目のタスクはキャンセルされる
	REQUIRE(runner.done() == false);
	REQUIRE(runner1FinishCount == 0);
	REQUIRE(runner1CancelCount == 1);
	REQUIRE(runner2FinishCount == 0);
	REQUIRE(runner2CancelCount == 0);

	System::Update();

	// 2個目のタスクが完了
	REQUIRE(runner.done() == true);
	REQUIRE(runner1FinishCount == 0);
	REQUIRE(runner1CancelCount == 1);
	REQUIRE(runner2FinishCount == 1);
	REQUIRE(runner2CancelCount == 0);

	System::Update();

	// すでに完了しているので何も起こらない
	REQUIRE(runner.done() == true);
	REQUIRE(runner1FinishCount == 0);
	REQUIRE(runner1CancelCount == 1);
	REQUIRE(runner2FinishCount == 1);
	REQUIRE(runner2CancelCount == 0);
}

Co::Task<> ManyRunnersTest(int32 i, Array<int32>* pValues)
{
	co_await Co::NextFrame();
	pValues->push_back(i);
}

TEST_CASE("ScopedTaskRunner many runners")
{
	Array<int32> values;
	Array<Co::ScopedTaskRunner> runners;
	for (int32 i = 0; i < 10000; ++i)
	{
		runners.push_back(ManyRunnersTest(i, &values).runScoped());
	}
	REQUIRE(values.empty() == true);
	System::Update();
	REQUIRE(values.size() == 10000);
	REQUIRE(values[0] == 0);
	REQUIRE(values[9999] == 9999);
	runners.clear();
	values.clear();

	for (int32 i = 0; i < 10000; ++i)
	{
		runners.push_back(ManyRunnersTest(10000 + i, &values).runScoped());
	}
	System::Update();
	REQUIRE(values.size() == 10000);
	REQUIRE(values[0] == 10000);
	REQUIRE(values[9999] == 19999);

	// すでに完了しているので何も起こらない
	System::Update();
	REQUIRE(values.size() == 10000);
	REQUIRE(values[0] == 10000);
	REQUIRE(values[9999] == 19999);
}

TEST_CASE("ScopedTaskRunner::forget")
{
	// forgetを呼んでもタスクは実行され続けることを確認
	int32 value = 0;
	bool finished = false;
	
	auto task = [](int32* pValue, bool* pFinished) -> Co::Task<void>
	{
		*pValue = 1;
		co_await Co::DelayFrame(2);
		*pValue = 2;
		co_await Co::DelayFrame(2);
		*pValue = 3;
		*pFinished = true;
	};
	
	auto runner = task(&value, &finished).runScoped();
	
	// 初期状態
	REQUIRE(value == 1);
	REQUIRE(finished == false);
	REQUIRE(runner.done() == false);
	
	// forgetを呼ぶ
	runner.forget();
	
	// forgetを呼んだ後、done()は即座にtrueを返す
	REQUIRE(runner.done() == true);
	
	// しかしタスクは実行され続ける
	System::Update();
	System::Update();
	REQUIRE(value == 2);
	REQUIRE(finished == false);
	
	System::Update();
	System::Update();
	REQUIRE(value == 3);
	REQUIRE(finished == true);
	
	// 後のテストケースに影響しないよう、全フレーム消化
	System::Update();
}

Co::Task<> ScopedDrawerTest(int32* pValue)
{
	Co::ScopedDrawer drawer{ [&] { ++(*pValue); } };
	co_await Co::DelayFrame(3);
}

TEST_CASE("ScopedDrawer")
{
	int32 value = 0;
	const auto runner = ScopedDrawerTest(&value).runScoped();

	REQUIRE(value == 0);
	System::Update();
	REQUIRE(value == 1);
	System::Update();
	REQUIRE(value == 2);
	System::Update();
	REQUIRE(value == 3);
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(value == 3); // すでに完了しているので何も起こらない
}

Co::Task<> ScopedDrawerTestWithFunc(std::function<void()> func)
{
	Co::ScopedDrawer drawer{ std::move(func) };
	co_await Co::DelayFrame(3);
}

TEST_CASE("ScopedDrawer same layer/drawIndex order")
{
	Array<String> array;
	const auto runner1 = ScopedDrawerTestWithFunc([&] { array.push_back(U"1"); }).runScoped();
	const auto runner2 = ScopedDrawerTestWithFunc([&] { array.push_back(U"2"); }).runScoped();
	const auto runner3 = ScopedDrawerTestWithFunc([&] { array.push_back(U"3"); }).runScoped();

	REQUIRE(array.empty() == true);
	System::Update();
	REQUIRE(array == Array<String>{ U"1", U"2", U"3" });
	System::Update();
	REQUIRE(array == Array<String>{ U"1", U"2", U"3", U"1", U"2", U"3" });
	System::Update();
	REQUIRE(array == Array<String>{ U"1", U"2", U"3", U"1", U"2", U"3", U"1", U"2", U"3" });
	System::Update();
	REQUIRE(runner1.done() == true);
	REQUIRE(runner2.done() == true);
	REQUIRE(runner3.done() == true);
	REQUIRE(array == Array<String>{ U"1", U"2", U"3", U"1", U"2", U"3", U"1", U"2", U"3" });
}

Co::Task<> ScopedDrawerTestWithFuncAndDrawIndex(std::function<void()> func, int32 drawIndex)
{
	Co::ScopedDrawer drawer{ std::move(func), Co::Layer::Default, drawIndex };
	co_await Co::DelayFrame(3);
}

TEST_CASE("ScopedDrawer drawIndex order")
{
	Array<String> array;
	const auto runner1 = ScopedDrawerTestWithFunc([&] { array.push_back(U"1"); }).runScoped();
	const auto runner2 = ScopedDrawerTestWithFuncAndDrawIndex([&] { array.push_back(U"2"); }, Co::DrawIndex::Front).runScoped();
	const auto runner3 = ScopedDrawerTestWithFuncAndDrawIndex([&] { array.push_back(U"3"); }, Co::DrawIndex::Back).runScoped();
	REQUIRE(array.empty() == true);
	System::Update();
	REQUIRE(array == Array<String>{ U"3", U"1", U"2" });
	System::Update();
	REQUIRE(array == Array<String>{ U"3", U"1", U"2", U"3", U"1", U"2" });
	System::Update();
	REQUIRE(array == Array<String>{ U"3", U"1", U"2", U"3", U"1", U"2", U"3", U"1", U"2" });
	System::Update();
	REQUIRE(runner1.done() == true);
	REQUIRE(runner2.done() == true);
	REQUIRE(runner3.done() == true);
	REQUIRE(array == Array<String>{ U"3", U"1", U"2", U"3", U"1", U"2", U"3", U"1", U"2" });
}

Co::Task<> ScopedDrawerTestWithFuncAndLayerAndDrawIndex(std::function<void()> func, Co::Layer layer, int32 drawIndex)
{
	Co::ScopedDrawer drawer{ std::move(func), layer, drawIndex };
	co_await Co::DelayFrame(3);
}

TEST_CASE("ScopedDrawer layer order")
{
	Array<String> array;
	const auto runner1 = ScopedDrawerTestWithFunc([&] { array.push_back(U"1"); }).runScoped();
	const auto runner2 = ScopedDrawerTestWithFuncAndLayerAndDrawIndex([&] { array.push_back(U"2"); }, Co::Layer::Modal, Co::DrawIndex::Front).runScoped();
	const auto runner3 = ScopedDrawerTestWithFuncAndLayerAndDrawIndex([&] { array.push_back(U"3"); }, Co::Layer::Modal, Co::DrawIndex::Back).runScoped();
	const auto runner4 = ScopedDrawerTestWithFuncAndLayerAndDrawIndex([&] { array.push_back(U"4"); }, Co::Layer::Default, Co::DrawIndex::Default).runScoped();
	const auto runner5 = ScopedDrawerTestWithFuncAndLayerAndDrawIndex([&] { array.push_back(U"5"); }, Co::Layer::Default, Co::DrawIndex::Back).runScoped();
	REQUIRE(array.empty() == true);
	System::Update();
	REQUIRE(array == Array<String>{ U"5", U"1", U"4", U"3", U"2" });
	System::Update();
	REQUIRE(array == Array<String>{ U"5", U"1", U"4", U"3", U"2", U"5", U"1", U"4", U"3", U"2" });
	System::Update();
	REQUIRE(array == Array<String>{ U"5", U"1", U"4", U"3", U"2", U"5", U"1", U"4", U"3", U"2", U"5", U"1", U"4", U"3", U"2" });
	System::Update();
	REQUIRE(runner1.done() == true);
	REQUIRE(runner2.done() == true);
	REQUIRE(runner3.done() == true);
	REQUIRE(runner4.done() == true);
	REQUIRE(runner5.done() == true);
	REQUIRE(array == Array<String>{ U"5", U"1", U"4", U"3", U"2", U"5", U"1", U"4", U"3", U"2", U"5", U"1", U"4", U"3", U"2" });
}

Co::Task<> ScopedDrawerTestWithFuncChangingDrawIndex(std::function<void()> func, Co::Layer layer, int32 drawIndex, int32 changeFrame, int32 newDrawIndex)
{
	Co::ScopedDrawer drawer{ std::move(func), layer, drawIndex };
	for (int32 i = 0; i < 3; ++i)
	{
		if (i == changeFrame)
		{
			drawer.setDrawIndex(newDrawIndex);
		}
		co_await Co::NextFrame();
	}
}

TEST_CASE("ScopedDrawer changing drawIndex")
{
	Array<String> array;
	const auto runner1 = ScopedDrawerTestWithFunc([&] { array.push_back(U"1"); }).runScoped();
	const auto runner2 = ScopedDrawerTestWithFuncChangingDrawIndex([&] { array.push_back(U"2"); }, Co::Layer::Modal, Co::DrawIndex::Front, 2, Co::DrawIndex::Back).runScoped();
	const auto runner3 = ScopedDrawerTestWithFuncChangingDrawIndex([&] { array.push_back(U"3"); }, Co::Layer::Modal, Co::DrawIndex::Front, 1, Co::DrawIndex::Back).runScoped();
	REQUIRE(array.empty() == true);
	System::Update();
	REQUIRE(array == Array<String>{ U"1", U"2", U"3" });
	System::Update();
	REQUIRE(array == Array<String>{ U"1", U"2", U"3", U"1", U"3", U"2" });
	System::Update();
	// drawIndexが同一の場合の順序はdrawIndex変更タイミングではなく実行開始タイミングで決まるため、最後は1,2,3の順番になる
	REQUIRE(array == Array<String>{ U"1", U"2", U"3", U"1", U"3", U"2", U"1", U"2", U"3" });
}

Co::Task<> ScopedDrawerTestWithFuncChangingLayer(std::function<void()> func, Co::Layer layer, int32 drawIndex, int32 changeFrame, Co::Layer newLayer)
{
	Co::ScopedDrawer drawer{ std::move(func), layer, drawIndex };
	for (int32 i = 0; i < 3; ++i)
	{
		if (i == changeFrame)
		{
			drawer.setLayer(newLayer);
		}
		co_await Co::NextFrame();
	}
}

TEST_CASE("ScopedDrawer changing layer")
{
	Array<String> array;
	const auto runner1 = ScopedDrawerTestWithFunc([&] { array.push_back(U"1"); }).runScoped();
	const auto runner2 = ScopedDrawerTestWithFuncChangingLayer([&] { array.push_back(U"2"); }, Co::Layer::Modal, Co::DrawIndex::Default, 2, Co::Layer::Default).runScoped();
	const auto runner3 = ScopedDrawerTestWithFuncChangingLayer([&] { array.push_back(U"3"); }, Co::Layer::Modal, Co::DrawIndex::Default, 1, Co::Layer::Default).runScoped();
	REQUIRE(array.empty() == true);
	System::Update();
	REQUIRE(array == Array<String>{ U"1", U"2", U"3" });
	System::Update();
	REQUIRE(array == Array<String>{ U"1", U"2", U"3", U"1", U"3", U"2" });
	System::Update();
	// layerが同一の場合の順序はlayer変更タイミングではなく実行開始タイミングで決まるため、最後は1,2,3の順番になる
	REQUIRE(array == Array<String>{ U"1", U"2", U"3", U"1", U"3", U"2", U"1", U"2", U"3" });
}

TEST_CASE("ScopedDrawer many drawers")
{
	Array<int32> values;
	Array<std::unique_ptr<Co::ScopedDrawer>> drawers;
	for (int32 i = 0; i < 10000; ++i)
	{
		drawers.push_back(std::make_unique<Co::ScopedDrawer>([i, &values] { values.push_back(i); }));
	}
	REQUIRE(values.empty() == true);
	System::Update();
	REQUIRE(values.size() == 10000);
	REQUIRE(values[0] == 0);
	REQUIRE(values[9999] == 9999);
	drawers.clear();
	values.clear();

	for (int32 i = 0; i < 10000; ++i)
	{
		drawers.push_back(std::make_unique<Co::ScopedDrawer>([i, &values] { values.push_back(10000 + i); }));
	}
	System::Update();
	REQUIRE(values.size() == 10000);
	REQUIRE(values[0] == 10000);
	REQUIRE(values[9999] == 19999);
	drawers.clear();

	// drawerは既に存在しないので何も起こらない
	System::Update();
	REQUIRE(values.size() == 10000);
	REQUIRE(values[0] == 10000);
	REQUIRE(values[9999] == 19999);
}

TEST_CASE("MultiRunner finish")
{
	Co::MultiRunner mr;
	int32 runner1FinishCount = 0;
	int32 runner1CancelCount = 0;
	int32 runner2FinishCount = 0;
	int32 runner2CancelCount = 0;

	Co::DelayFrame(1).runAddTo(mr, [&] { ++runner1FinishCount; }, [&] { ++runner1CancelCount; });
	Co::DelayFrame(2).runAddTo(mr, [&] { ++runner2FinishCount; }, [&] { ++runner2CancelCount; });

	// まだ完了していない
	REQUIRE(runner1FinishCount == 0);
	REQUIRE(runner1CancelCount == 0);
	REQUIRE(runner2FinishCount == 0);
	REQUIRE(runner2CancelCount == 0);
	REQUIRE(mr.allDone() == false);
	REQUIRE(mr.anyDone() == false);

	System::Update();

	// 1個目のタスクが完了
	REQUIRE(runner1FinishCount == 1);
	REQUIRE(runner1CancelCount == 0);
	REQUIRE(runner2FinishCount == 0);
	REQUIRE(runner2CancelCount == 0);
	REQUIRE(mr.allDone() == false);
	REQUIRE(mr.anyDone() == true);

	System::Update();

	// 2個目のタスクが完了
	REQUIRE(runner1FinishCount == 1);
	REQUIRE(runner1CancelCount == 0);
	REQUIRE(runner2FinishCount == 1);
	REQUIRE(runner2CancelCount == 0);
	REQUIRE(mr.allDone() == true);
	REQUIRE(mr.anyDone() == true);
}

TEST_CASE("MultiRunner removeDone")
{
	Co::MultiRunner mr;
	Co::DelayFrame(1).runAddTo(mr);
	Co::DelayFrame(2).runAddTo(mr);

	REQUIRE(mr.size() == 2);

	// 完了していないのでremoveDoneを呼んでも削除されない
	mr.removeDone();
	REQUIRE(mr.size() == 2);

	System::Update();

	// 1個目のタスクが完了
	REQUIRE(mr.size() == 2);
	mr.removeDone();
	REQUIRE(mr.size() == 1);

	System::Update();

	// 2個目のタスクが完了
	REQUIRE(mr.size() == 1);
	mr.removeDone();
	REQUIRE(mr.size() == 0);

	// すでに空なので何も起こらない
	mr.removeDone();
	REQUIRE(mr.size() == 0);
}

TEST_CASE("MultiRunner::requestCancelAll")
{
	Co::MultiRunner mr;
	int32 runner1FinishCount = 0;
	int32 runner1CancelCount = 0;
	int32 runner2FinishCount = 0;
	int32 runner2CancelCount = 0;
	int32 runner3FinishCount = 0;
	int32 runner3CancelCount = 0;

	Co::DelayFrame(1).runAddTo(mr, [&] { ++runner1FinishCount; }, [&] { ++runner1CancelCount; });
	Co::DelayFrame(2).runAddTo(mr, [&] { ++runner2FinishCount; }, [&] { ++runner2CancelCount; });
	Co::DelayFrame(3).runAddTo(mr, [&] { ++runner3FinishCount; }, [&] { ++runner3CancelCount; });

	// まだ完了していない
	REQUIRE(runner1FinishCount == 0);
	REQUIRE(runner1CancelCount == 0);
	REQUIRE(runner2FinishCount == 0);
	REQUIRE(runner2CancelCount == 0);
	REQUIRE(runner3FinishCount == 0);
	REQUIRE(runner3CancelCount == 0);
	REQUIRE(mr.allDone() == false);
	REQUIRE(mr.anyDone() == false);

	System::Update();

	// 1個目のタスクが完了
	REQUIRE(runner1FinishCount == 1);
	REQUIRE(runner1CancelCount == 0);
	REQUIRE(runner2FinishCount == 0);
	REQUIRE(runner2CancelCount == 0);
	REQUIRE(runner3FinishCount == 0);
	REQUIRE(runner3CancelCount == 0);
	REQUIRE(mr.allDone() == false);
	REQUIRE(mr.anyDone() == true);

	// キャンセル
	const bool canceled = mr.requestCancelAll();
	REQUIRE(canceled == true);

	// キャンセルされたので2個目のタスクは完了しない
	REQUIRE(runner1FinishCount == 1);
	REQUIRE(runner1CancelCount == 0);
	REQUIRE(runner2FinishCount == 0);
	REQUIRE(runner2CancelCount == 1);
	REQUIRE(runner3FinishCount == 0);
	REQUIRE(runner3CancelCount == 1);
	REQUIRE(mr.allDone() == true);
	REQUIRE(mr.anyDone() == true);

	// 2回目以降のキャンセルは通らない
	const bool canceled2 = mr.requestCancelAll();
	REQUIRE(canceled2 == false);
	REQUIRE(runner1FinishCount == 1);
	REQUIRE(runner1CancelCount == 0);
	REQUIRE(runner2FinishCount == 0);
	REQUIRE(runner2CancelCount == 1);
	REQUIRE(runner3FinishCount == 0);
	REQUIRE(runner3CancelCount == 1);
}

TEST_CASE("MultiRunner::waitUntilAllDone")
{
	Co::MultiRunner mr;
	Co::DelayFrame(3).runAddTo(mr);
	Co::DelayFrame(1).runAddTo(mr);
	Co::DelayFrame(2).runAddTo(mr);

	const auto runner = mr.waitUntilAllDone().runScoped();

	// まだ完了していない
	REQUIRE(runner.done() == false);

	System::Update();

	// 1個のタスクが完了
	REQUIRE(runner.done() == false);

	System::Update();

	// 2個のタスクが完了
	REQUIRE(runner.done() == false);

	System::Update();

	// 3個のタスクが完了
	REQUIRE(runner.done() == true);
}

TEST_CASE("MultiRunner::waitUntilAllDone immediate")
{
	Co::MultiRunner mr;
	Co::DelayFrame(0).runAddTo(mr);
	Co::DelayFrame(0).runAddTo(mr);
	Co::DelayFrame(0).runAddTo(mr);

	const auto runner = mr.waitUntilAllDone().runScoped();

	REQUIRE(runner.done() == true);
}

TEST_CASE("MultiRunner::waitUntilAllDone empty")
{
	Co::MultiRunner mr;

	const auto runner = mr.waitUntilAllDone().runScoped();

	REQUIRE(runner.done() == true);
}

TEST_CASE("MultiRunner::waitUntilAllDone added while running")
{
	Co::MultiRunner mr;
	Co::DelayFrame(1).runAddTo(mr);

	const auto runner = mr.waitUntilAllDone().runScoped();
	REQUIRE(runner.done() == false);

	// タスクを後から追加
	Co::DelayFrame(2).runAddTo(mr);

	System::Update();

	// 後から追加されたタスクが完了していないのでまだ完了しない
	REQUIRE(runner.done() == false);

	System::Update();

	// 2個のタスクが完了
	REQUIRE(mr.allDone() == true);

	// ただし、後から追加されたタスクはwaitUntilAllDoneより実行順が後ろなので、waitUntilAllDoneはまだ完了しない
	REQUIRE(runner.done() == false);

	System::Update();

	// ここでwaitUntilAllDoneが完了
	REQUIRE(runner.done() == true);
}

TEST_CASE("MultiRunner::waitUntilAnyDone")
{
	Co::MultiRunner mr;
	Co::DelayFrame(3).runAddTo(mr);
	Co::DelayFrame(1).runAddTo(mr);
	Co::DelayFrame(2).runAddTo(mr);

	const auto runner = mr.waitUntilAnyDone().runScoped();

	// まだ完了していない
	REQUIRE(runner.done() == false);

	System::Update();

	// 1個のタスクが完了
	REQUIRE(runner.done() == true);
}

TEST_CASE("MultiRunner::waitUntilAnyDone immediate")
{
	Co::MultiRunner mr;
	Co::DelayFrame(0).runAddTo(mr);
	Co::DelayFrame(0).runAddTo(mr);
	Co::DelayFrame(0).runAddTo(mr);

	const auto runner = mr.waitUntilAnyDone().runScoped();

	REQUIRE(runner.done() == true);
}

TEST_CASE("MultiRunner::waitUntilAnyDone empty")
{
	Co::MultiRunner mr;

	const auto runner = mr.waitUntilAnyDone().runScoped();

	// 10フレーム待っても完了しない
	for (int32 i = 0; i < 10; ++i)
	{
		System::Update();
		REQUIRE(runner.done() == false);
	}
}

TEST_CASE("MultiRunner::waitUntilAnyDone added while running")
{
	Co::MultiRunner mr;
	Co::DelayFrame(3).runAddTo(mr);

	const auto runner = mr.waitUntilAnyDone().runScoped();
	REQUIRE(runner.done() == false);

	// タスクを後から追加
	Co::DelayFrame(1).runAddTo(mr);

	System::Update();

	// 後から追加されたタスクが完了
	REQUIRE(mr.anyDone() == true);

	// ただし、後から追加されたタスクはwaitUntilAnyDoneより実行順が後ろなので、waitUntilAnyDoneはまだ完了しない
	REQUIRE(runner.done() == false);

	System::Update();

	// ここでwaitUntilAnyDoneが完了
	REQUIRE(runner.done() == true);
}

TEST_CASE("MultiRunner many runners")
{
	Co::MultiRunner mr;
	Array<int32> values;
	for (int32 i = 0; i < 10000; ++i)
	{
		ManyRunnersTest(i, &values).runAddTo(mr);
	}
	REQUIRE(values.empty() == true);
	System::Update();
	REQUIRE(values.size() == 10000);
	REQUIRE(values[0] == 0);
	REQUIRE(values[9999] == 9999);
	values.clear();
	mr.clear();

	for (int32 i = 0; i < 10000; ++i)
	{
		ManyRunnersTest(10000 + i, &values).runAddTo(mr);
	}
	System::Update();
	REQUIRE(values.size() == 10000);
	REQUIRE(values[0] == 10000);
	REQUIRE(values[9999] == 19999);

	// すでに完了しているので何も起こらない
	System::Update();
	REQUIRE(values.size() == 10000);
	REQUIRE(values[0] == 10000);
	REQUIRE(values[9999] == 19999);
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

TEST_CASE("WaitForResult with std::optional")
{
	std::optional<int32> result;
	int32 ret = 0;

	const auto runner = Co::WaitForResult(&result).runScoped([&](int32 r) { ret = r; });
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
	auto task = Co::WaitForResult(&result);
	REQUIRE(task.done() == false);

	// 既に結果が代入されているのでrunScopedで開始すると即座に完了する
	const auto runner = std::move(task).runScoped([&](int32 r) { ret = r; });
	REQUIRE(runner.done() == true);
	REQUIRE(ret == 42);

	// Updateを呼んでも何も起こらない
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(ret == 42);
}

TEST_CASE("WaitForResult with Optional")
{
	Optional<int32> result;
	int32 ret = 0;

	const auto runner = Co::WaitForResult(&result).runScoped([&](int32 r) { ret = r; });
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
	auto task = Co::WaitForResult(&result);
	REQUIRE(task.done() == false);

	// 既に結果が代入されているのでrunScopedで開始すると即座に完了する
	const auto runner = std::move(task).runScoped([&](int32 r) { ret = r; });
	REQUIRE(runner.done() == true);
	REQUIRE(ret == 42);

	// Updateを呼んでも何も起こらない
	System::Update();
	REQUIRE(runner.done() == true);
	REQUIRE(ret == 42);
}

TEST_CASE("WaitUntilHasValue with std::optional")
{
	std::optional<int32> result;

	const auto runner = Co::WaitUntilHasValue(&result).runScoped();
	REQUIRE(runner.done() == false);

	// 結果が代入されるまで完了しない
	System::Update();
	REQUIRE(runner.done() == false);

	result = 42;

	// 結果が代入されてもUpdateが呼ばれるまでは完了しない
	REQUIRE(runner.done() == false);

	// 結果が代入された後の初回のUpdateで完了する
	System::Update();
	REQUIRE(runner.done() == true);
}

TEST_CASE("Finish WaitUntilHasValue with std::optional immediately")
{
	std::optional<int32> result = 42;

	// 既に結果が代入されているが、タスク生成時点ではまだ実行されない
	auto task = Co::WaitUntilHasValue(&result);
	REQUIRE(task.done() == false);

	// 既に結果が代入されているのでrunScopedで開始すると即座に完了する
	const auto runner = std::move(task).runScoped();
	REQUIRE(runner.done() == true);
}

TEST_CASE("WaitUntilHasValue with Optional")
{
	Optional<int32> result;

	const auto runner = Co::WaitUntilHasValue(&result).runScoped();
	REQUIRE(runner.done() == false);

	// 結果が代入されるまで完了しない
	System::Update();
	REQUIRE(runner.done() == false);

	result = 42;

	// 結果が代入されてもUpdateが呼ばれるまでは完了しない
	REQUIRE(runner.done() == false);

	// 結果が代入された後の初回のUpdateで完了する
	System::Update();
	REQUIRE(runner.done() == true);
}

TEST_CASE("Finish WaitUntilHasValue with Optional immediately")
{
	Optional<int32> result = 42;

	// 既に結果が代入されているが、タスク生成時点ではまだ実行されない
	auto task = Co::WaitUntilHasValue(&result);
	REQUIRE(task.done() == false);

	// 既に結果が代入されているのでrunScopedで開始すると即座に完了する
	const auto runner = std::move(task).runScoped();
	REQUIRE(runner.done() == true);
}

TEST_CASE("WaitUntilValueChanged")
{
	int32 value = 0;
	const auto runner = Co::WaitUntilValueChanged(&value).runScoped();
	REQUIRE(runner.done() == false);

	// 値が変わるまでは完了しない
	System::Update();
	REQUIRE(runner.done() == false);

	// 値が変わってもUpdateが呼ばれるまでは完了しない
	value = 42;
	REQUIRE(runner.done() == false);

	// 値が変わった後の初回のUpdateで完了する
	System::Update();
	REQUIRE(runner.done() == true);
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
	co_await Co::NextFrame();
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
		Co::DelayFrame(1));

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
		Co::DelayFrame(1),
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

TEST_CASE("UpdaterTask without TaskFinishSource argument")
{
	int32 count = 0;
	auto task = Co::UpdaterTask([&] { ++count; });

	// タスク生成時点ではまだ実行されない
	REQUIRE(count == 0);

	// runScopedで開始すると最初のsuspendまで実行される
	const auto runner = std::move(task).runScoped();
	REQUIRE(count == 1);
	REQUIRE(runner.done() == false);
	
	System::Update();
	REQUIRE(count == 2);
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(count == 3);
	REQUIRE(runner.done() == false);
}

TEST_CASE("UpdaterTask with TaskFinishSource argument")
{
	int32 count = 0;
	auto task = Co::UpdaterTask<void>(
		[&](Co::TaskFinishSource<void>& taskFinishSource)
		{
			if (count == 3)
			{
				taskFinishSource.requestFinish();
				return;
			}
			++count;
		});

	// タスク生成時点ではまだ実行されない
	REQUIRE(count == 0);

	// runScopedで開始すると最初のsuspendまで実行される
	const auto runner = std::move(task).runScoped();
	REQUIRE(count == 1);
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(count == 2);
	REQUIRE(runner.done() == false);

	System::Update();
	REQUIRE(count == 3);
	REQUIRE(runner.done() == false);

	// requestFinishが呼ばれたら完了する
	System::Update();
	REQUIRE(count == 3);
	REQUIRE(runner.done() == true);
}

TEST_CASE("UpdaterTask with TaskFinishSource argument that has result")
{
	int32 count = 0;
	auto task = Co::UpdaterTask<int32>(
		[&](Co::TaskFinishSource<int32>& taskFinishSource)
		{
			if (count == 3)
			{
				taskFinishSource.requestFinish(42);
				return;
			}
			++count;
		});

	// タスク生成時点ではまだ実行されない
	REQUIRE(count == 0);

	int32 result = 0;

	// runScopedで開始すると最初のsuspendまで実行される
	const auto runner = std::move(task).runScoped([&](int32 r) { result = r; });
	REQUIRE(count == 1);
	REQUIRE(runner.done() == false);
	REQUIRE(result == 0);

	System::Update();
	REQUIRE(count == 2);
	REQUIRE(runner.done() == false);
	REQUIRE(result == 0);

	System::Update();
	REQUIRE(count == 3);
	REQUIRE(runner.done() == false);
	REQUIRE(result == 0);

	// requestFinishが呼ばれたら完了する
	System::Update();
	REQUIRE(count == 3);
	REQUIRE(runner.done() == true);
	REQUIRE(result == 42);
}

TEST_CASE("UpdaterTask with TaskFinishSource argument that has immediate result")
{
	int32 count = 0;
	auto task = Co::UpdaterTask<int32>(
		[&](Co::TaskFinishSource<int32>& taskFinishSource)
		{
			++count;
			taskFinishSource.requestFinish(42);
		});

	// タスク生成時点ではまだ実行されない
	REQUIRE(count == 0);

	int32 result = 0;

	// runScopedで開始すると最初のsuspendまで実行されて即座に完了する
	const auto runner = std::move(task).runScoped([&](int32 r) { result = r; });
	REQUIRE(count == 1);
	REQUIRE(runner.done() == true);
	REQUIRE(result == 42);
}

TEST_CASE("UpdaterTask with TaskFinishSource argument that has move-only result")
{
	int32 count = 0;
	auto task = Co::UpdaterTask<std::unique_ptr<int32>>(
		[&](Co::TaskFinishSource<std::unique_ptr<int32>>& taskFinishSource)
		{
			if (count == 3)
			{
				taskFinishSource.requestFinish(std::make_unique<int32>(42));
				return;
			}
			++count;
		});

	// タスク生成時点ではまだ実行されない
	REQUIRE(count == 0);

	std::unique_ptr<int32> result;

	// runScopedで開始すると最初のsuspendまで実行される
	const auto runner = std::move(task).runScoped([&](std::unique_ptr<int32> r) { result = std::move(r); });
	REQUIRE(count == 1);
	REQUIRE(runner.done() == false);
	REQUIRE(result == nullptr);

	System::Update();
	REQUIRE(count == 2);
	REQUIRE(runner.done() == false);
	REQUIRE(result == nullptr);

	System::Update();
	REQUIRE(count == 3);
	REQUIRE(runner.done() == false);
	REQUIRE(result == nullptr);

	// requestFinishが呼ばれたら完了する
	System::Update();
	REQUIRE(count == 3);
	REQUIRE(runner.done() == true);
	REQUIRE(result != nullptr);
	REQUIRE(*result == 42);
}

TEST_CASE("TaskFinishSource move-only result multiple access")
{
	Co::TaskFinishSource<std::unique_ptr<int32>> source;
	std::unique_ptr<int32> result1;
	
	// TaskFinishSourceに値を設定
	source.requestFinish(std::make_unique<int32>(200));
	
	// 1回目のresult()呼び出し
	REQUIRE(source.hasResult() == true);
	result1 = source.result();
	REQUIRE(result1 != nullptr);
	REQUIRE(*result1 == 200);
	
	// result()を2回目以降呼び出した場合、値が既にムーブ済みなので例外を送出する
	REQUIRE_THROWS_AS(source.result(), Error);
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
		co_await Co::NextFrame();
		m_pProgress->isPreStartFinished = true;
	}

	Co::Task<void> fadeIn() override
	{
		m_pProgress->isFadeInStarted = true;
		co_await Co::NextFrame();
		m_pProgress->isFadeInFinished = true;
	}

	Co::Task<int32> start() override
	{
		m_pProgress->isStartStarted = true;
		co_await Co::NextFrame();
		m_pProgress->isStartFinished = true;
		co_return m_argValue;
	}

	Co::Task<void> fadeOut() override
	{
		m_pProgress->isFadeOutStarted = true;
		co_await Co::NextFrame();
		m_pProgress->isFadeOutFinished = true;
	}

	Co::Task<void> postFadeOut() override
	{
		m_pProgress->isPostFadeOutStarted = true;
		co_await Co::NextFrame();
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

class TestSequenceWithLayer : public Co::SequenceBase<int32>
{
public:
	// LayerとdrawIndexを指定
	TestSequenceWithLayer()
		: Co::SequenceBase<int32>(Co::Layer::Modal, 100)
	{
	}

private:
	Co::Task<int32> start() override
	{
		// LayerとdrawIndexが正しく設定されていることを確認
		REQUIRE(layer() == Co::Layer::Modal);
		REQUIRE(drawIndex() == 100);
		co_return 42;
	}

	void draw() const override
	{
		// drawでも確認
		REQUIRE(layer() == Co::Layer::Modal);
		REQUIRE(drawIndex() == 100);
	}
};

TEST_CASE("Sequence with custom layer and drawIndex")
{
	// SequenceBaseがレイヤーと描画インデックスを正しく受け取れることを確認
	TestSequenceWithLayer sequence;
	const auto runner = sequence.playScoped();
	REQUIRE(runner.done() == true);
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
		co_await Co::NextFrame();
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
		co_await Co::NextFrame();
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

class TestUpdaterSequenceWithLayer : public Co::UpdaterSequenceBase<void>
{
public:
	// LayerとdrawIndexを指定できることを確認
	TestUpdaterSequenceWithLayer()
		: Co::UpdaterSequenceBase<void>(Co::Layer::Modal, 100)
	{
	}

private:
	void update() override
	{
		requestFinish();
	}

	void draw() const override
	{
		// LayerとdrawIndexが正しく設定されていることを内部で確認
		REQUIRE(layer() == Co::Layer::Modal);
		REQUIRE(drawIndex() == 100);
	}
};

TEST_CASE("UpdaterSequence<void> with custom layer and drawIndex")
{
	// UpdaterSequenceBase<void>がレイヤーと描画インデックスを正しく受け取れることを確認
	const auto runner = Co::Play<TestUpdaterSequenceWithLayer>().runScoped();
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
		co_await Co::NextFrame();
		m_pProgress->isPreStartFinished = true;
	}

	Co::Task<void> fadeIn() override
	{
		m_pProgress->isFadeInStarted = true;
		co_await Co::NextFrame();
		m_pProgress->isFadeInFinished = true;
	}

	Co::Task<void> start() override
	{
		m_pProgress->isStartStarted = true;
		co_await Co::NextFrame();
		m_pProgress->isStartFinished = true;
	}

	Co::Task<void> fadeOut() override
	{
		m_pProgress->isFadeOutStarted = true;
		co_await Co::NextFrame();
		m_pProgress->isFadeOutFinished = true;
	}

	Co::Task<void> postFadeOut() override
	{
		m_pProgress->isPostFadeOutStarted = true;
		co_await Co::NextFrame();
		m_pProgress->isPostFadeOutFinished = true;
	}
};

TEST_CASE("Co::PlaySceneFrom<TScene>")
{
	SequenceProgress progress;
	const auto runner = Co::PlaySceneFrom<TestScene>(&progress).runScoped();
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
		co_await Co::NextFrame();
		m_pProgress1->isPreStartFinished = true;
	}

	Co::Task<void> fadeIn() override
	{
		m_pProgress1->isFadeInStarted = true;
		co_await Co::NextFrame();
		m_pProgress1->isFadeInFinished = true;
	}

	Co::Task<void> start() override
	{
		m_pProgress1->isStartStarted = true;
		co_await Co::NextFrame();
		REQUIRE(nextActionRequested() == false);
		REQUIRE(requestNextScene<TestScene>(m_pProgress2) == true);
		REQUIRE(nextActionRequested() == true);
		m_pProgress1->isStartFinished = true;
	}

	Co::Task<void> fadeOut() override
	{
		m_pProgress1->isFadeOutStarted = true;
		co_await Co::NextFrame();
		m_pProgress1->isFadeOutFinished = true;
	}

	Co::Task<void> postFadeOut() override
	{
		m_pProgress1->isPostFadeOutStarted = true;
		co_await Co::NextFrame();
		m_pProgress1->isPostFadeOutFinished = true;
	}
};

TEST_CASE("requestNextScene")
{
	SequenceProgress progress1;
	SequenceProgress progress2;
	const auto runner = Co::PlaySceneFrom<ChainedTestScene>(&progress1, &progress2).runScoped();
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
	const auto runner = Co::PlaySceneFrom<TestUpdaterScene>().runScoped();
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
		.playScoped();

	// 即座に終了
	REQUIRE(runner.done() == true);
	REQUIRE(value == 100.0);
}

TEST_CASE("Co::Ease callback count with zero duration")
{
	// duration = 0の場合のコールバック呼び出し回数を確認
	int callCount = 0;
	double lastValue = -1.0;
	auto callback = [&](double v) { lastValue = v; ++callCount; };
	
	const auto runner = Co::Ease(std::function<void(double)>(callback), 0s)
		.from(0.0)
		.to(100.0)
		.playScoped();

	// 即座に終了
	REQUIRE(runner.done() == true);
	REQUIRE(lastValue == 100.0);
	// コールバックは1回だけ呼ばれるべき
	REQUIRE(callCount == 1);
}

TEST_CASE("Co::Ease and Co::Delay ends at the same time")
{
	TestClock clock;

	double value = -1.0;
	auto easeTask = Co::Ease(&value, 1s).fromTo(0.0, 1.0).setClock(&clock).play();

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
	auto easeTask = Co::LinearEase(&value, 1s).fromTo(0.0, 1.0).setClock(&clock).play();

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

TEST_CASE("Co::Ease with Vec2 from scalar")
{
	// Vec2に対してスカラー値からの構築が動作することを確認
	Vec2 value{ 0, 0 };
	
	auto easeTask = Co::Ease(&value, 0s)
		.from(0.0)
		.to(1.0)
		.play();
	
	const auto runner = std::move(easeTask).runScoped();
	
	REQUIRE(value.x == Approx(1.0));
	REQUIRE(value.y == Approx(1.0));
}

TEST_CASE("Co::Ease with Vec2 from two values")
{
	// Vec2に対して2つの値からの構築が動作することを確認
	Vec2 value{ 0, 0 };
	
	auto easeTask = Co::Ease(&value, 0s)
		.from(1.0, 2.0)
		.to(10.0, 20.0)
		.play();
	
	const auto runner = std::move(easeTask).runScoped();
	
	REQUIRE(value.x == Approx(10.0));
	REQUIRE(value.y == Approx(20.0));
}

TEST_CASE("Co::LinearEase with Vec2 mixed constructors")
{
	// Vec2でfromとtoで異なる構築方法を使用
	Vec2 value{ -1, -1 };
	
	auto easeTask = Co::LinearEase(&value, 0s)
		.from(Vec2{ 5.0, 10.0 })
		.to(100.0)  // スカラー値
		.play();
	
	const auto runner = std::move(easeTask).runScoped();
	
	REQUIRE(value.x == Approx(100.0));
	REQUIRE(value.y == Approx(100.0));
}

TEST_CASE("Co::Ease with Vec3 from scalar")
{
	// Vec3に対してスカラー値からの構築が動作することを確認
	Vec3 value{ 0, 0, 0 };
	
	auto easeTask = Co::Ease(&value, 0s)
		.from(0.0)
		.to(1.0)
		.play();
	
	const auto runner = std::move(easeTask).runScoped();
	
	REQUIRE(value.x == Approx(1.0));
	REQUIRE(value.y == Approx(1.0));
	REQUIRE(value.z == Approx(1.0));
}

TEST_CASE("Co::Ease with Vec3 from three values")
{
	// Vec3に対して3つの値からの構築が動作することを確認
	Vec3 value{ 0, 0, 0 };
	
	auto easeTask = Co::Ease(&value, 0s)
		.from(1.0, 2.0, 3.0)
		.to(10.0, 20.0, 30.0)
		.play();
	
	const auto runner = std::move(easeTask).runScoped();
	
	REQUIRE(value.x == Approx(10.0));
	REQUIRE(value.y == Approx(20.0));
	REQUIRE(value.z == Approx(30.0));
}

TEST_CASE("Co::LinearEase with Vec3 mixed constructors")
{
	// Vec3でfromとtoで異なる構築方法を使用
	Vec3 value{ -1, -1, -1 };
	
	auto easeTask = Co::LinearEase(&value, 0s)
		.from(Vec3{ 5.0, 10.0, 15.0 })
		.to(100.0)  // スカラー値
		.play();
	
	const auto runner = std::move(easeTask).runScoped();
	
	REQUIRE(value.x == Approx(100.0));
	REQUIRE(value.y == Approx(100.0));
	REQUIRE(value.z == Approx(100.0));
}

TEST_CASE("Co::Ease with callback function")
{
	// コールバック関数の型が正しいことを確認
	double receivedValue = -1.0;
	auto callback = [&receivedValue](double v) { receivedValue = v; };
	
	auto easeTask = Co::Ease(std::function<void(double)>(callback), 0s)
		.from(0.0)
		.to(100.0)
		.play();
	
	const auto runner = std::move(easeTask).runScoped();
	
	REQUIRE(receivedValue == Approx(100.0));
}

TEST_CASE("Co::LinearEase with callback function")
{
	// LinearEaseでも同様の確認
	Vec2 receivedValue{ -1.0, -1.0 };
	auto callback = [&receivedValue](Vec2 v) { receivedValue = v; };
	
	auto easeTask = Co::LinearEase(std::function<void(Vec2)>(callback), 0s)
		.from(Vec2{ 0.0, 0.0 })
		.to(Vec2{ 100.0, 200.0 })
		.play();
	
	const auto runner = std::move(easeTask).runScoped();
	
	REQUIRE(receivedValue.x == Approx(100.0));
	REQUIRE(receivedValue.y == Approx(200.0));
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
	const auto runner = Co::Typewriter(&value, 0s, U"TEST").playScoped();

	// 即座に終了
	REQUIRE(runner.done() == true);
	REQUIRE(value == U"TEST");
}

TEST_CASE("Co::Typewriter with empty string")
{
	// 空文字列でのTypewriterの動作を確認
	String value = U"initial";
	const auto runner = Co::Typewriter(&value, 0.1s, U"").playScoped();

	// 空文字列の場合でも正しく動作することを確認
	REQUIRE(runner.done() == true);
	REQUIRE(value == U"");
}

Co::Task<std::unique_ptr<int32>> TaskFinishSourceWaiter(Co::TaskFinishSource<std::unique_ptr<int32>>* pSource)
{
	co_return co_await pSource->waitForResult();
}

Co::Task<void> TaskFinishSourceSetter(Co::TaskFinishSource<std::unique_ptr<int32>>* pSource)
{
	co_await Co::DelayFrame(2);
	pSource->requestFinish(std::make_unique<int32>(42));
}

TEST_CASE("TaskFinishSource waitForResult")
{
	// waitForResult()がムーブセマンティクスを正しく扱うことを確認
	Co::TaskFinishSource<std::unique_ptr<int32>> source;
	
	auto waiterRunner = TaskFinishSourceWaiter(&source).runScoped();
	auto setterRunner = TaskFinishSourceSetter(&source).runScoped();
	
	// 最初はまだ結果がない
	REQUIRE(!source.hasResult());
	REQUIRE(!waiterRunner.done());
	
	// 2フレーム待機
	System::Update();
	System::Update();
	
	// 結果が設定される
	REQUIRE(source.hasResult());
	REQUIRE(!waiterRunner.done());
	
	// もう1フレーム待機してwaiterが完了
	System::Update();
	REQUIRE(waiterRunner.done());
}

TEST_CASE("Co::Typewriter with total duration")
{
	TestClock clock;

	String value;
	const auto runner = Co::Typewriter(&value)
		.text(U"TEST")
		.totalDuration(1s)
		.setClock(&clock)
		.playScoped();

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

template <typename Func, typename... Args>
auto AsyncTaskCaller(Func func, Args... args) -> Co::Task<std::invoke_result_t<Func, Args...>>
	requires std::is_invocable_v<Func, Args...>
{
	co_return co_await Async(func, std::move(args)...);
}

TEST_CASE("s3d::AsyncTask immediate")
{
	int32 value = 0;
	const auto runner = AsyncTaskCaller([] { return 42; }).runScoped([&value](int32 result) { value = result; });

	// 通常の即時returnとは違って別スレッドなので、完了までに最低限の待機は必要
	while (!runner.done())
	{
		System::Update();
	}
	REQUIRE(value == 42);
}

TEST_CASE("s3d::AsyncTask with sleep")
{
	std::atomic<int32> value = 0;

	// 完了に十分な時間待つ
	const auto wait = Co::Delay(0.5s).runScoped();
	{
		const auto runner = AsyncTaskCaller([] { std::this_thread::sleep_for(0.01s); return 42; }).runScoped([&value](int32 result) { value = result; });
		REQUIRE(runner.done() == false);
		REQUIRE(value == 0);

		while (!wait.done())
		{
			System::Update();
		}
	}

	REQUIRE(value == 42);
}

TEST_CASE("s3d::AsyncTask with sleep canceled")
{
	std::atomic<int32> value = 0;
	int32 finishCallbackCOunt = 0;
	int32 cancelCallbackCount = 0;

	// 完了に不十分な時間しか待たない場合も、タスク自体はキャンセルされるが、std::futureの破棄タイミングでスレッド終了を待機するため実行は最後までされることを確認
	{
		const auto runner = AsyncTaskCaller([&] { std::this_thread::sleep_for(0.2s); value = 42; })
			.runScoped([&]() { ++finishCallbackCOunt; }, [&]() { ++cancelCallbackCount; });
		REQUIRE(runner.done() == false);
		REQUIRE(value == 0);

		const auto wait = Co::Delay(0.01s).runScoped();
		while (!wait.done())
		{
			System::Update();
		}
	}
	REQUIRE(value == 42);
	REQUIRE(finishCallbackCOunt == 0);
	REQUIRE(cancelCallbackCount == 1);
}

TEST_CASE("s3d::AsyncTask with exception")
{
	std::atomic<int32> value = 0;
	int32 finishCallbackCOunt = 0;
	int32 cancelCallbackCount = 0;

	auto task = AsyncTaskCaller([&] { std::this_thread::sleep_for(0.01s); throw std::runtime_error("test exception"); value = 42; })
		.runScoped([&]() { ++finishCallbackCOunt; }, [&]() { ++cancelCallbackCount; });
	REQUIRE(value == 0);

	const auto fnWait = [&task]
		{
			while (!task.done())
			{
				std::this_thread::sleep_for(0.01s);

				// System::Update内で例外が発生すると以降のテスト実行に影響が出る可能性があるため、手動resumeでテスト
				Co::detail::Backend::ManualUpdate();
			}
		};

	// 他スレッドで発生した例外が捕捉できることを確認
	REQUIRE_THROWS_WITH(fnWait(), "test exception");
	REQUIRE(value == 0);
	REQUIRE(finishCallbackCOunt == 0);
	REQUIRE(cancelCallbackCount == 1);
}

TEST_CASE("s3d::AsyncTask with move-only result")
{
	std::unique_ptr<int32> result = nullptr;

	// ムーブオンリー型の結果も取得できる
	const auto runner = AsyncTaskCaller([](int32 value) { return std::make_unique<int32>(value * 10); }, 42)
		.runScoped([&](std::unique_ptr<int32>&& r) { result = std::move(r); });

	// 通常の即時returnとは違って別スレッドなので、完了までに最低限の待機は必要
	while (!runner.done())
	{
		System::Update();
	}
	REQUIRE(result != nullptr);
	REQUIRE(*result == 420);
}

namespace AddTaskDuringUpdateTest
{
	struct LogContext
	{
		Array<String> logs;
	};

	Co::Task<> ChildTask(LogContext* pContext)
	{
		pContext->logs.push_back(U"ChildTask: Frame 1");
		co_await Co::NextFrame();
		pContext->logs.push_back(U"ChildTask: Frame 2");
	}

	Co::Task<> ParentTask(LogContext* pContext)
	{
		pContext->logs.push_back(U"ParentTask: Frame 1");
		co_await Co::NextFrame();

		pContext->logs.push_back(U"ParentTask: Frame 2, creating ChildTask");
		Co::ScopedTaskRunner childRunner = ChildTask(pContext).runScoped();

		co_await childRunner.waitUntilDone();

		pContext->logs.push_back(U"ParentTask: Frame 3, after ChildTask finished");
	}
}

TEST_CASE("Adding task during Backend::update loop")
{
	using namespace AddTaskDuringUpdateTest;
	LogContext context;

	// ParentTaskの1フレーム目が即時実行される
	const auto runner = ParentTask(&context).runScoped();
	REQUIRE(context.logs == Array<String>{U"ParentTask: Frame 1"});

	// 1回目のUpdate: ParentTaskがChildTaskを生成し、ChildTaskの1フレーム目が実行される
	System::Update();
	REQUIRE(context.logs == Array<String>{U"ParentTask: Frame 1", U"ParentTask: Frame 2, creating ChildTask", U"ChildTask: Frame 1"});

	// 2回目のUpdate: ChildTaskの2フレーム目が実行され、完了する
	System::Update();
	REQUIRE(context.logs == Array<String>{U"ParentTask: Frame 1", U"ParentTask: Frame 2, creating ChildTask", U"ChildTask: Frame 1", U"ChildTask: Frame 2"});

	// 3回目のUpdate: ChildTaskの完了を受けてParentTaskの3フレーム目が実行され、完了する
	System::Update();
	REQUIRE(context.logs == Array<String>{U"ParentTask: Frame 1", U"ParentTask: Frame 2, creating ChildTask", U"ChildTask: Frame 1", U"ChildTask: Frame 2", U"ParentTask: Frame 3, after ChildTask finished"});

	REQUIRE(runner.done());
}

namespace SelfCancelTest
{
	struct TestContext
	{
		// コルーチン内で寿命を切れさせる必要があるため、ポインタで持つ
		Optional<Co::ScopedTaskRunner>* pRunner;

		bool isAfterReset;
	};

	Co::Task<> SelfCancelingTask(TestContext* pContext)
	{
		co_await Co::NextFrame();
		// 実行中に自分自身をリセット(ScopedTaskRunnerのデストラクタが呼ばれる)
		pContext->pRunner->reset();
		pContext->isAfterReset = true;
		co_await Co::NextFrame();
	}
}

TEST_CASE("Task cancels itself during execution")
{
	using namespace SelfCancelTest;
	Optional<Co::ScopedTaskRunner> runner;
	TestContext context{ &runner, false };

	runner.emplace(
		SelfCancelingTask(&context),
		nullptr,
		[&]()
		{
			// 自分自身を実行キャンセルした場合、実際のキャンセル発生は次のsuspend地点まで遅延される
			// (Backend::m_currentTaskRemovalNeededがtrueになり、resumeの完了後にそれをもとにキャンセルされる)
			REQUIRE(context.isAfterReset == true);
		});

	REQUIRE_NOTHROW(System::Update());
	REQUIRE(runner.has_value() == false);
}

void Main()
{
	Co::Init();

	Catch::Session session;

	int numFailed = session.run();
	if (numFailed == 0)
	{
		Console << U"All tests passed!";
	}
	else
	{
		Console << U"Tests failed: " << numFailed;
	}
}
