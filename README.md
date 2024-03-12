# CoTaskLib for Siv3D

Siv3D用コルーチンライブラリ(試験的)。ヘッダオンリー。

C++20の`co_await`/`co_return`キーワードを利用して、複数フレームにまたがる処理を見通しの良いシンプルなコードで実装できます。

## `CoTask<TResult>`クラス
コルーチンで実行するタスクのクラスです。結果の型をテンプレートパラメータ`TResult`で指定します。  
結果を返す必要がない場合、`CoTask<void>`を使用します。

### コルーチン制御

`CoTask`を戻り値とするコルーチン関数内では、下記のキーワードでコルーチンを制御できます。  
※本ライブラリでは`co_yield`は使用しません。

- `co_await`: 他の`CoTask`を実行し、終了まで待機します。結果がある場合は値を返します。
- `co_return`: `TResult`型で結果を返します。

### メンバ関数
下記のメンバ関数を使用してタスクの実行を開始できます。

- `runScoped()` -> `Co::ScopedTaskRun`
    - タスクの実行を開始し、`Co::ScopedTaskRun`(生存期間オブジェクト)のインスタンスを返します。
	- タスクの実行が完了する前に`Co::ScopedTaskRun`のインスタンスが破棄された場合、タスクは中断されます。
    	- メモリ安全性のため、タスク内で外部の変数を参照する場合は必ず`Co::ScopedTaskRun`のインスタンスよりも生存期間が長い変数であることを確認してください。
- `runForget()`
    - タスクの実行を開始し、バックグラウンドで実行します。
    - プログラム終了(正確にはSiv3DのAddon破棄のタイミング)まで実行され続けるため、外部の変数を参照する場合は生存期間に注意してください。
		- 基本的には`runScoped`を優先して使用し、シーンをまたぐ必要がある処理などどうしても必要な場合にのみ`runForget`を使用することを推奨します。

### タスクの実行方法

#### 通常の関数内から実行開始する場合
`runScoped`関数または`runForget`関数を使用します。

```cpp
const auto taskRun = ExampleTask().runScoped();
```
```cpp
ExampleTask().runForget();
```

### コルーチン内から実行する場合
`co_await`へ渡すことで、`CoTask`を実行して完了まで待機できます。

```cpp
CoTask<void> ExampleTask()
{
	co_await Co::Delay(1s); // 1秒間待機するコルーチンを実行し、完了まで待機します

	co_await AnotherTask(); // 別に定義したAnotherTask関数のコルーチンを実行し、完了まで待機します
}
```

完了を待つ必要がない場合は`runScoped`関数および`runForget`関数を併用することもできます。

```cpp
CoTask<void> ExampleTask()
{
	// 2つのタスクを同時に実行開始し、10秒間経ったらタスクの完了を待たずに終了
	const auto anotherTask1Run = AnotherTask1().runScoped();
	const auto anotherTask2Run = AnotherTask2().runScoped();

	co_await Co::Delay(10s);
}
```

(補足) 複数のタスクを同時に実行したい場合、下記のように`Co::WhenAll`関数も利用できます。

```cpp
CoTask<void> ExampleTask()
{
	// 2つのタスクを同時に実行し、両方完了するまで待機
	co_await Co::WhenAll(AnotherTask1(), AnotherTask2());
}
```

## `CoSceneBase<TResult>`クラス

シーンの基底クラスです。`CoTask`を活用してシーンを作成したい場合、このクラスを継承してシーンクラスを作成します。

作成したシーンは`CoTask`と同様に`co_await`で実行できます。

シーン同士は`co_await`を利用して簡単に入れ子構造にできます。そのため、「シーン」という名称で一般的に想起されるタイトル画面やゲーム画面といった大きな単位だけでなく、ちょっとした画面表示(エフェクトのアニメーション、ダイアログ表示など)を記述する際にも活用できます。描画処理が含まれる`CoTask`を作成したい場合はシーンとして記述する、と覚えておくと良いでしょう。

### 仮想関数

- `start()` -> `CoTask<TResult>`
    - シーン開始時に実行されるコルーチンです。
	- シーンは`start`関数の実行が終了↓タイミングで終了します。
	- `co_return`で`TResult`型の結果を返します。

- `draw() const`
    - シーンの描画処理を記述します。
	- この関数は、毎フレーム実行されます。
	    - `start`関数内で`co_await`を使用して別のコルーチンの実行完了まで待機している間も、`draw`関数は実行され続けます。

### シーンの実行方法

#### 通常の関数内から実行開始する場合
`Co::MakeTask`関数を使用することで、シーンを生成した上でそれを実行する`CoTask`を取得できます。これに対して通常通り、`runScoped`関数または`runForget`関数を使用します。

もしシーンクラスのコンストラクタに引数が必要な場合、`Co::MakeTask`関数の引数として渡すことができます。

