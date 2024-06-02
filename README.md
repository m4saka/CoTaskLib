# CoTaskLib for Siv3D
Siv3D用コルーチンタスクライブラリ(試験的)。ヘッダオンリー。

C++20の`co_await`/`co_return`キーワードを利用して、複数フレームにまたがる処理を見通しの良いシンプルなコードで実装できます。

## `Co::Task<TResult>`クラス
コルーチンで実行するタスクのクラスです。結果の型をテンプレートパラメータ`TResult`で指定します。  
結果を返す必要がない場合、`Co::Task<void>`を使用します。

#### 戻り値がない場合の例:
クリックしたタイミングで完了します。

```cpp
Co::Task<void> ExampleTask()
{
    // クリックされるまで待機
    co_await Co::WaitForDown(MouseL);

    Print << U"クリックされました！";
}
```

#### 戻り値がある場合の例:
クリックまたは右クリックしたタイミングで完了し、呼び出し元へ文字列を返します。

```cpp
Co::Task<String> ExampleTaskWithResult()
{
    // クリックまたは右クリックされるまで待機
    const auto [isMouseL, isMouseR] = co_await Co::Any(
        Co::WaitForDown(MouseL),
        Co::WaitForDown(MouseR));

    // どちらが押されたかに応じて、文字列を返す
    if (isMouseL)
    {
        co_return U"クリックされました！";
    }
    else
    {
        co_return U"右クリックされました！";
    }
}
```

### コルーチン制御
`Co::Task`を戻り値とするコルーチン関数内では、下記のキーワードでコルーチンを制御できます。  
※本ライブラリでは`co_yield`は使用しません。

- `co_await`: 他の`Co::Task`を実行し、完了まで待機します。結果がある場合は値を返します。
- `co_return`: `TResult`型で結果を返します。

### メンバ関数
下記のメンバ関数を使用してタスクの実行を制御できます。

- `runScoped()` -> `Co::ScopedTaskRunner`
    - タスクの実行を開始し、`Co::ScopedTaskRunner`(生存期間オブジェクト)のインスタンスを返します。
    - タスクの実行が完了する前に`Co::ScopedTaskRunner`のインスタンスが破棄された場合、タスクは途中で終了されます。
        - メモリ安全性のため、タスク内で外部の変数を参照する場合は必ず`Co::ScopedTaskRunner`のインスタンスよりも生存期間が長い変数であることを確認してください。
    - 第1引数に`std::funciton<void(const TResult&)>`型でタスク完了時のコールバック関数、第2引数に`std::function<void()>`型でタスクキャンセル時のコールバック関数を指定することもできます。
- `with(Co::Task)` -> `Co::Task<TResult>`
    - タスクの実行中、同時に実行される子タスクを登録します。
    - 子タスクの完了は待ちません。親タスクが先に完了した場合、子タスクの実行は途中で終了されます。
    - 子タスクの戻り値は無視されます。
    - この関数を複数回使用して、複数個のタスクを登録することも可能です。その場合、毎フレームの処理は登録した順番で実行されます。

### タスクの実行方法

#### 通常の関数内から実行開始する場合
`runScoped`関数を使用します。

```cpp
const auto taskRunner = ExampleTask().runScoped();
```

### コルーチン内から実行する場合
`co_await`へ渡すことで、`Co::Task`を実行して完了まで待機できます。

```cpp
co_await ExampleTask();
```

完了を待つ必要がない場合は`runScoped`関数を併用することもできます。

```cpp
Co::Task<void> ExampleTask()
{
    // Task1とTask2を同時に実行開始し、10秒間経ったらタスクの完了を待たずに終了
    const auto anotherTask1Runner = Task1().runScoped();
    const auto anotherTask2Runner = Task2().runScoped();

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

```cpp
class ExampleSequence : public Co::SequenceBase<void>
{
private:
    Co::Task<void> start() override
    {
        // ここに処理をコルーチンで記述
        co_return;
    }

    void draw() const override
    {
        // ここに毎フレームの描画処理を記述
    }

    Co::Task<void> fadeIn() override
    {
        // 必要に応じて、フェードイン処理をコルーチンで記述(startと同時に実行される)
    }

    Co::Task<void> fadeOut() override
    {
        // 必要に応じて、フェードアウト処理をコルーチンで記述(startの完了後に実行される)
    }

