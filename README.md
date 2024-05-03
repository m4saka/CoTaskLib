# CoTaskLib for Siv3D

Siv3D用コルーチンライブラリ(試験的)。ヘッダオンリー。

C++20の`co_await`/`co_return`キーワードを利用して、複数フレームにまたがる処理を見通しの良いシンプルなコードで実装できます。

## `Co::Task<TResult>`クラス
コルーチンで実行するタスクのクラスです。結果の型をテンプレートパラメータ`TResult`で指定します。  
結果を返す必要がない場合、`Co::Task<void>`を使用します。

### コルーチン制御

`Co::Task`を戻り値とするコルーチン関数内では、下記のキーワードでコルーチンを制御できます。  
※本ライブラリでは`co_yield`は使用しません。

- `co_await`: 他の`Co::Task`を実行し、完了まで待機します。結果がある場合は値を返します。
- `co_return`: `TResult`型で結果を返します。

### メンバ関数
下記のメンバ関数を使用してタスクの実行を制御できます。

- `runScoped()` -> `Co::ScopedTaskRun`
    - タスクの実行を開始し、`Co::ScopedTaskRun`(生存期間オブジェクト)のインスタンスを返します。
    - タスクの実行が完了する前に`Co::ScopedTaskRun`のインスタンスが破棄された場合、タスクは途中で終了されます。
        - メモリ安全性のため、タスク内で外部の変数を参照する場合は必ず`Co::ScopedTaskRun`のインスタンスよりも生存期間が長い変数であることを確認してください。
- `runForget()`
    - タスクの実行を開始し、バックグラウンドで実行します。
    - プログラム終了(正確にはSiv3DのAddon破棄のタイミング)まで実行され続けるため、外部の変数を参照する場合は生存期間に注意してください。
        - 基本的には`runScoped`を優先して使用し、シーンをまたぐ必要がある処理などどうしても必要な場合にのみ`runForget`を使用することを推奨します。
- `with(Co::Task)` -> `Co::Task<TResult>`
    - タスクの実行中、同時に実行される子タスクを登録します。
    - 子タスクの完了は待ちません。親タスクが先に完了した場合、子タスクの実行は途中で終了されます。
    - 子タスクの戻り値は無視されます。
- `withUpdate(std::function<void()>)` -> `Co::Task<TResult>`
    - タスクの実行中にupdateフェーズで毎フレーム実行する関数を登録します。
    - この関数を複数回使用して、複数個の関数を登録することも可能です。その場合、登録した順番で実行されます。
- `withDraw(std::function<void()>)` -> `Co::Task<TResult>`
    - タスクの実行中にdrawフェーズで毎フレーム実行する関数を登録します。
    - この関数を複数回使用して、複数個の関数を登録することも可能です。その場合、登録した順番で実行されます。
- `withLateDraw(std::function<void()>)` -> `Co::Task<TResult>`
    - タスクの実行中にlateDrawフェーズで毎フレーム実行する関数を登録します。
        - lateDrawフェーズはdrawフェーズより後に実行されるフェーズで、最前面に描画したい場合はここに処理を登録します。
    - この関数を複数回使用して、複数個の関数を登録することも可能です。その場合、登録した順番で実行されます。
- `then(std::function<void(TResult)>)` -> `Co::Task<TResult>`
    - タスクの実行完了時に実行する関数を登録します。
    - タスクが実行完了前に途中で終了された場合、登録した関数は実行されません。
    - この関数を複数回使用して、複数個の関数を登録することも可能です。その場合、登録した順番で実行されます。

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
`co_await`へ渡すことで、`Co::Task`を実行して完了まで待機できます。

```cpp
Co::Task<void> ExampleTask()
{
    co_await Co::Delay(1s); // 1秒間待機するコルーチンを実行し、完了まで待機します

    co_await AnotherTask(); // 別に定義したAnotherTask関数のコルーチンを実行し、完了まで待機します
}
```

完了を待つ必要がない場合は`runScoped`関数および`runForget`関数を併用することもできます。

```cpp
Co::Task<void> ExampleTask()
{
    // Task1とTask2を同時に実行開始し、10秒間経ったらタスクの完了を待たずに終了
    const auto anotherTask1Run = Task1().runScoped();
    const auto anotherTask2Run = Task2().runScoped();

    co_await Co::Delay(10s);
}
```

(補足) 複数のタスクを同時に実行したい場合、下記のように`with`関数や`Co::All`関数も利用できます。

