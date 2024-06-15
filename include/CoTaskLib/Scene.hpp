//----------------------------------------------------------------------------------------
//
//  CoTaskLib
//
//  Copyright (c) 2024 masaka
//
//  Licensed under the MIT License.
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.
//
//----------------------------------------------------------------------------------------

#pragma once
#include "Core.hpp"

namespace cotasklib
{
	namespace Co
	{
		namespace detail
		{
			template <typename T>
			concept IsBasicStringView = std::is_same_v<T, std::basic_string_view<typename T::value_type, typename T::traits_type>>;

			template <typename T>
			concept IsSpan = std::is_same_v<T, std::span<typename T::value_type>>;
		}

		template <detail::SceneConcept TScene, typename... Args>
		[[nodiscard]]
		SceneFactory MakeSceneFactory(Args&&... args)
		{
			// Args...はコピー構築可能である必要がある
			static_assert((std::is_copy_constructible_v<Args> && ...),
				"Scene constructor arguments must be copy-constructible.");

			// std::basic_string_viewは許可しない
			static_assert(!(detail::IsBasicStringView<Args> || ...),
				"std::basic_string_view is not allowed as a scene constructor argument because it is not deep-copied, risking dangling references. Use std::basic_string instead.");

			// StringViewは許可しない
			static_assert(!(std::is_same_v<std::decay_t<Args>, StringView> || ...),
				"StringView is not allowed as a scene constructor argument because it is not deep-copied, risking dangling references. Use String instead.");

			// std::spanは許可しない
			static_assert(!(detail::IsSpan<Args> || ...),
				"std::span is not allowed as a scene constructor argument because it is not deep-copied, risking dangling references. Use std::vector or similar.");

			return [=] { return std::make_unique<TScene>(args...); };
		}

		class [[nodiscard]] SceneBase
		{
		private:
			bool m_isPreStart = true;
			bool m_isFadingIn = false;
			bool m_isFadingOut = false;
			bool m_isPostFadeOut = false;

			TaskFinishSource<SceneFactory> m_taskFinishSource;

			[[nodiscard]]
			Task<void> startAndFadeOut()
			{
				co_await start();
				m_isFadingOut = true;
				co_await fadeOut();
				m_isFadingOut = false;
			}

			[[nodiscard]]
			Task<void> fadeInInternal()
			{
				m_isFadingIn = true;
				co_await fadeIn();
				m_isFadingIn = false;
			}

		protected:
			[[nodiscard]]
			virtual Task<void> preStart()
			{
				co_return;
			}

			virtual void preStartDraw() const
			{
			}

			[[nodiscard]]
			virtual int32 preStartDrawIndex() const
			{
				return DrawIndex::Default;
			}

			[[nodiscard]]
			virtual Task<void> start() = 0;

			virtual void draw() const
			{
			}

			[[nodiscard]]
			virtual int32 drawIndex() const
			{
				return DrawIndex::Default;
			}

			[[nodiscard]]
			virtual Task<void> fadeIn()
			{
				co_return;
			}

			[[nodiscard]]
			virtual Task<void> fadeOut()
			{
				co_return;
			}

			[[nodiscard]]
			virtual Task<void> postFadeOut()
			{
				co_return;
			}

			virtual void postFadeOutDraw() const
			{
			}

			[[nodiscard]]
			virtual int32 postFadeOutDrawIndex() const
			{
				return DrawIndex::Default;
			}

			[[nodiscard]]
			Task<void> waitForFadeIn()
			{
				if (m_isPreStart)
				{
					throw Error{ U"waitForFadeIn() must not be called in preStart()" };
				}

				if (m_isPostFadeOut)
				{
					throw Error{ U"waitForFadeIn() must not be called in postFadeOut()" };
				}

				while (m_isFadingIn)
				{
					co_await NextFrame();
				}
			}

			template <class TScene, typename... Args>
			bool requestNextScene(Args&&... args)
			{
				return m_taskFinishSource.requestFinish(MakeSceneFactory<TScene>(std::forward<Args>(args)...));
			}

			bool requestNextScene(SceneFactory sceneFactory)
			{
				return m_taskFinishSource.requestFinish(std::move(sceneFactory));
			}

			bool requestSceneFinish()
			{
				return m_taskFinishSource.requestFinish(nullptr);
			}

			[[nodiscard]]
			bool isRequested() const
			{
				return m_taskFinishSource.done();
			}

		public:
			SceneBase() = default;

			// thisポインタをキャプチャするためコピー・ムーブ禁止
			SceneBase(const SceneBase&) = delete;
			SceneBase& operator=(const SceneBase&) = delete;
			SceneBase(SceneBase&&) = delete;
			SceneBase& operator=(SceneBase&&) = delete;

			virtual ~SceneBase() = default;

			[[nodiscard]]
			bool isPreStart() const
			{
				return m_isPreStart;
			}