```cpp
const auto taskRun = Co::MakeTask<ExampleScene>().runScoped();
```
```cpp
Co::MakeTask<ExampleScene>().runForget();
```

### コルーチン内から実行する場合
`CoTask`と同様、`co_await`へ渡すことでシーンを実行して完了まで待機できます。

```cpp
CoTask<void> ExampleTask()
{
	co_await Co::MakeTask<ExampleScene>(); // ExampleSceneを実行し、完了まで待機します
}
```

シーンクラスがムーブ構築可能な場合に限り、下記のようにシーンクラスのインスタンスを直接`co_await`に指定するシンプルな記述方法も使用できます。  
※ムーブ構築可能でない場合、こちらの記述方法はコンパイルエラーになります。

```cpp
CoTask<void> ExampleTask()
{
	co_await ExampleScene{}; // ExampleSceneを実行し、完了まで待機します
}
```

### 注意点
シーン実行の際、シーンクラスの`start`関数を外部から手動で呼び出すことは想定されていません。  
`start`関数を手動で呼び出してタスク実行すると、`draw`関数が呼び出されずシーンが正常に実行されません。

## 関数一覧
- `Co::Init()`
    - `CoTaskLib`ライブラリを初期化します。
	- `CoTask`の`runScoped`関数または`runForget`関数を初めて呼び出す前に、必ず一度だけ実行してください。
- `Co::DelayFrame()` -> `CoTask<void>`
    - 1フレーム待機します。
- `Co::DelayFrame(int32)` -> `CoTask<void>`
    - 指定されたフレーム数だけ待機します。
- `Co::Delay(Duration)` -> `CoTask<void>`
    - 指定された時間だけ待機します。
- `Co::Delay(Duration, std::function<void(const Timer&)>)` -> `CoTask<void>`
    - 指定された時間だけ待機し、待機中に定期的に指定された関数を実行します。
- `Co::WaitUntil(std::function<bool()>)` -> `CoTask<void>`
    - 指定された条件が満たされるまで待機します。
- `Co::WaitForTimer(const Timer*)` -> `CoTask<void>`
    - `Timer`が0になるまで待機します。この関数は`Timer`を自動的に開始しないので、あらかじめ開始しておく必要があります。
- `Co::WaitForDown(TInput)` -> `CoTask<void>`
    - 入力が押されるまで待機します。
- `Co::WaitForUp(TInput)` -> `CoTask<void>`
    - 入力が離されるまで待機します。
- `Co::WaitForLeftClicked(TArea)` -> `CoTask<void>`
    - マウスの左ボタンが指定領域で押されるまで待機します。
- `Co::WaitForLeftReleased(TArea)` -> `CoTask<void>`
    - マウスの左ボタンが指定領域で離されるまで待機します。
- `Co::WaitForLeftClickedThenReleased(TArea)` -> `CoTask<void>`
    - マウスの左ボタンが指定領域でクリックされてから離されるまで待機します。
- `Co::WaitForRightClicked(TArea)` -> `CoTask<void>`
    - マウスの右ボタンが指定領域で押されるまで待機します。
- `Co::WaitForRightReleased(TArea)` -> `CoTask<void>`
    - マウスの右ボタンが指定領域で離されるまで待機します。
- `Co::WaitForRightClickedThenReleased(TArea)` -> `CoTask<void>`
    - マウスの右ボタンが指定領域でクリックされてから離されるまで待機します。
- `Co::WaitForMouseOver(TArea)` -> `CoTask<void>`
    - マウスカーソルが指定領域内に侵入するまで待機します。
- `Co::WaitWhile(std::function<bool()>)` -> `CoTask<void>`
    - 指定された関数を毎フレーム実行し、結果がtrueの間、待機します。
- `Co::EveryFrame(std::function<void()>)` -> `CoTask<void>`
    - updateフェーズで毎フレーム指定された関数を実行します。
- `Co::EveryFrameDraw(std::function<void()>)` -> `CoTask<void>`
    - drawフェーズで毎フレーム指定された関数を実行します。
- `Co::EveryFramePostPresent(std::function<void()>)` -> `CoTask<void>`
    - postPresentフェーズで毎フレーム指定された関数を実行します。
- `Co::ExecOnDown(TInput, std::function<void()>)` -> `CoTask<void>`
    - 入力が押された時に関数を実行します。
- `Co::ExecOnUp(TInput, std::function<void()>)` -> `CoTask<void>`
    - 入力が離された時に関数を実行します。
- `Co::ExecOnPressed(TInput, std::function<void()>)` -> `CoTask<void>`
    - 入力が押されている間、関数を実行します。
- `Co::ExecOnLeftClicked(TArea, std::function<void()>)` -> `CoTask<void>`
    - マウスの左ボタンが指定領域で押された時に関数を実行します。