    Co::Task<void> preStart() override
    {
        // 必要に応じて、fadeIn・startより前に実行すべき処理(ローディングなど)があればコルーチンで記述
        co_return;
    }
};
```

### 仮想関数
下記の仮想関数をオーバーライドできます。`start()`のみ必須で、それ以外は必要な場合のみオーバーライドしてください。

- `start()` -> `Co::Task<TResult>`
    - シーケンス開始時に実行されるコルーチンです。
    - シーケンスは`start`関数の実行が終了したタイミングで終了します。
    - `co_return`で`TResult`型の結果を返します。

- `draw() const`
    - シーケンスの描画処理を記述します。
    - この関数は、毎フレーム実行されます。
        - `start`関数内で`co_await`を使用して別のコルーチンの実行完了まで待機している間も、`draw`関数は実行され続けます。

- `drawIndex() const` -> `int32`
    - 描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番と同じ順序で描画されます。

- `fadeIn()` -> `Co::Task<void>`
    - シーケンス開始時に実行されるフェードイン用のコルーチンです。
    - `start()`と同時に実行されます。もし同時に実行したくない場合は、`start()`内の先頭で`co_await waitForFadeIn()`を実行し、フェードイン完了まで待機してください。
    - `fadeIn()`の実行が完了する前に`start()`および`fadeOut()`が実行完了した場合、`fadeIn()`の実行は途中で終了されます。

- `fadeOut()` -> `Co::Task<void>`
    - シーケンス終了時に実行されるフェードアウト用のコルーチンです。
    - `start()`の完了後に実行されます。
    - `fadeOut()`内で`start()`の戻り値に応じて別々のフェードアウトを実装したい場合、`result()`を使用して結果を取得できます。

- `preStart()` -> `Co::Task<void>`
    - `start()`および`fadeIn()`より前に呼び出されるコルーチンです。
    - ローディング処理を複数フレームにわたって実行する場合など、フェードイン開始より前に何か処理を実行したい場合に使用します。
    - Tips: 変数の初期化等のコルーチンで実装する必要がない処理は、`preStart()`関数を使用せずシーケンスクラスのコンストラクタ内に処理を記述するのが良いでしょう。

- `preStartDraw() const`
    - `preStart()`の実行中に毎フレーム呼び出される描画処理です。

- `preStartDrawIndex()` -> `int32`
    - `preStart()`の実行中の描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番で描画されます。

- `postFadeOut()` -> `Co::Task<void>`
    - `fadeOut()`より後に呼び出されるコルーチンです。
    - フェードアウト後に何か処理を実行したい場合に使用します。

- `postFadeOutDraw() const`
    - `postFadeOut()`の実行中に毎フレーム呼び出される描画処理です。

- `postFadeOutDrawIndex()` -> `int32`
    - `postFadeOut()`の実行中の描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番で描画されます。

### シーケンスの実行方法

#### 通常の関数内から実行開始する場合
`Co::Play`関数を使用することで、シーケンスを再生する`Co::Task`を取得できます。これに対して通常通り、`runScoped`関数を使用します。  
もしシーケンスクラスのコンストラクタに引数が必要な場合、`Co::Play`関数の引数として渡すことができます。

```cpp
const auto taskRunner = Co::Play<ExampleSequence>().runScoped();
```

上記の書き方では、シーケンスのインスタンスへ外部からアクセスすることはできません。  
もしタスク実行中に外部からシーケンスのインスタンスへ操作が必要な場合は、下記のようにシーケンスに対して`runScoped()`関数を使用して記述します。

```cpp
ExampleSequence exampleSequence{};
const auto taskRunner = exampleSequence.runScoped();