			[[nodiscard]]
			bool isFadingIn() const
			{
				return m_isFadingIn;
			}

			[[nodiscard]]
			bool isFadingOut() const
			{
				return m_isFadingOut;
			}

			// ライブラリ内部で使用するためのタスク実行関数
			[[nodiscard]]
			Task<SceneFactory> playInternal()&
			{
				{
					const ScopedDrawer drawer{ [this] { preStartDraw(); }, [this] { return preStartDrawIndex(); } };
					m_isPreStart = true;
					co_await preStart();
					m_isPreStart = false;
				}

				{
					const ScopedDrawer drawer{ [this] { draw(); }, [this] { return drawIndex(); } };

					// start内の先頭でwaitForFadeInを呼んだ場合に正常に待てるよう、先にfadeInTaskを生成して初回resumeでm_isFadingInをtrueにしている点に注意
					auto fadeInTask = fadeInInternal();
					co_await startAndFadeOut().with(std::move(fadeInTask), WithTiming::Before);
				}

				{
					const ScopedDrawer drawer{ [this] { postFadeOutDraw(); }, [this] { return postFadeOutDrawIndex(); } };
					m_isPostFadeOut = true;
					co_await postFadeOut();
					m_isPostFadeOut = false;
				}

				if (m_taskFinishSource.hasResult())
				{
					co_return m_taskFinishSource.result();
				}
				else
				{
					co_return nullptr;
				}
			}

			// 右辺値参照の場合はタスク実行中にthisがダングリングポインタになるため、使用しようとした場合はコンパイルエラーとする
			Task<SceneFactory> playInternal() && = delete;
		};

		// 毎フレーム呼ばれるupdate関数を記述するタイプのシーン基底クラス
		class [[nodiscard]] UpdaterSceneBase : public SceneBase
		{
		public:
			UpdaterSceneBase() = default;

			UpdaterSceneBase(const UpdaterSceneBase&) = delete;

			UpdaterSceneBase& operator=(const UpdaterSceneBase&) = delete;

			UpdaterSceneBase(UpdaterSceneBase&&) = default;

			UpdaterSceneBase& operator=(UpdaterSceneBase&&) = default;

			virtual ~UpdaterSceneBase() = default;

			[[nodiscard]]
			virtual Task<void> start() override final
			{
				if (isRequested())
				{
					// コンストラクタやpreStart内で次シーンが指定済みの場合は即座に終了
					co_return;
				}

				// 次シーンが指定されるまでループ
				while (true)
				{
					update();
					if (isRequested())
					{
						co_return;
					}
					co_await NextFrame();
				}
			}

			virtual void update() = 0;
		};

		namespace detail
		{
			[[nodiscard]]
			inline Task<void> ScenePtrToTask(std::unique_ptr<SceneBase> scene)
			{
				std::unique_ptr<SceneBase> currentScene = std::move(scene);

				while (true)
				{
					const SceneFactory nextSceneFactory = co_await currentScene->playInternal();

					// 次シーンがなければ抜ける
					if (nextSceneFactory == nullptr)
					{
						Backend::SetCurrentSceneFactory(nullptr);
						break;
					}

					// 次シーンを生成
					currentScene.reset();
					Backend::SetCurrentSceneFactory(nextSceneFactory); // 次シーンのコンストラクタ内で参照される場合があるためシーン生成前にセットしておく必要がある
					currentScene = nextSceneFactory();
					if (!currentScene)
					{
						Backend::SetCurrentSceneFactory(nullptr);
						break;
					}
				}
			}
		}

		template <detail::SceneConcept TScene, class... Args>
		[[nodiscard]]
		Task<void> PlaySceneFrom(Args&&... args)
		{
			return detail::ScenePtrToTask(std::make_unique<TScene>(std::forward<Args>(args)...));
		}

		inline Task<void> PlaySceneFrom(SceneFactory sceneFactory)
		{
			auto scenePtr = sceneFactory();
			return detail::ScenePtrToTask(std::move(scenePtr));
		}

#ifdef __cpp_deleted_function_with_reason
		template <detail::SceneConcept TScene>
		auto operator co_await(TScene&& scene) = delete("To co_await a Scene, use Co::PlaySceneFrom<TScene>() instead.");

		template <detail::SceneConcept TScene>
		auto operator co_await(TScene& scene) = delete("To co_await a Scene, use Co::PlaySceneFrom<TScene>() instead.");

		auto operator co_await(SceneFactory sceneFactory) = delete("To co_await a Scene created by a SceneFactory, use Co::PlaySceneFrom(SceneFactory) instead.");
#else
		template <detail::SceneConcept TScene>
		auto operator co_await(TScene&& scene) = delete;

		template <detail::SceneConcept TScene>
		auto operator co_await(TScene& scene) = delete;

		auto operator co_await(SceneFactory sceneFactory) = delete;
#endif
	}
}

#ifndef NO_COTASKLIB_USING
using namespace cotasklib;
#endif
