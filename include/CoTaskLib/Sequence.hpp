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

#ifdef NO_COTASKLIB_USING
namespace cotasklib
#else
inline namespace cotasklib
#endif
{
	namespace Co
	{
		template <typename TResult = void>
		class [[nodiscard]] SequenceBase
		{
			static_assert(!std::is_reference_v<TResult>, "TResult must not be a reference type");
			static_assert(std::is_copy_constructible_v<TResult> || std::is_void_v<TResult>, "TResult must be copy constructible");
			static_assert(!std::is_const_v<TResult>, "TResult must not have 'const' qualifier");

		public:
			using result_type = TResult;
			using result_type_void_replaced = detail::VoidResultTypeReplace<TResult>;
			using finish_callback_type = FinishCallbackType<TResult>;

		private:
			bool m_onceRun = false;
			bool m_isPreStart = true;
			bool m_isFadingIn = false;
			bool m_isFadingOut = false;
			bool m_isPostFadeOut = false;
			Optional<result_type_void_replaced> m_result;

			[[nodiscard]]
			Task<void> startAndFadeOut()
			{
				if constexpr (std::is_void_v<TResult>)
				{
					co_await start();
					m_result.emplace();
					m_isFadingOut = true;
					co_await fadeOut();
					m_isFadingOut = false;
				}
				else
				{
					m_result = co_await start();
					m_isFadingOut = true;
					co_await fadeOut();
					m_isFadingOut = false;
				}
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
			virtual Task<TResult> start() = 0;

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
					co_await detail::Yield{};
				}
			}

		public:
			SequenceBase() = default;

			SequenceBase(const SequenceBase&) = delete;

			SequenceBase& operator=(const SequenceBase&) = delete;

			SequenceBase(SequenceBase&&) = default;

			SequenceBase& operator=(SequenceBase&&) = default;

			virtual ~SequenceBase() = default;

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

			[[nodiscard]]
			bool isPostFadeOut() const
			{
				return m_isPostFadeOut;
			}

			[[nodiscard]]
			Task<TResult> play()&
			{
				if (m_onceRun)
				{
					// 2回以上の実行は許可しないため例外を投げる
					throw Error{ U"Cannot play the same Sequence multiple times" };
				}
				m_onceRun = true;

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

				if constexpr (std::is_void_v<TResult>)
				{
					co_return;
				}
				else
				{
					// m_resultにはstartAndFadeOut内で結果が代入済みなのでそれを返す
					co_return *m_result;
				}
			}

			// 右辺値参照の場合はタスク実行中にthisがダングリングポインタになるため、使用しようとした場合はコンパイルエラーとする
			Task<TResult> play() && = delete;

			[[nodiscard]]
			ScopedTaskRunner playScoped(FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr)&
			{
				return play().runScoped(std::move(finishCallback), std::move(cancelCallback));
			}

			[[nodiscard]]
			ScopedTaskRunner playScoped(FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr) && = delete;

			void playAddTo(MultiScoped& ms, FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr)&
			{
				play().runAddTo(ms, std::move(finishCallback), std::move(cancelCallback));
			}

			void playAddTo(MultiScoped& ms, FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr) && = delete;

			[[nodiscard]]
			bool hasResult() const
			{
				return m_result.has_value();
			}

			[[nodiscard]]
			const result_type_void_replaced& result() const
			{
				return *m_result;
			}

			[[nodiscard]]
			const Optional<result_type_void_replaced>& resultOpt() const
			{
				return m_result;
			}
		};

		namespace detail
		{
			template <typename TSequence>
			concept SequenceConcept = std::derived_from<TSequence, SequenceBase<typename TSequence::result_type>>;

			template <typename TResult>
			[[nodiscard]]
			Task<TResult> SequencePtrToTask(std::unique_ptr<SequenceBase<TResult>> sequence)
			{
				co_return co_await sequence->play();
			}
		}

		template <detail::SequenceConcept TSequence, class... Args>
		[[nodiscard]]
		Task<typename TSequence::result_type> Play(Args&&... args)
		{
			std::unique_ptr<SequenceBase<typename TSequence::result_type>> sequence = std::make_unique<TSequence>(std::forward<Args>(args)...);
			return detail::SequencePtrToTask(std::move(sequence));
		}

		// 毎フレーム呼ばれるupdate関数を記述するタイプのシーケンス基底クラス
		template <typename TResult>
		class [[nodiscard]] UpdaterSequenceBase : public SequenceBase<TResult>
		{
		private:
			Optional<TResult> m_result;

		protected:
			void finish(const TResult& result)
			{
				m_result = result;
			}

		public:
			UpdaterSequenceBase() = default;

			UpdaterSequenceBase(const UpdaterSequenceBase&) = delete;

			UpdaterSequenceBase& operator=(const UpdaterSequenceBase&) = delete;

			UpdaterSequenceBase(UpdaterSequenceBase&&) = default;

			UpdaterSequenceBase& operator=(UpdaterSequenceBase&&) = default;

			virtual ~UpdaterSequenceBase() = default;

			[[nodiscard]]
			virtual Task<TResult> start() override final
			{
				while (!m_result)
				{
					update();
					co_await detail::Yield{};
				}
				co_return *m_result;
			}

			virtual void update() = 0;
		};

		// 毎フレーム呼ばれるupdate関数を記述するタイプのシーケンス基底クラス(void特殊化)
		template <>
		class [[nodiscard]] UpdaterSequenceBase<void> : public SequenceBase<void>
		{
		private:
			bool m_isFinishRequested = false;

		protected:
			void requestFinish()
			{
				m_isFinishRequested = true;
			}

		public:
			UpdaterSequenceBase() = default;

			[[nodiscard]]
			virtual Task<void> start() override final
			{
				while (!m_isFinishRequested)
				{
					update();
					co_await detail::Yield{};
				}
			}

			virtual void update() = 0;

			[[nodiscard]]
			bool isFinishRequested() const
			{
				return m_isFinishRequested;
			}
		};

#ifdef __cpp_deleted_function_with_reason
		template <detail::SequenceConcept TSequence>
		auto operator co_await(TSequence&& sequence) = delete("To co_await a Sequence, use Co::Play<TSequence>() instead.");

		template <detail::SequenceConcept TSequence>
		auto operator co_await(TSequence& sequence) = delete("To co_await a Sequence, use Co::Play<TSequence>() instead.");
#else
		template <detail::SequenceConcept TSequence>
		auto operator co_await(TSequence&& sequence) = delete;

		template <detail::SequenceConcept TSequence>
		auto operator co_await(TSequence& sequence) = delete;
#endif

		template <detail::SequenceConcept TSequence>
		class [[nodiscard]] ScopedSequencePlayer : public detail::IScoped
		{
		public:
			using result_type = typename TSequence::result_type;
			using result_type_void_replaced = detail::VoidResultTypeReplace<result_type>;

		private:
			ScopedTaskRunner m_runner;

			TaskFinishSource<result_type> m_taskFinishSource;

		public:
			template <typename... Args>
			explicit ScopedSequencePlayer(Args&&... args) requires !std::is_void_v<result_type>
				: m_runner(Play<TSequence>(std::forward<Args>(args)...).runScoped([this](const result_type& result) { m_taskFinishSource.requestFinish(result); }))
			{
			}

			template <typename... Args>
			explicit ScopedSequencePlayer(Args&&... args) requires std::is_void_v<result_type>
				: m_runner(Play<TSequence>(std::forward<Args>(args)...).runScoped([this] { m_taskFinishSource.requestFinish(); }))
			{
			}

			ScopedSequencePlayer(const ScopedSequencePlayer&) = delete;

			ScopedSequencePlayer& operator=(const ScopedSequencePlayer&) = delete;

			ScopedSequencePlayer(ScopedSequencePlayer&&) = default;

			ScopedSequencePlayer& operator=(ScopedSequencePlayer&&) = default;

			~ScopedSequencePlayer() = default;

			[[nodiscard]]
			bool isFinished() const
			{
				return m_runner.isFinished();
			}

			void forget()
			{
				m_runner.forget();
			}

			[[nodiscard]]
			bool hasResult() const
			{
				return m_taskFinishSource.hasResult();
			}

			[[nodiscard]]
			const result_type_void_replaced& result() const
			{
				return m_taskFinishSource.result();
			}

			[[nodiscard]]
			const Optional<result_type_void_replaced>& resultOpt() const
			{
				return m_taskFinishSource.resultOpt();
			}

			[[nodiscard]]
			Task<result_type_void_replaced> waitForFinish() const
			{
				return m_taskFinishSource.waitForFinish();
			}

			void addTo(MultiScoped& ms) && override
			{
				ms.add(std::move(*this));
			}
		};
	}
}