// シーケンスクラスのインスタンスへ操作可能(※一例)
exampleSequence.setText(U"テキスト");
```

### コルーチン内から実行する場合
`Co::Play`関数でシーケンスを再生する`Co::Task`を取得し、これに対して`co_await`を使用します。

```cpp
Co::Task<void> ExampleTask()
{
    co_await Co::Play<ExampleSequence>(); // ExampleSequenceを再生し、完了まで待機します
}
```

上記の書き方では、シーケンスのインスタンスへ外部からアクセスすることはできません。  
もしタスク実行中に外部からシーケンスのインスタンスへ操作が必要な場合は、下記のようにシーケンスのインスタンスを生成した上で`play()`関数を使用して`Co::Task`を取得し、それをco_awaitに渡します。

```cpp
Co::Task<void> ExampleTask()
{
    ExampleSequence exampleSequence{};
    co_await exampleSequence.play();

    // シーケンスクラスのインスタンスへ操作可能(※一例)
    exampleSequence.setText(U"テキスト");
}
```

シーケンスクラスの同一インスタンスに対して再生できるのは1回のみです。同一インスタンスに対して複数回`play()`を呼び出すことは許可されていません。複数回再生しようとすると`s3d::Error`例外が送出されます。

### 注意点
シーケンス実行の際、シーケンスクラスの`start`関数を外部から直接呼び出すことは想定されていません。`start`関数を直接呼び出してタスク実行すると、それ以外の関数(`draw`関数等)に記述した処理が呼び出されずに実行されてしまいます。

このような意図しない呼び出しを防ぐために、`start`等の関数をオーバーライドする際はアクセス指定子をprivate(さらに継承させたい場合はprotected)にしておくことを推奨します。

```cpp
class ExampleSequence : public Co::SequenceBase<void>
{
private: // 基本的にprivateにしておくことを推奨
    Co::Task<void> start() override
    {
        co_return;
    }
};
```

## `Co::UpdaterSequenceBase<TResult>`クラス
毎フレーム実行される`update()`関数を持つシーケンスの基底クラスです。コルーチンを使用しないシーケンスを作成する際は、このクラスを継承します。

コルーチンを使用せずに作成した既存の処理をなるべく変更せずに移植したい場合や、毎フレームの処理を記述した方が都合が良いシーケンスを作成する場合に便利です。

なお、`Co::UpdaterSequenceBase`は`Co::SequenceBase`の派生クラスです。`Co::UpdaterSequenceBase`を継承した場合でもシーケンスの実行方法に違いはありません。

```cpp
class ExampleUpdaterSequence : public Co::UpdaterSequenceBase<void>
{
private:
    void update() override
    {
        // ここに毎フレームの処理を記述

        // シーケンスを終了させたい場合はrequestFinish関数を呼ぶ
        requestFinish();
    }

    void draw() const override
    {
        // ここに毎フレームの描画処理を記述
    }

    Co::Task<void> fadeIn() override
    {
        // 必要に応じて、フェードイン処理をコルーチンで記述(updateと同時に実行される)
        co_await Co::SimpleFadeIn(1s, Palette::Black);
    }

    Co::Task<void> fadeOut() override
    {
        // 必要に応じて、フェードアウト処理をコルーチンで記述(updateの完了後に実行される)
        co_await Co::SimpleFadeOut(1s, Palette::Black);
    }

