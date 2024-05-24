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
		template <typename TResult>
		class [[nodiscard]] SequenceBase
		{
			static_assert(!std::is_reference_v<TResult>, "TResult must not be a reference type");
			static_assert(std::is_copy_constructible_v<TResult> || std::is_void_v<TResult>, "TResult must be copy constructible");
			static_assert(!std::is_const_v<TResult>, "TResult must not have 'const' qualifier");

		public:
			using result_type = TResult;
			using result_type_void_replaced = detail::VoidResultTypeReplace<TResult>;

		private:
			bool m_onceRun = false;
			bool m_isPreStart = true;
			bool m_isFadingIn = false;
			bool m_isFadingOut = false;
			bool m_isPostFadeOut = false;
			std::optional<result_type_void_replaced> m_result;

			[[nodiscard]]
			Task<void> startAndFadeOut()
			{
				if constexpr (std::is_void_v<TResult>)
				{
					co_await start();
					m_result.emplace();
					m_isFadingOut = true;
					co_await fadeOut();
				}
				else
				{
					m_result = co_await start();
					m_isFadingOut = true;
					co_await fadeOut();
				}
			}

		protected:
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
				return 0;
			}

			[[nodiscard]]
			virtual Task<TResult> start() = 0;

			virtual void draw() const
			{
			}

			[[nodiscard]]
			virtual int32 drawIndex() const
			{
				return 0;
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
				return 0;
			}

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
					ScopedDrawer drawer{ [this] { preStartDraw(); }, [this] { return preStartDrawIndex(); } };
					co_await preStart();
				}

				m_isPreStart = false;
				m_isFadingIn = true;

				{
					ScopedDrawer drawer{ [this] { draw(); }, [this] { return drawIndex(); } };
					co_await startAndFadeOut()
						.with(fadeIn().then([this] { m_isFadingIn = false; }));
				}

				m_isFadingOut = false;
				m_isPostFadeOut = true;

				{
					ScopedDrawer drawer{ [this] { postFadeOutDraw(); }, [this] { return postFadeOutDrawIndex(); } };
					co_await postFadeOut();
				}

				m_isPostFadeOut = false;

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
			ScopedTaskRunner runScoped()&
			{
				return ScopedTaskRunner{ play() };
			}

			[[nodiscard]]
			ScopedTaskRunner runScoped() && = delete;

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
			const std::optional<result_type_void_replaced>& resultOpt() const
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

		template <detail::SequenceConcept TSequence>
		[[nodiscard]]
		Task<typename TSequence::result_type> ToTask(TSequence&& sequence)
		{
			std::unique_ptr<SequenceBase<typename TSequence::result_type>> sequence = std::make_unique<TSequence>(std::move(sequence));
			return detail::SequencePtrToTask(std::move(sequence));
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
			std::optional<TResult> m_result;

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
			bool m_isFinished = false;

		protected:
			void finish()
			{
				m_isFinished = true;
			}

		public:
			UpdaterSequenceBase() = default;

			[[nodiscard]]
			virtual Task<void> start() override final
			{
				while (!m_isFinished)
				{
					update();
					co_await detail::Yield{};
				}
			}

			virtual void update() = 0;
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
		class [[nodiscard]] ScopedSequenceRunner : public detail::IScoped
		{
		public:
			using result_type = typename TSequence::result_type;
			using result_type_void_replaced = detail::VoidResultTypeReplace<result_type>;

		private:
			ScopedTaskRunner m_runner;

			TaskFinishSource<result_type> m_taskFinishSource;

		public:
			template <typename... Args>
			explicit ScopedSequenceRunner(Args&&... args)
				: m_runner(Play<TSequence>(std::forward<Args>(args)...).then([this](const result_type_void_replaced& result) { m_taskFinishSource.requestFinish(result); }))
			{
			}

			ScopedSequenceRunner(const ScopedSequenceRunner&) = delete;

			ScopedSequenceRunner& operator=(const ScopedSequenceRunner&) = delete;

			ScopedSequenceRunner(ScopedSequenceRunner&&) = default;

			ScopedSequenceRunner& operator=(ScopedSequenceRunner&&) = default;

			~ScopedSequenceRunner() = default;

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
			const std::optional<result_type_void_replaced>& resultOpt() const
			{
				return m_taskFinishSource.resultOpt();
			}

			[[nodiscard]]
			Task<result_type_void_replaced> waitForFinish() const
			{
				return m_taskFinishSource.waitForFinish();
			}
		};
	}
}