- `Co::ExecOnLeftPressed(TArea, std::function<void()>)` -> `CoTask<void>`
    - マウスの左ボタンが指定領域で押され続けている間、関数を実行します。
- `Co::ExecOnLeftReleased(TArea, std::function<void()>)` -> `CoTask<void>`
    - マウスの左ボタンが指定領域で離された時に関数を実行します。
- `Co::ExecOnLeftClickedThenReleased(TArea, std::function<void()>)` -> `CoTask<void>`
    - マウスの左ボタンを指定領域でクリックした後、指定領域で離した時に関数を実行します。
- `Co::ExecOnRightClicked(TArea, std::function<void()>)` -> `CoTask<void>`
    - マウスの右ボタンが指定領域で押された時に関数を実行します。
- `Co::ExecOnRightPressed(TArea, std::function<void()>)` -> `CoTask<void>`
    - マウスの右ボタンが指定領域で押され続けている間、関数を実行します。
- `Co::ExecOnRightReleased(TArea, std::function<void()>)` -> `CoTask<void>`
    - マウスの右ボタンが指定領域で離された時に関数を実行します。
- `Co::ExecOnRightClickedThenReleased(TArea, std::function<void()>)` -> `CoTask<void>`
    - マウスの右ボタンを指定領域でクリックした後、指定領域で離した時に関数を実行します。
- `Co::ExecOnMouseOver(TArea, std::function<void()>)` -> `CoTask<void>`
    - マウスが指定領域上にある間、関数を実行します。
- `Co::FadeIn(Duration, ColorF)` -> `CoTask<void>`
    - フェードインを開始し、完了まで待機します。
- `Co::FadeOut(Duration, ColorF)` -> `CoTask<void>`
    - フェードアウトを開始し、完了まで待機します。
- `Co::IsFading()` -> `bool`
    - 現在フェードイン中またはフェードアウト中かどうかを返します。
	- もし独自のフェードイン・フェードアウト処理を作成したい場合、フェード開始時に`Co::ScopedSetIsFadingToTrue`のインスタンスを生成、フェード完了時に破棄することで、独自のフェード処理中も本関数の結果を`true`に上書きできます。
- `Co::WhenAll(TTasks&&...)` -> `CoTask<std::tuple<...>>`
    - すべての`CoTask`が完了するまで待機します。各`CoTask`の結果が`std::tuple`で返されます。
	- `CoTask`の結果が`void`型の場合、`Co::VoidResult`型(空の構造体)に置換して返されます。
- `Co::WhenAny(TTasks&&...)` -> `CoTask<std::tuple<Optional<...>>>`
    - いずれかの `CoTask` が完了した時点で進行し、各`CoTask`の結果が`Optional<T>`型の`std::tuple`で返されます。
	- `CoTask`の結果が`void`型の場合、`Co::VoidResult`型(空の構造体)に置換して返されます。
- `Co::MakeTask<TScene>(...)` -> `CoTask<TResult>`
    - `TScene`クラスのインスタンスを構築し、それを実行するタスクを返します。
	- `TScene`クラスは`CoSceneBase<TResult>`の派生クラスである必要があります。
	- 引数には、`TScene`のコンストラクタの引数を指定します。

## サンプル

### サンプル1: 時間待ち

「Hello, ○○!」、「Nice to meet you!」というメッセージを、1秒間の時間待ちを挟みながら順番に表示するサンプルです。

```cpp
#include <Siv3D.hpp>
#include <CoTaskLib.hpp>

CoTask<void> Greet(const String name) // 注意: コルーチンには参照を渡さないこと
{
	Print << U"Hello, " << name << U"!";
	co_await Co::Delay(1s);
	Print << U"Nice to meet you!";
	co_await Co::Delay(1s);
}

CoTask<void> ShowMessages()
{
	co_await Greet(U"World");
	co_await Greet(U"Siv3D");
	co_await Greet(U"CoTask");
}

void Main()
{
	Co::Init();

	const auto scopedCoTaskRun = ShowMessages().runScoped();
	while (System::Update())
	{
	}
}
```

### サンプル2: 質問ダイアログ

質問文とテキストボックスを表示し、ユーザーが「OK」ボタンを押したタイミングでテキストボックスの内容を返却するサンプルです。
コルーチンからco_returnで返却した戻り値はco_awaitで受け取ることができます。

```cpp
#include <Siv3D.hpp>
#include <CoTaskLib.hpp>

CoTask<String> ShowQuestion(const String question) // 注意: コルーチンには参照を渡さないこと
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

		co_await Co::DelayFrame();
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
	Co::Init();

	const auto scopedCoTaskRun = MainTask().runScoped();
	while (System::Update())
	{
	}
}
```