    Co::Task<void> preStart() override
    {
        // 必要に応じて、fadeIn・startより前に実行すべき処理(ローディングなど)があればコルーチンで記述
        co_return;
    }
};
```

### 仮想関数
下記の仮想関数をオーバーライドできます。`update()`のみ必須で、それ以外は必要な場合のみオーバーライドしてください。

- `update()`
    - 毎フレームの処理を記述します。
    - シーケンスを終了するには、`requestFinish()`関数を実行します。

- `draw() const`
    - シーケンスの描画処理を記述します。
    - この関数は、毎フレーム実行されます。
        - `start`関数内で`co_await`を使用して別のコルーチンの実行完了まで待機している間も、`draw`関数は実行され続けます。

- `drawIndex() const` -> `int32`
    - 描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番と同じ順序で描画されます。

- `fadeIn()` -> `Co::Task<void>`
    - シーン開始時に実行されるフェードイン用のコルーチンです。
    - `update()`と同時に実行されます。もし同時に実行したくない場合は、`update`関数内で`isFadingIn()`がtrueの場合は処理をスキップするなどしてください。
    - `fadeIn()`の実行が完了する前にシーン処理および`fadeOut()`が実行完了した場合、`fadeIn()`の実行は途中で終了されます。

- `fadeOut()` -> `Co::Task<void>`
    - シーン終了時に実行されるフェードアウト用のコルーチンです。
    - `update()`内でシーン終了の関数が呼ばれた後に実行されます。

- `preStart()` -> `Co::Task<void>`
    - `update()`および`fadeIn()`より前に呼び出されるコルーチンです。
    - ローディング処理を複数フレームにわたって実行する場合など、フェードイン開始より前に何か処理を実行したい場合に使用します。
    - Tips: 変数の初期化等のコルーチンで実装する必要がない処理は、`preStart()`関数を使用せずシーケンスクラスのコンストラクタ内に処理を記述するのが良いでしょう。

- `preStartDraw() const`
    - `preStart()`の実行中に毎フレーム呼び出される描画処理です。

- `preStartDrawIndex()` -> `int32`
    - `preStart()`の実行中の描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番で描画されます。

- `postFadeOut()` -> `Co::Task<void>`
    - `fadeOut()`より後に呼び出されるコルーチンです。
    - フェードアウト後に何か処理を実行したい場合に使用します。

- `postFadeOutDraw() const`
    - `postFadeOut()`の実行中に毎フレーム呼び出される描画処理です。

- `postFadeOutDrawIndex()` -> `int32`
    - `postFadeOut()`の実行中の描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番で描画されます。

- `postFadeOut()` -> `Co::Task<void>`
    - `fadeOut()`より後に呼び出されるコルーチンです。
    - フェードアウト後に何か処理を実行したい場合に使用します。

- `postFadeOutDraw() const`
    - `postFadeOut()`の実行中に毎フレーム呼び出される描画処理です。

- `postFadeOutDrawIndex()` -> `int32`
    - `postFadeOut()`の実行中の描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番で描画されます。

### update関数からシーケンスを完了するには
`update()`関数内で`requestFinish()`関数を実行し、シーケンスを完了できます。
`requestFinish()`関数の引数には`TResult`型の結果を指定します。`TResult`が`void`型の場合、引数は不要です。

```cpp
void update() override
{
    if (...) // 何らかの条件
    {
        requestFinish();
        return;
    }
}
```

なお、`requestFinish()`関数が複数回呼ばれた場合、1回目の呼び出しで受け取った結果のみが使用され、2回目以降の呼び出し分は無視されます。

## `Co::SceneBase`クラス
シーンの基底クラスです。シーンを実装するには、このクラスを継承します。

シーンとは、例えばタイトル画面・ゲーム画面・リザルト画面など、ゲームの大まかな画面を表す単位です。

シーン機能を使用しなくてもゲームを作成することは可能ですが、シーン機能を使用することで画面間の遷移を簡単に実装することができます。

大まかにはシーケンスにシーン遷移のための機能が付いたもので、基本的な使用方法はシーケンスと同じです。

```cpp
class ExampleScene : public Co::SceneBase
{
private:
    Co::Task<void> start() override
    {
        // ここに処理をコルーチンで記述

        // EnterキーかEscキーを押すまで待機
        const auto [isEnter, isEsc] = co_await Co::Any(
            Co::WaitForDown(KeyEnter),
            Co::WaitForDown(KeyEsc));

        if (isEnter)
        {
            // Enterキーを押したらゲームシーンへ遷移
            requestNextScene<GameScene>();
            co_return;
        }
        else
        {
            // Escキーを押したらシーン遷移を終了
            co_return;
        }
    }

    void draw() const override
    {
        // ここに毎フレームの描画処理を記述
    }

    Co::Task<void> fadeIn() override
    {
        // 必要に応じて、フェードイン処理をコルーチンで記述(startと同時に実行される)
        co_await Co::ScreenFadeIn(1s, Palette::Black);
    }