```cpp
Co::Task<void> ExampleTask()
{
    // Task1とTask2を同時に実行し、Task1が完了するまで待機
    // (Task1の実行が完了したタイミングでTask2の実行は途中で終了される)
    co_await Task1().with(Task2());
}
```
```cpp
Co::Task<void> ExampleTask()
{
    // Task1とTask2を同時に実行し、両方完了するまで待機
    co_await Co::All(Task1(), Task2());
}
```

## `Co::SequenceBase<TResult>`クラス

シーケンスの基底クラスです。シーケンスとは、タスクと描画処理(draw関数)を組み合わせたものです。  
描画処理を含むタスクを作成したい場合、このクラスを継承したクラスを作成します。

作成したシーケンスは`Co::Task`と同様に`co_await`で実行できます。

シーケンス同士は`co_await`を利用して入れ子構造にできます。そのため、単純なシーケンスを複数組み合わせて複雑なシーケンスを作成することができます。

### 仮想関数

- `start()` -> `Co::Task<TResult>`
    - シーケンス開始時に実行されるコルーチンです。
    - シーケンスは`start`関数の実行が終了したタイミングで終了します。
    - `co_return`で`TResult`型の結果を返します。

- `draw() const`
    - シーケンスの描画処理を記述します。
    - この関数は、毎フレーム実行されます。
        - `start`関数内で`co_await`を使用して別のコルーチンの実行完了まで待機している間も、`draw`関数は実行され続けます。

### シーケンスの実行方法

#### 通常の関数内から実行開始する場合
`Co::MakeTask`関数を使用することで、シーケンスを生成した上でそれを実行する`Co::Task`を取得できます。これに対して通常通り、`runScoped`関数または`runForget`関数を使用します。

もしシーケンスクラスのコンストラクタに引数が必要な場合、`Co::MakeTask`関数の引数として渡すことができます。

```cpp
const auto taskRun = Co::MakeTask<ExampleSequence>().runScoped();
```
```cpp
Co::MakeTask<ExampleSequence>().runForget();
```

### コルーチン内から実行する場合
`Co::Task`と同様、`co_await`へ渡すことでシーケンスを実行して完了まで待機できます。

```cpp
Co::Task<void> ExampleTask()
{
    co_await Co::MakeTask<ExampleSequence>(); // ExampleSequenceを実行し、完了まで待機します
}
```

シーケンスクラスがムーブ構築可能な場合に限り、下記のようにシーケンスクラスのインスタンスを直接`co_await`に指定するシンプルな記述方法も使用できます。  
※ムーブ構築可能でない場合、こちらの記述方法はコンパイルエラーになります。

```cpp
Co::Task<void> ExampleTask()
{
    co_await ExampleSequence{}; // ExampleSequenceを実行し、完了まで待機します
}
```

### 注意点
シーケンス実行の際、シーケンスクラスの`start`関数を外部から直接呼び出すことは想定されていません。  
`start`関数を直接呼び出してタスク実行すると、`draw`関数が呼び出されません。

## `Co::UpdateSequenceBase<TResult>`クラス

毎フレーム実行される`update()`関数を持つシーケンスの基底クラスです。コルーチンを使用しないシーケンスを作成する際は、このクラスを継承します。

コルーチンを使用せずに作成した既存の処理をなるべく変更せずに移植したい場合や、毎フレームの処理を記述した方が都合が良いシーケンスを作成する場合に便利です。

なお、`Co::UpdateSequenceBase`は`Co::SequenceBase`の派生クラスです。`Co::UpdateSequenceBase`を継承した場合でもシーケンスの実行方法に違いはありません。

### 仮想関数

- `update()`
    - 毎フレームの処理を記述します。
    - シーケンスを終了するには、`finish()`関数を実行します。
        - `finish`関数の引数には`TResult`型の結果を指定します。`TResult`が`void`型の場合、引数は不要です。

- `draw() const`
    - シーケンスの描画処理を記述します。
    - この関数は、毎フレーム実行されます。
        - `start`関数内で`co_await`を使用して別のコルーチンの実行完了まで待機している間も、`draw`関数は実行され続けます。

## `Co::SceneBase`クラス

シーンの基底クラスです。シーンを実装するには、このクラスを継承します。

シーンとは、例えばタイトル画面・ゲーム画面・リザルト画面など、ゲームの大まかな画面を表す単位です。

シーン機能を使用しなくてもゲームを作成することは可能ですが、シーン機能を使用することで画面間の遷移を簡単に実装することができます。

### 仮想関数

