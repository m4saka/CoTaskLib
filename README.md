# CoTask for Siv3D

Siv3D用コルーチンライブラリ(試験的)。ヘッダオンリー。

C++20の`co_await`/`co_return`キーワードを利用して、複数フレームにまたがる処理を見通しの良いシンプルなコードで実装できます。

## サンプル1: 時間待ち

`co_await Delay(待ち時間);`で時間待ちが可能です。

```cpp
#include <Siv3D.hpp>
#include <CoTask.hpp>

CoTask<void> Greet(StringView name)
{
	Print << U"Hello, " << name << U"!";
	co_await Delay(1s);
	Print << U"Nice to meet you!";
	co_await Delay(1s);
}

CoTask<void> ShowMessages()
{
	co_await Greet(U"World");
	co_await Greet(U"Siv3D");
	co_await Greet(U"CoTask");
}

void Main()
{
	CoTaskBackend::Init();

	const ScopedCoTaskRun scopedCoTaskRun = ShowMessages().runScoped();
	while (System::Update())
	{
	}
}
```

## サンプル2: 質問ダイアログ

質問文とテキストボックスを表示し、ユーザーが「OK」ボタンを押したタイミングでテキストボックスの内容を返却します。
co_returnで返却した戻り値はco_awaitで受け取ることができます。

```cpp
#include <Siv3D.hpp>
#include <CoTask.hpp>

CoTask<String> ShowQuestion(StringView question)
{
	Font font(30);
	TextEditState textEditState;

	while (true)
	{
		font(question).draw(40, 40);

		SimpleGUI::TextBox(textEditState, { 40, 120 }, 400);

		if (SimpleGUI::Button(U"OK", { 450, 120 }, 100))
		{
			MouseL.clearInput();
			co_return textEditState.text;
		}

		co_await DelayFrame();
	}
}

CoTask<void> MainTask()
{
	const String name = co_await ShowQuestion(U"あなたの名前は？");

	const int32 rand1 = Random(1, 10);
	const int32 rand2 = Random(1, 10);
	const String answer = co_await ShowQuestion(U"こんにちは、{}さん！{}+{}は何でしょう？"_fmt(name, rand1, rand2));
	if (ParseOpt<int32>(answer) == rand1 + rand2)
	{
		Print << U"正解！";
	}
	else
	{
		Print << U"不正解！";
	}
}

void Main()
{
	CoTaskBackend::Init();

	const ScopedCoTaskRun scopedCoTaskRun = MainTask().runScoped();
	while (System::Update())
	{
	}
}
```