    Co::Task<void> fadeOut() override
    {
        // 必要に応じて、フェードアウト処理をコルーチンで記述(startの完了後に実行される)
        co_await Co::ScreenFadeOut(1s, Palette::Black);
    }
};
```

### 仮想関数
下記の仮想関数をオーバーライドできます。`start()`のみ必須で、それ以外は必要な場合のみオーバーライドしてください。

- `start()` -> `Co::Task<void>`
    - シーン開始時に実行されるコルーチンです。

- `draw() const`
    - シーンの描画処理を記述します。
    - この関数は、毎フレーム実行されます。
        - `start`関数内で`co_await`を使用して別のコルーチンの実行完了まで待機している間も、`draw`関数は実行され続けます。

- `drawIndex() const` -> `int32`
    - 描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番で描画されます。

- `fadeIn()` -> `Co::Task<void>`
    - シーン開始時に実行されるフェードイン用のコルーチンです。
    - `start()`と同時に実行されます。もし同時に実行したくない場合は、`start()`内の先頭で`co_await waitForFadeIn()`を実行し、フェードイン完了まで待機してください。
    - `fadeIn()`の実行が完了する前に`start()`および`fadeOut()`が実行完了した場合、`fadeIn()`の実行は途中で終了されます。

- `fadeOut()` -> `Co::Task<void>`
    - シーン終了時に実行されるフェードアウト用のコルーチンです。
    - `start()`の完了後に実行されます。
    - Tips: フェードアウトは必ずしも`fadeOut()`関数内に実装する必要はありません。遷移先のシーンごとにフェードアウトの方法を変えたい場合など、`fadeOut()`関数を使用せずに`start()`関数内でフェードアウトを実行した方がシンプルに実装できる場合があります。

- `preStart()` -> `Co::Task<void>`
    - `start()`および`fadeIn()`より前に呼び出されるコルーチンです。
    - ローディング処理を複数フレームにわたって実行する場合など、フェードイン開始より前に何か処理を実行したい場合に使用します。
    - Tips: 変数の初期化等のコルーチンで実装する必要がない処理は、`preStart()`関数を使用せずシーンクラスのコンストラクタ内に処理を記述するのが良いでしょう。

- `preStartDraw() const`
    - `preStart()`の実行中に毎フレーム呼び出される描画処理です。

- `preStartDrawIndex()` -> `int32`
    - `preStart()`の実行中の描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番で描画されます。

- `postFadeOut()` -> `Co::Task<void>`
    - `fadeOut()`より後に呼び出されるコルーチンです。
    - フェードアウト後に何か処理を実行したい場合に使用します。

- `postFadeOutDraw() const`
    - `postFadeOut()`の実行中に毎フレーム呼び出される描画処理です。

- `postFadeOutDrawIndex()` -> `int32`
    - `postFadeOut()`の実行中の描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番で描画されます。

### シーンの実行方法
`Co::EnterScene`関数を開始シーンの型を指定して呼び出すことで、最初のシーンから最後のシーンまでの一連の動作を実行する`Co::Task`を取得できます。これに対して通常通り、`runScoped`関数を使用します。  
もし開始シーンのクラスのコンストラクタに引数が必要な場合、`Co::EnterScene`関数の引数として渡すことができます。

全てのシーンが終了した場合にプログラムを終了するためには、下記のように`isFinished()`関数でタスクの完了を確認してwhileループを抜けます。

```cpp
const auto taskRunner = Co::EnterScene<ExampleScene>().runScoped();
while (System::Update())
{
    if (taskRunner.isFinished())
    {
        break;
    }
}
```

### Siv3D標準のシーン機能との比較

下記の点が異なります。

- CoTaskLibのシーン機能には、SceneManagerのようなマネージャークラスがありません。遷移先シーンのクラスは直接`Co::EnterScene()`関数または`requestNextScene()`関数のテンプレート引数として指定するため、シーン名の登録などが必要ありません。
- CoTaskLibでは、シーンクラスのコンストラクタに引数を持たせることができます。そのため、遷移元のシーンから必要なデータを受け渡すことができます。
    - Siv3D標準のシーン機能にある`getData()`のような、シーン間でグローバルにデータを受け渡すための機能は提供していません。代わりに、シーンクラスのコンストラクタに引数を用意して受け渡してください。
- CoTaskLibのシーンクラスでは、毎フレーム実行されるupdate関数の代わりに、`start()`関数というコルーチン関数を実装します。
    - update関数を使用したい場合、`SceneBase`クラスの代わりに`UpdaterSceneBase`クラスを基底クラスとして使用するか(詳細は後述)、自前で`update()`関数を用意した上で`start()`関数内に`Co::ScopedUpdater updater{ [this] { update(); } };`のように記述して`update()`関数が毎フレーム実行されるようにするかのいずれかの方法を取ってください。※`ScopedUpdater`/`ScopedDrawer`について加筆予定

### 次のシーンの指定方法

次のシーンへ遷移するには、基底クラスに実装されている`requestNextScene()`関数を実行します。  
`requestNextScene()`関数は、`start()`関数をはじめ、それ以外のタイミングのコルーチン関数内やコンストラクタ内でも実行可能です。

- `requestNextScene<TScene>(...)`関数
    - シーンクラスを指定し、次のシーンへ遷移します。
    - `TScene`には次のシーンのクラスを指定します。`TScene`は`Co::SceneBase`の派生クラスである必要があります。
    - 引数には、`TScene`のコンストラクタの引数を指定します。
        - 指定した引数はそれぞれコピーされます。引数にコピー構築できない型が含まれる場合、コンパイルエラーとなります。

```cpp
Co::Task<void> start() override
{
    // ... (何らかの処理)

    switch (selectedMenuItem)
    {
    case MenuItem::Start:
        requestNextScene<GameScene>();
        co_return;
    
    case MenuItem::Option:
        requestNextScene<OptionScene>();
        co_return;

    case MenuItem::Exit:
        // 何も指定しなかった場合は最終シーンとして扱われる
        co_return;
    }
}
```

## `Co::UpdaterSceneBase`クラス

毎フレーム実行される`update()`関数を持つシーンの基底クラスです。コルーチンを使用しないシーンを作成する際は、このクラスを継承します。

Siv3D標準のシーン機能を使用して作成したシーンをなるべく変更せずに移植したい場合や、毎フレームの処理を記述した方が都合が良いシーンを作成する場合に便利です。

なお、`Co::UpdaterSceneBase`は`Co::SceneBase`の派生クラスです。`Co::UpdaterSceneBase`を継承した場合でもシーンの実行方法に違いはありません。

```cpp
class ExampleUpdaterScene : public Co::SceneBase
{
private:
    void update() override
    {
        // ここに毎フレームの処理を記述

        if (KeyEnter.down())
        {
            // Enterキーを押したらゲームシーンへ遷移
            requestNextScene<GameScene>();
            return;
        }

        if (KeyEsc.down())
        {
            // Escキーを押したらシーン遷移を終了
            requestSceneFinish();
            return;
        }
    }