- `start()` -> `Co::Task<SceneFactory>`
    - シーン開始時に実行されるコルーチンです。
    - シーンは`start`関数の実行が終了したタイミングで終了し、次のシーンへ遷移します。
    - 次のシーンは下記のいずれかで指定します。
        - `co_return Co::MakeSceneFactory<TScene>(...)`
            - シーンクラスを指定し、次のシーンを生成するための`SceneFactory`を返します。
            - `TScene`には次のシーンのクラスを指定します。`TScene`は`Co::SceneBase`の派生クラスである必要があります。
            - 引数には、`TScene`のコンストラクタの引数を指定します。
                - 指定した引数はそれぞれコピーされます。引数にコピー構築できない型が含まれる場合、コンパイルエラーとなります。
        - `co_return Co::SceneFinish()`
            - シーンを完了し、次のシーンへ遷移しません。

- `draw() const`
    - シーンの描画処理を記述します。
    - この関数は、毎フレーム実行されます。
        - `start`関数内で`co_await`を使用して別のコルーチンの実行完了まで待機している間も、`draw`関数は実行され続けます。

- `fadeIn()` -> `Co::Task<void>`
    - シーン開始時に実行されるフェードイン用のコルーチンです。
    - `start()`と同時に実行されます。もし同時に実行したくない場合は、`start()`内の先頭で`co_await waitForFadeIn()`を実行し、フェードイン完了まで待機してください。
    - `fadeIn()`の実行が完了する前に`start()`および`fadeOut()`が実行完了した場合、`fadeIn()`の実行は途中で終了されます。

- `fadeOut()` -> `Co::Task<void>`
    - シーン終了時に実行されるフェードアウト用のコルーチンです。
    - `start()`の完了後に実行されます。

### シーンの実行方法
`Co::MakeTask`関数を使用することで、シーンを生成した上でそれを実行する`Co::Task`を取得できます。これに対して通常通り、`runScoped`関数または`runForget`関数を使用します。

もしシーンクラスのコンストラクタに引数が必要な場合、`Co::MakeTask`関数の引数として渡すことができます。

すべてのシーンが終了したらプログラムを終了するには、下記のように`done()`関数でタスクの完了を確認してwhileループを抜けます。

```cpp
const auto taskRun = Co::MakeTask<ExampleScene>().runScoped();
while (System::Update())
{
    if (taskRun.done())
    {
        break;
    }
}
```

### Siv3D標準のシーン機能との比較

下記の点が異なります。

- CoTaskLibのシーン機能には、SceneManagerのようなマネージャークラスがありません。遷移先シーンのクラスは直接`Co::MakeSceneFactory`のテンプレート引数として指定するため、シーン名の登録などが必要ありません。
- CoTaskLibでは、シーンクラスのコンストラクタに引数を持たせることができます。そのため、遷移元のシーンから必要なデータを受け渡すことができます。
    - Siv3D標準のシーン機能にある`getData()`のような、シーン間でグローバルにデータを受け渡すための機能は提供していません。代わりに、シーンクラスのコンストラクタに引数を用意して受け渡してください。
- CoTaskLibのシーンクラスでは、毎フレーム実行されるupdate関数の代わりに、`start()`関数というコルーチン関数を実装します。
    - update関数を使用したい場合、`SceneBase`クラスの代わりに`UpdateSceneBase`クラスを基底クラスとして使用するか(詳細は後述)、`co_await Co::Delay(10s).withUpdate([this] { update(); })`のように`Co::Task`の`withUpdate()`関数を使用してタスク実行に紐づけて実行してください。

## `Co::UpdateSceneBase`クラス

毎フレーム実行される`update()`関数を持つシーンの基底クラスです。コルーチンを使用しないシーンを作成する際は、このクラスを継承します。

Siv3D標準のシーン機能を使用して作成したシーンをなるべく変更せずに移植したい場合や、毎フレームの処理を記述した方が都合が良いシーンを作成する場合に便利です。

なお、`Co::UpdateSceneBase`は`Co::SceneBase`の派生クラスです。`Co::UpdateSceneBase`を継承した場合でもシーンの実行方法に違いはありません。

### 仮想関数

- `update()`
    - 毎フレームの処理を記述します。
    - シーンを終了して次のシーンへ遷移するには、基底クラスに実装されている下記のいずれかの関数を実行します。
        - `requestNextScene<TScene>(...)`関数
            - シーンクラスを指定し、次のシーンへ遷移します。
            - `TScene`には次のシーンのクラスを指定します。`TScene`は`Co::SceneBase`の派生クラスである必要があります。
            - 引数には、`TScene`のコンストラクタの引数を指定します。
                - 指定した引数はそれぞれコピーされます。引数にコピー構築できない型が含まれる場合、コンパイルエラーとなります。
        - `requestSceneFinish()`関数
            - シーンを完了し、次のシーンへ遷移しません。

