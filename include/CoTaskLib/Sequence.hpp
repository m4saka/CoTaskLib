//----------------------------------------------------------------------------------------
//
//  CoTaskLib
//
//  Copyright (c) 2024-2025 masaka
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

namespace cotasklib::Co
{
	template <typename TResult = void>
	class [[nodiscard]] SequenceBase : public detail::IDrawerInternal
	{
		static_assert(!std::is_reference_v<TResult>, "TResult must not be a reference type");
		static_assert(std::is_move_constructible_v<TResult> || std::is_void_v<TResult>, "TResult must be move constructible");
		static_assert(!std::is_const_v<TResult>, "TResult must not have 'const' qualifier");

	public:
		using result_type = TResult;
		using result_type_void_replaced = detail::VoidResultTypeReplace<TResult>;
		using finish_callback_type = FinishCallbackType<TResult>;

	private:
		Layer m_layer;
		int32 m_drawIndex;
		detail::ScopedDrawerInternal* m_pCurrentScopedDrawer = nullptr;
		bool m_onceRun = false;
		bool m_isPreStart = true;
		bool m_isFadingIn = false;
		bool m_isFadingOut = false;
		bool m_isPostFadeOut = false;
		bool m_isDone = false;
		TaskFinishSource<TResult> m_taskFinishSource;

		[[nodiscard]]
		Task<void> startAndFadeOut()
		{
			if constexpr (std::is_void_v<TResult>)
			{
				co_await start();
				m_taskFinishSource.requestFinish();
				m_isFadingOut = true;
				co_await fadeOut();
				m_isFadingOut = false;
			}
			else
			{
				TResult result = co_await start();
				m_taskFinishSource.requestFinish(std::move(result));
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

		void drawInternal() const override
		{
			if (m_isPreStart)
			{
				preStartDraw();
			}
			else if (m_isPostFadeOut)
			{
				postFadeOutDraw();
			}
			else
			{
				draw();
			}
		}

	protected:
		[[nodiscard]]
		virtual Task<void> preStart()
		{
			return EmptyTask();
		}

		virtual void preStartDraw() const
		{
		}

		[[nodiscard]]
		virtual Task<TResult> start() = 0;

		virtual void draw() const
		{
		}

		[[nodiscard]]
		virtual Task<void> fadeIn()
		{
			return EmptyTask();
		}

		[[nodiscard]]
		virtual Task<void> fadeOut()
		{
			return EmptyTask();
		}

		[[nodiscard]]
		virtual Task<void> postFadeOut()
		{
			return EmptyTask();
		}

		virtual void postFadeOutDraw() const
		{
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

		void setLayer(Layer layer)
		{
			m_layer = layer;
			if (m_pCurrentScopedDrawer)
			{
				m_pCurrentScopedDrawer->setLayer(layer);
			}
		}

		void setDrawIndex(int32 drawIndex)
		{
			m_drawIndex = drawIndex;
			if (m_pCurrentScopedDrawer)
			{
				m_pCurrentScopedDrawer->setDrawIndex(drawIndex);
			}
		}

	public:
		explicit SequenceBase(Layer layer = Layer::Default, int32 drawIndex = DrawIndex::Default)
			: m_layer(layer)
			, m_drawIndex(drawIndex)
		{
		}

		// thisポインタをキャプチャするためコピー・ムーブ禁止
		SequenceBase(const SequenceBase&) = delete;
		SequenceBase& operator=(const SequenceBase&) = delete;
		SequenceBase(SequenceBase&&) = delete;
		SequenceBase& operator=(SequenceBase&&) = delete;

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
		Layer layer() const
		{
			return m_layer;
		}

		[[nodiscard]]
		int32 drawIndex() const
		{
			return m_drawIndex;
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

			detail::ScopedDrawerInternal drawer{ this, m_layer, m_drawIndex, &m_pCurrentScopedDrawer };

			{
				m_isPreStart = true;
				co_await preStart();
				m_isPreStart = false;
			}

			co_await startAndFadeOut().with(fadeInInternal(), WithTiming::Before);

			{
				m_isPostFadeOut = true;
				co_await postFadeOut();
				m_isPostFadeOut = false;
			}

			m_isDone = true;

			if constexpr (std::is_void_v<TResult>)
			{
				co_return;
			}
			else
			{
				// m_resultにはstartAndFadeOut内で結果が代入済みなのでそれを返す
				co_return m_taskFinishSource.result();
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

		void playAddTo(MultiRunner& mr, FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr)&
		{
			play().runAddTo(mr, std::move(finishCallback), std::move(cancelCallback));
		}

		void playAddTo(MultiRunner& mr, FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr) && = delete;

		[[nodiscard]]
		bool done() const
		{
			return m_isDone;
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
	template <typename TResult = void>
	class [[nodiscard]] UpdaterSequenceBase : public SequenceBase<TResult>
	{
	private:
		TaskFinishSource<TResult> m_taskFinishSource;

		[[nodiscard]]
		virtual Task<TResult> start() override final
		{
			// コンストラクタやpreStart内でrequestFinishが呼ばれた場合は即座に終了
			if (m_taskFinishSource.hasResult())
			{
				co_return m_taskFinishSource.result();
			}

			while (true)
			{
				update();
				if (m_taskFinishSource.hasResult())
				{
					break;
				}
				co_await NextFrame();
			}
			co_return m_taskFinishSource.result();
		}

	protected:
		virtual void update() = 0;

		void requestFinish(const TResult& result)
		{
			m_taskFinishSource.requestFinish(result);
		}

		void requestFinish(TResult&& result)
		{
			m_taskFinishSource.requestFinish(std::move(result));
		}

		[[nodiscard]]
		bool finishRequested() const
		{
			return m_taskFinishSource.done();
		}

	public:
		explicit UpdaterSequenceBase(Layer layer = Layer::Default, int32 drawIndex = DrawIndex::Default)
			: SequenceBase<TResult>(layer, drawIndex)
		{
		}

		UpdaterSequenceBase(const UpdaterSequenceBase&) = delete;

		UpdaterSequenceBase& operator=(const UpdaterSequenceBase&) = delete;

		UpdaterSequenceBase(UpdaterSequenceBase&&) = default;

		UpdaterSequenceBase& operator=(UpdaterSequenceBase&&) = default;

		virtual ~UpdaterSequenceBase() = default;
	};

	// 毎フレーム呼ばれるupdate関数を記述するタイプのシーケンス基底クラス(void特殊化)
	template <>
	class [[nodiscard]] UpdaterSequenceBase<void> : public SequenceBase<void>
	{
	private:
		TaskFinishSource<void> m_taskFinishSource;

		[[nodiscard]]
		virtual Task<void> start() override final
		{
			if (m_taskFinishSource.done())
			{
				// コンストラクタやpreStart内でrequestFinishが呼ばれた場合は即座に終了
				co_return;
			}

			while (true)
			{
				update();
				if (m_taskFinishSource.done())
				{
					co_return;
				}
				co_await NextFrame();
			}
		}

	protected:
		virtual void update() = 0;

		void requestFinish()
		{
			m_taskFinishSource.requestFinish();
		}

		[[nodiscard]]
		bool finishRequested() const
		{
			return m_taskFinishSource.done();
		}

	public:
		explicit UpdaterSequenceBase(Layer layer = Layer::Default, int32 drawIndex = DrawIndex::Default)
			: SequenceBase<void>(layer, drawIndex)
		{
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
}

#ifndef NO_COTASKLIB_USING
using namespace cotasklib;
#endif