    void draw() const override
    {
        // ここに毎フレームの描画処理を記述
    }

    Co::Task<void> fadeIn() override
    {
        // 必要に応じて、フェードイン処理をコルーチンで記述(updateと同時に実行される)
        co_await Co::ScreenFadeIn(1s, Palette::Black);
    }

    Co::Task<void> fadeOut() override
    {
        // 必要に応じて、フェードアウト処理をコルーチンで記述(updateの完了後に実行される)
        co_await Co::ScreenFadeOut(1s, Palette::Black);
    }

    Co::Task<void> preStart() override
    {
        // 必要に応じて、fadeIn・startより前に実行すべき処理(ローディングなど)があればコルーチンで記述
        co_return;
    }
};
```

### 仮想関数
下記の仮想関数をオーバーライドできます。`update()`のみ必須で、それ以外は必要な場合のみオーバーライドしてください。

- `update()`
    - 毎フレームの処理を記述します。

- `draw() const`
    - シーンの描画処理を記述します。
    - この関数は、毎フレーム実行されます。
        - `start`関数内で`co_await`を使用して別のコルーチンの実行完了まで待機している間も、`draw`関数は実行され続けます。

- `drawIndex() const` -> `int32`
    - 描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番で描画されます。

- `fadeIn()` -> `Co::Task<void>`
    - シーン開始時に実行されるフェードイン用のコルーチンです。
    - `update()`と同時に実行されます。もし同時に実行したくない場合は、`update`関数内で`isFadingIn()`がtrueの場合は処理をスキップするなどしてください。
    - `fadeIn()`の実行が完了する前にシーン処理および`fadeOut()`が実行完了した場合、`fadeIn()`の実行は途中で終了されます。

- `fadeOut()` -> `Co::Task<void>`
    - シーン終了時に実行されるフェードアウト用のコルーチンです。
    - `update()`内でシーン終了の関数が呼ばれた後に実行されます。

- `preStart()` -> `Co::Task<void>`
    - `update()`および`fadeIn()`より前に呼び出されるコルーチンです。
    - ローディング処理を複数フレームにわたって実行する場合など、フェードイン開始より前に何か処理を実行したい場合に使用します。
    - Tips: 変数の初期化等のコルーチンで実装する必要がない処理は、`preStart()`関数を使用せずシーンクラスのコンストラクタ内に処理を記述するのが良いでしょう。

- `preStartDraw() const`
    - `preStart()`の実行中に毎フレーム呼び出される描画処理です。

- `preStartDrawIndex()` -> `int32`
    - `preStart()`の実行中の描画順序のソート値(drawIndex)を設定します。
    - デフォルト値は0です。drawIndexが同一のもの同士は、実行を開始した順番で描画されます。


### シーンを完了するには
シーンを終了して次のシーンへ遷移するには、基底クラスに実装されている下記のいずれかの関数を実行します。

- `requestNextScene<TScene>(...)`関数
    - シーンクラスを指定し、次のシーンへ遷移します。
    - `TScene`には次のシーンのクラスを指定します。`TScene`は`Co::SceneBase`の派生クラスである必要があります。
    - 引数には、`TScene`のコンストラクタの引数を指定します。
        - 指定した引数はそれぞれコピーされます。引数にコピー構築できない型が含まれる場合、コンパイルエラーとなります。
- `requestSceneFinish()`関数
    - シーンを完了し、次のシーンへ遷移しません。

これらの関数は、`update()`関数(またはコンストラクタ)内で呼び出されることが想定されています。それ以降のタイミング(`fadeOut()`関数等)で呼び出すことにより次シーンを上書きすることも可能ですが、その前提として`update()`のループ実行を終了させておく必要があるため、一度`update()`関数(またはコンストラクタ)内で呼び出しておく必要があります。

```cpp
void update() override
{
    // ... (何らかの処理)

    switch (selectedMenuItem)
    {
    case MenuItem::Start:
        requestNextScene<GameScene>();
        break;
    
    case MenuItem::Option:
        requestNextScene<OptionScene>();
        break;

    case MenuItem::Exit:
        requestSceneFinish();
        break;
    }
}
```

## イージング
`Co::Ease<T>()`および`Co::LinearEase<T>()`関数を使うと、ある値からある値へ滑らかに値を推移させるタスクを実行できます。

```cpp
class EaseExample : public Co::SequenceBase<void>
{
private:
    Vec2 m_position;

