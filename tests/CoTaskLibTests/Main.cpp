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

	// 条件を満たしているのでUpdateしても完了しない
	System::Update();
	REQUIRE(runner.isFinished() == false);

	// 条件を満たさないのでUpdateが呼ばれるまでは完了しない
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

void Main()
{
	Co::Init();

	Console.open();
	Catch::Session().run();
	(void)std::getchar();
}
