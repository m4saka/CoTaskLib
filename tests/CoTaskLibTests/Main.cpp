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

void Main()
{
	Co::Init();

	Console.open();
	Catch::Session().run();
	(void)std::getchar();
}