    Co::Task<void> start() override
    {
        // 3秒かけて(100,100)から(700,500)へ推移させる。その値を毎フレームm_positionへ代入
        co_await Co::Ease<Vec2>(&m_position, 3s)
            .from(100, 100)
            .to(700, 500)
            .play();
    }

    void draw() const override
    {
        Circle{ m_position, 100 }.draw();
    }
};
```

`Co::Ease<T>()`および`Co::LinearEase<T>()`関数は、`Co::EaseTaskBuilder<T>`というクラスのインスタンスを返します。  
これに対して、下記のメンバ関数をメソッドチェインで繋げて使用します。

- `duration(Duration)` -> `Co::EaseTaskBuilder<T>&`
    - 時間の長さを指定します。
    - この関数の代わりに、`Co::Ease()`の第1引数に指定することもできます。
- `from(T)`/`to(T)` -> `Co::EaseTaskBuilder<T>&`
    - 開始値・目標値を指定します。
    - 引数には、T型の値を指定するか、T型のコンストラクタ引数を指定します。
    - この関数の代わりに、from・toの値をそれぞれ`Co::Ease()`の第2・第3引数に指定することもできます。なお、その場合`Co::Ease<T>()`の`<T>`は記述を省略できます。
    - Tが浮動小数点型(double、float等)の場合は、この関数を呼ばなくてもデフォルトでfromに0.0、toに1.0が指定されます。
- `fromTo(T, T)` -> `Co::EaseTaskBuilder<T>&`
    - 開始値・目標値をまとめて指定します。
- `setEase(double(*)(double))` -> `Co::EaseTaskBuilder<T>&`
    - 値の補間に使用するイージング関数を指定します。
    - Siv3Dに用意されているイージング関数を関数名で指定できます(例:`.setEase(EaseInOutExpo)`)。
    - デフォルトでは下記のイージング関数が指定されています。
        - `Co::Ease()`: `EaseOutQuad`(目標値にやや早めに近づく曲線的な動き)
        - `Co::LinearEase()`: `Easing::Linear`(直線的な動き)
    - この関数の代わりに、`Co::Ease()`の第4引数に指定することもできます。
- `play()` -> `Co::Task<void>`
    - イージングを再生するタスクを取得します。

## 文字送り
`Co::Typewriter()`関数を使うと、1文字ずつ文字表示する処理が簡単に実装できます。

```cpp
class TypewriterExample : public Co::SequenceBase<void>
{
private:
    Font m_font{ 30 };
    String m_text;

    Co::Task<void> start() override
    {
        // テキストを1文字ずつ表示
        co_await Co::Typewriter(&m_text, 50ms, U"Hello, CoTaskLib!").play();

        // クリックされるまで待つ
        co_await Co::WaitForDown(MouseL);
    }