- `draw() const`
    - シーンの描画処理を記述します。
    - この関数は、毎フレーム実行されます。
        - `start`関数内で`co_await`を使用して別のコルーチンの実行完了まで待機している間も、`draw`関数は実行され続けます。

- `fadeIn()` -> `Co::Task<void>`
    - シーン開始時に実行されるフェードイン用のコルーチンです。
    - `update()`と同時に実行されます。もし同時に実行したくない場合は、`update`関数内で`isFadingIn()`がtrueの場合は処理をスキップするなどしてください。
    - `fadeIn()`の実行が完了する前にシーン処理および`fadeOut()`が実行完了した場合、`fadeIn()`の実行は途中で終了されます。

- `fadeOut()` -> `Co::Task<void>`
    - シーン終了時に実行されるフェードアウト用のコルーチンです。
    - `update()`内でシーン終了の関数が呼ばれた後に実行されます。

## 関数一覧
- `Co::Init()`
    - `CoTaskLib`ライブラリを初期化します。
    - `Co::Task`の`runScoped`関数または`runForget`関数を初めて呼び出す前に、必ず一度だけ実行してください。
- `Co::DelayFrame()` -> `Co::Task<void>`
    - 1フレーム待機します。
- `Co::DelayFrame(int32)` -> `Co::Task<void>`
    - 指定されたフレーム数だけ待機します。
- `Co::Delay(Duration)` -> `Co::Task<void>`
    - 指定された時間だけ待機します。
- `Co::Delay(Duration, std::function<void(const Timer&)>)` -> `Co::Task<void>`
    - 指定された時間だけ待機し、待機中に定期的に指定された関数を実行します。
- `Co::WaitUntil(std::function<bool()>)` -> `Co::Task<void>`
    - 指定された条件が満たされるまで待機します。
- `Co::WaitWhile(std::function<bool()>)` -> `Co::Task<void>`
    - 指定された関数を毎フレーム実行し、結果がtrueの間、待機します。
- `Co::WaitForever()` -> `Co::Task<void>`
    - 永久に待機します。
- `Co::WaitForResult(const Optional<T>*)` -> `Co::Task<T>`
    - `Optional`の`has_value()`関数がtrueを返すまで待機します。
    - 値はコピーして返却されます。値のコピーを避けたい場合は、代わりに`Co::WaitUntil`を使用して`has_value()`がtrueを返すまで待機し、`Optional`の値を手動で取得してください。
- `Co::WaitForTimer(const Timer*)` -> `Co::Task<void>`
    - `Timer`が0になるまで待機します。この関数は`Timer`を自動的に開始しないので、あらかじめ開始しておく必要があります。
- `Co::WaitForDown(TInput)` -> `Co::Task<void>`
    - 入力が押されるまで待機します。
- `Co::WaitForUp(TInput)` -> `Co::Task<void>`
    - 入力が離されるまで待機します。
- `Co::WaitForLeftClicked(TArea)` -> `Co::Task<void>`
    - マウスの左ボタンが指定領域で押されるまで待機します。
- `Co::WaitForLeftReleased(TArea)` -> `Co::Task<void>`
    - マウスの左ボタンが指定領域で離されるまで待機します。
- `Co::WaitForLeftClickedThenReleased(TArea)` -> `Co::Task<void>`
    - マウスの左ボタンが指定領域でクリックされてから離されるまで待機します。
- `Co::WaitForRightClicked(TArea)` -> `Co::Task<void>`
    - マウスの右ボタンが指定領域で押されるまで待機します。
- `Co::WaitForRightReleased(TArea)` -> `Co::Task<void>`
    - マウスの右ボタンが指定領域で離されるまで待機します。
- `Co::WaitForRightClickedThenReleased(TArea)` -> `Co::Task<void>`
    - マウスの右ボタンが指定領域でクリックされてから離されるまで待機します。
- `Co::WaitForMouseOver(TArea)` -> `Co::Task<void>`
    - マウスカーソルが指定領域内に侵入するまで待機します。
- `Co::ExecOnDown(TInput, std::function<void()>)` -> `Co::Task<void>`
    - 入力が押された時に関数を実行します。
- `Co::ExecOnUp(TInput, std::function<void()>)` -> `Co::Task<void>`
    - 入力が離された時に関数を実行します。