    void draw() const override
    {
        m_font(m_text).draw();
    }
};
```

`Co::Typewriter()`関数は、`Co::TypewriterTaskBuilder`というクラスのインスタンスを返します。これに対して、下記のメンバ関数をメソッドチェインで繋げて使用します。

- `oneLetterDuration(Duration)` -> `Co::TypewriterTaskBuilder&`
    - 表示時間を1文字あたりの時間で指定します。
    - この関数の代わりに、`Co::Typewriter()`の第2引数に指定することもできます。
- `totalDuration(Duration)` -> `Co::TypewriterTaskBuilder&`
    - 表示時間を文字列全体の時間で指定します。
- `text(StringView)` -> `Co::TypewriterTaskBuilder&`
    - 表示するテキストを指定します。
    - この関数の代わりに、`Co::Typewriter()`の第3引数に指定することもできます。
- `play()` -> `Co::Task<void>`
    - 文字送りを再生するタスクを取得します。

## 関数一覧
- `Co::Init()`
    - CoTaskLibライブラリを初期化します。
    - ライブラリの機能を使用する前に、必ず一度だけ実行してください。
- `Co::DelayFrame()` -> `Co::Task<void>`
    - 1フレーム待機します。
- `Co::DelayFrame(int32)` -> `Co::Task<void>`
    - 指定されたフレーム数だけ待機します。
- `Co::Delay(Duration)` -> `Co::Task<void>`
    - 指定された時間だけ待機します。
- `Co::WaitForever()` -> `Co::Task<void>`
    - 永久に待機します。
    - Tips: 終了しないシーケンスの`start()`関数に使用できます。
- `Co::WaitUntil(std::function<bool()>)` -> `Co::Task<void>`
    - 指定された関数を毎フレーム実行し、結果がfalseの間、待機します。
- `Co::WaitWhile(std::function<bool()>)` -> `Co::Task<void>`
    - 指定された関数を毎フレーム実行し、結果がtrueの間、待機します。
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
- `Co::ScreenFadeIn(Duration, ColorF)` -> `Co::Task<void>`
    - 指定色からの画面フェードインを開始し、完了まで待機します。
    - 任意で、第3引数にint32型で描画順序のソート値(drawIndex)を指定することもできます。
        - デフォルト値は`Co::DrawIndex::FadeIn`(=200000)で、通常描画(0)よりも手前に表示されるようになっています。
- `Co::ScreenFadeOut(Duration, ColorF)` -> `Co::Task<void>`
    - 指定色への画面フェードアウトを開始し、完了まで待機します。
    - 任意で、第3引数にint32型で描画順序のソート値(drawIndex)を指定することもできます。
        - デフォルト値は`Co::DrawIndex::FadeOut`(=300000)で、通常描画(0)よりも手前に表示されるようになっています。
- `Co::All(TTasks&&...)` -> `Co::Task<std::tuple<...>>`
    - 全ての`Co::Task`が完了するまで待機します。各`Co::Task`の結果が`std::tuple`で返されます。
    - `Co::Task`の結果が`void`型の場合、`Co::VoidResult`型(空の構造体)に置換して返されます。
- `Co::Any(TTasks&&...)` -> `Co::Task<std::tuple<Optional<...>>>`
    - いずれかの `Co::Task` が完了した時点で進行し、各`Co::Task`の結果が`Optional<T>`型の`std::tuple`で返されます。
    - `Co::Task`の結果が`void`型の場合、`Co::VoidResult`型(空の構造体)に置換して返されます。
- `Co::Play<TSequence>(...)` -> `Co::Task<TResult>`
    - `TSequence`クラスのインスタンスを構築し、それを実行するタスクを返します。
    - `TSequence`クラスは`Co::SequenceBase<TResult>`の派生クラスである必要があります。
    - 引数には、`TSequence`のコンストラクタの引数を指定します。
- `Co::EnterScene<TScene>(...)` -> `Co::Task<void>`
    - `TScene`クラスのインスタンスを構築し、それを開始シーンとした一連のシーン実行のタスクを返します。
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

    const auto taskRunner = ShowMessages().runScoped();
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

    const auto mainTaskRunner = MainTask().runScoped();
    while (System::Update())
    {
    }
}
```