- `Co::ExecOnPressed(TInput, std::function<void()>)` -> `Co::Task<void>`
    - 入力が押されている間、関数を実行します。
- `Co::ExecOnLeftClicked(TArea, std::function<void()>)` -> `Co::Task<void>`
    - マウスの左ボタンが指定領域で押された時に関数を実行します。
- `Co::ExecOnLeftPressed(TArea, std::function<void()>)` -> `Co::Task<void>`
    - マウスの左ボタンが指定領域で押され続けている間、関数を実行します。
- `Co::ExecOnLeftReleased(TArea, std::function<void()>)` -> `Co::Task<void>`
    - マウスの左ボタンが指定領域で離された時に関数を実行します。
- `Co::ExecOnLeftClickedThenReleased(TArea, std::function<void()>)` -> `Co::Task<void>`
    - マウスの左ボタンを指定領域でクリックした後、指定領域で離した時に関数を実行します。
- `Co::ExecOnRightClicked(TArea, std::function<void()>)` -> `Co::Task<void>`
    - マウスの右ボタンが指定領域で押された時に関数を実行します。
- `Co::ExecOnRightPressed(TArea, std::function<void()>)` -> `Co::Task<void>`
    - マウスの右ボタンが指定領域で押され続けている間、関数を実行します。
- `Co::ExecOnRightReleased(TArea, std::function<void()>)` -> `Co::Task<void>`
    - マウスの右ボタンが指定領域で離された時に関数を実行します。
- `Co::ExecOnRightClickedThenReleased(TArea, std::function<void()>)` -> `Co::Task<void>`
    - マウスの右ボタンを指定領域でクリックした後、指定領域で離した時に関数を実行します。
- `Co::ExecOnMouseOver(TArea, std::function<void()>)` -> `Co::Task<void>`
    - マウスが指定領域上にある間、関数を実行します。
- `Co::SimpleFadeIn(Duration, ColorF)` -> `Co::Task<void>`
    - 指定色からのフェードインを開始し、完了まで待機します。
- `Co::SimpleFadeOut(Duration, ColorF)` -> `Co::Task<void>`
    - 指定色へのフェードアウトを開始し、完了まで待機します。
- `Co::All(TTasks&&...)` -> `Co::Task<std::tuple<...>>`
    - すべての`Co::Task`が完了するまで待機します。各`Co::Task`の結果が`std::tuple`で返されます。
    - `Co::Task`の結果が`void`型の場合、`Co::VoidResult`型(空の構造体)に置換して返されます。
- `Co::Any(TTasks&&...)` -> `Co::Task<std::tuple<Optional<...>>>`
    - いずれかの `Co::Task` が完了した時点で進行し、各`Co::Task`の結果が`Optional<T>`型の`std::tuple`で返されます。
    - `Co::Task`の結果が`void`型の場合、`Co::VoidResult`型(空の構造体)に置換して返されます。
- `Co::MakeTask<TSequence>(...)` -> `Co::Task<TResult>`
    - `TSequence`クラスのインスタンスを構築し、それを実行するタスクを返します。
    - `TSequence`クラスは`Co::SequenceBase<TResult>`の派生クラスである必要があります。
    - 引数には、`TSequence`のコンストラクタの引数を指定します。
- `Co::MakeTask<TScene>(...)` -> `Co::Task<void>`
    - `TScene`クラスのインスタンスを構築し、それを実行するタスクを返します。
    - `TScene`クラスは`Co::SceneBase`の派生クラスである必要があります。
    - 引数には、`TScene`のコンストラクタの引数を指定します。

## サンプル

### サンプル1: 時間待ち

「Hello, ○○!」、「Nice to meet you!」というメッセージを、1秒間の時間待ちを挟みながら順番に表示するサンプルです。

```cpp
#include <Siv3D.hpp>
#include <CoTaskLib.hpp>

Co::Task<void> Greet(const String name) // 注意: コルーチンには参照を渡さないこと
{
    Print << U"Hello, " << name << U"!";
    co_await Co::Delay(1s);
    Print << U"Nice to meet you!";
    co_await Co::Delay(1s);
}

Co::Task<void> ShowMessages()
{
    co_await Greet(U"World");
    co_await Greet(U"Siv3D");
    co_await Greet(U"Co::Task");
}

void Main()
{
    Co::Init();

    const auto taskRun = ShowMessages().runScoped();
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

Co::Task<String> ShowQuestion(const String question) // 注意: コルーチンには参照を渡さないこと
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

Co::Task<void> MainTask()
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

    const auto mainTaskRun = MainTask().runScoped();
    while (System::Update())
    {
    }
}
```
