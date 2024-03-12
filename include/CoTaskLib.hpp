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
#include <Siv3D.hpp>
#include <coroutine>

#ifdef NO_COTASKLIB_USING
namespace CoTaskLib
#else
inline namespace CoTaskLib
#endif
{
	namespace detail
	{
		enum class FrameTiming
		{
			Init,
			Update,
			Draw,
			PostPresent,
		};

		class IAwaiter
		{
		public:
			virtual ~IAwaiter() = default;

			virtual void resume(FrameTiming) = 0;

			virtual bool done() const = 0;
		};

		using AwaiterID = uint64;

		template <typename TResult>
		class CoTaskAwaiter;
	}

	template <typename TResult>
	class CoTask;

	namespace detail
	{
		class CoTaskBackend
		{
		private:
			static constexpr StringView AddonName{ U"CoTaskBackendAddon" };

			static inline CoTaskBackend* s_pInstance = nullptr;

			// Note: draw関数がconstであることの対処用にアドオンと実体を分離し、実体はポインタで持つようにしている
			class CoTaskBackendAddon : public IAddon
			{
			private:
				bool m_isFirstUpdated = false;
				std::unique_ptr<CoTaskBackend> m_instance;

			public:
				CoTaskBackendAddon()
					: m_instance{ std::make_unique<CoTaskBackend>() }
				{
					if (s_pInstance)
					{
						throw Error{ U"CoTaskBackendAddon: Instance already exists" };
					}
					s_pInstance = m_instance.get();
				}

				virtual ~CoTaskBackendAddon()
				{
					if (s_pInstance == m_instance.get())
					{
						s_pInstance = nullptr;
					}
				}

				virtual bool update() override
				{
					m_isFirstUpdated = true;
					m_instance->resume(FrameTiming::Update);
					return true;
				}

				virtual void draw() const override
				{
					if (!m_isFirstUpdated)
					{
						// Addonの初回drawがupdateより先に実行される挙動を回避
						return;
					}
					m_instance->resume(FrameTiming::Draw);
				}

				virtual void postPresent() override
				{
					if (!m_isFirstUpdated)
					{
						// Addonの初回postPresentがupdateより先に実行される挙動を回避
						return;
					}
					m_instance->resume(FrameTiming::PostPresent);
				}

				[[nodiscard]]
				CoTaskBackend* instance() const
				{
					return m_instance.get();
				}
			};

			FrameTiming m_currentFrameTiming = FrameTiming::Init;

			AwaiterID m_nextAwaiterID = 1;

			std::optional<AwaiterID> m_currentAwaiterID = none;

			std::map<AwaiterID, std::unique_ptr<IAwaiter>> m_awaiters;

		public:
			CoTaskBackend() = default;

			void resume(FrameTiming frameTiming)
			{
				m_currentFrameTiming = frameTiming;
				for (auto it = m_awaiters.begin(); it != m_awaiters.end();)
				{
					m_currentAwaiterID = it->first;

					it->second->resume(frameTiming);
					if (it->second->done())
					{
						it = m_awaiters.erase(it);
					}
					else
					{
						++it;
					}
				}
				m_currentAwaiterID = none;
			}

			static void Init()
			{
				Addon::Register(AddonName, std::make_unique<CoTaskBackendAddon>());
			}

			[[nodiscard]]
			static AwaiterID Add(std::unique_ptr<IAwaiter>&& awaiter)
			{
				if (!awaiter)
				{
					throw Error{ U"CoTask is nullptr" };
				}

				if (!s_pInstance)
				{
					throw Error{ U"CoTaskBackend is not initialized" };
				}
				const AwaiterID id = s_pInstance->m_nextAwaiterID++;
				s_pInstance->m_awaiters.emplace(id, std::move(awaiter));
				return id;
			}

			static void Remove(AwaiterID id)
			{
				if (!s_pInstance)
				{
					// Note: ユーザーがScopedTaskRunをstaticで持ってしまった場合にAddon解放後に呼ばれるケースが起こりうるので、ここでは例外を出さない
					return;
				}
				if (id == s_pInstance->m_currentAwaiterID)
				{
					throw Error{ U"CoTaskBackend::UnregisterTask: Cannot unregister the currently running task" };
				}
				s_pInstance->m_awaiters.erase(id);
			}

			[[nodiscard]]
			static bool IsDone(AwaiterID id)
			{
				if (!s_pInstance)
				{
					throw Error{ U"CoTaskBackend is not initialized" };
				}
				if (s_pInstance->m_awaiters.contains(id))
				{
					return s_pInstance->m_awaiters.at(id)->done();
				}
				else
				{
					return id < s_pInstance->m_nextAwaiterID;
				}
			}

			[[nodiscard]]
			static FrameTiming CurrentFrameTiming()
			{
				if (!s_pInstance)
				{
					throw Error{ U"CoTaskBackend is not initialized" };
				}
				return s_pInstance->m_currentFrameTiming;
			}
		};

		class ScopedTaskRunLifetime
		{
		private:
			std::optional<AwaiterID> m_id;

		public:
			explicit ScopedTaskRunLifetime(const std::optional<AwaiterID>& id)
				: m_id(id)
			{
			}

			ScopedTaskRunLifetime(const ScopedTaskRunLifetime&) = delete;

			ScopedTaskRunLifetime& operator=(const ScopedTaskRunLifetime&) = delete;

			ScopedTaskRunLifetime(ScopedTaskRunLifetime&& rhs) noexcept
				: m_id(rhs.m_id)
			{
				rhs.m_id = none;
			}

			ScopedTaskRunLifetime& operator=(ScopedTaskRunLifetime&& rhs) noexcept
			{
				if (this != &rhs)
				{
					if (m_id)
					{
						CoTaskBackend::Remove(m_id.value());
					}
					m_id = rhs.m_id;
					rhs.m_id = none;
				}
				return *this;
			}

			~ScopedTaskRunLifetime()
			{
				if (m_id)
				{
					CoTaskBackend::Remove(m_id.value());
				}
			}

			[[nodiscard]]
			bool done() const
			{
				return !m_id || CoTaskBackend::IsDone(m_id.value());
			}
		};

		template <typename TResult>
		std::optional<AwaiterID> ResumeAwaiterOnceAndRegisterIfNotDone(CoTaskAwaiter<TResult>&& awaiter)
		{
			if (awaiter.done())
			{
				// 既に終了済み
				return none;
			}

			awaiter.resume(CoTaskBackend::CurrentFrameTiming());
			if (awaiter.done())
			{
				// フレーム待ちなしで終了した場合は登録不要
				return none;
			}
			return CoTaskBackend::Add(std::make_unique<CoTaskAwaiter<TResult>>(std::move(awaiter)));
		}

		template <typename TResult>
		std::optional<AwaiterID> ResumeAwaiterOnceAndRegisterIfNotDone(const CoTaskAwaiter<TResult>& awaiter) = delete;
	}

	namespace Co
	{
		class ScopedTaskRun
		{
		private:
			detail::ScopedTaskRunLifetime m_lifetime;

		public:
			template <typename TResult>
			explicit ScopedTaskRun(detail::CoTaskAwaiter<TResult>&& awaiter)
				: m_lifetime(detail::ResumeAwaiterOnceAndRegisterIfNotDone(std::move(awaiter)))
			{
			}

			ScopedTaskRun(ScopedTaskRun&&) = default;

			ScopedTaskRun& operator=(ScopedTaskRun&&) = default;

			~ScopedTaskRun() = default;

			[[nodiscard]]
			bool done() const
			{
				return m_lifetime.done();
			}
		};
	}

	namespace detail
	{
		template <typename TResult>
		class Promise;

		template <typename TResult>
		class CoroutineHandleWrapper
		{
		private:
			using handle_type = std::coroutine_handle<Promise<TResult>>;

			handle_type m_handle;

		public:
			explicit CoroutineHandleWrapper(handle_type handle)
				: m_handle(std::move(handle))
			{
			}

			~CoroutineHandleWrapper()
			{
				if (m_handle)
				{
					m_handle.destroy();
				}
			}

			CoroutineHandleWrapper(const CoroutineHandleWrapper<TResult>&) = delete;

			CoroutineHandleWrapper& operator=(const CoroutineHandleWrapper<TResult>&) = delete;

			CoroutineHandleWrapper(CoroutineHandleWrapper<TResult>&& rhs) noexcept
				: m_handle(rhs.m_handle)
			{
				rhs.m_handle = nullptr;
			}

			CoroutineHandleWrapper& operator=(CoroutineHandleWrapper<TResult>&& rhs) noexcept
			{
				if (this != &rhs)
				{
					if (m_handle)
					{
						m_handle.destroy();
					}
					m_handle = rhs.m_handle;
					rhs.m_handle = nullptr;
				}
				return *this;
			}

			[[nodiscard]]
			TResult value() const
			{
				return m_handle.promise().value();
			}

			[[nodiscard]]
			bool done() const
			{
				return !m_handle || m_handle.done();
			}

			void resume(FrameTiming frameTiming) const
			{
				if (done())
				{
					return;
				}

				if (m_handle.promise().resumeSubAwaiter(frameTiming))
				{
					return;
				}

				if (m_handle.promise().nextResumeTiming() != frameTiming)
				{
					return;
				}

				m_handle.resume();
				m_handle.promise().rethrowIfException();
			}

			FrameTiming nextResumeTiming() const
			{
				return m_handle.promise().nextResumeTiming();
			}
		};
	}

	template <typename TResult>
	class CoSceneBase;

	template <typename TResult>
	class [[nodiscard]] CoTask
	{
	private:
		detail::CoroutineHandleWrapper<TResult> m_handle;

	public:
		using promise_type = detail::Promise<TResult>;
		using handle_type = std::coroutine_handle<promise_type>;
		using result_type = TResult;

		explicit CoTask(handle_type h)
			: m_handle(std::move(h))
		{
		}

		CoTask(const CoTask<TResult>&) = delete;

		CoTask<TResult>& operator=(const CoTask<TResult>&) = delete;

		CoTask(CoTask<TResult>&& rhs) = default;

		CoTask<TResult>& operator=(CoTask<TResult>&& rhs) = default;

		virtual void resume(detail::FrameTiming frameTiming)
		{
			if (m_handle.done())
			{
				return;
			}
			m_handle.resume(frameTiming);
		}

		[[nodiscard]]
		virtual bool done() const
		{
			return m_handle.done();
		}

		[[nodiscard]]
		TResult value() const
		{
			return m_handle.value();
		}

		[[nodiscard]]
		Co::ScopedTaskRun runScoped()&&
		{
			return Co::ScopedTaskRun{ detail::CoTaskAwaiter<TResult>{ std::move(*this) } };
		}

		void runForget()&&
		{
			resume(detail::CoTaskBackend::CurrentFrameTiming());
			if (m_handle.done())
			{
				// フレーム待ちなしで終了した場合は登録不要
				return;
			}
			(void)detail::CoTaskBackend::Add(std::make_unique<detail::CoTaskAwaiter<TResult>>(std::move(*this)));
		}
	};

	namespace detail
	{
		template <typename TResult>
		class [[nodiscard]] CoTaskAwaiter : public detail::IAwaiter
		{
		private:
			CoTask<TResult> m_task;

		public:
			explicit CoTaskAwaiter(CoTask<TResult>&& task)
				: m_task(std::move(task))
			{
			}

			CoTaskAwaiter(const CoTaskAwaiter<TResult>&) = delete;

			CoTaskAwaiter<TResult>& operator=(const CoTaskAwaiter<TResult>&) = delete;

			CoTaskAwaiter(CoTaskAwaiter<TResult>&& rhs) = default;

			CoTaskAwaiter<TResult>& operator=(CoTaskAwaiter<TResult>&& rhs) = default;

			virtual void resume(detail::FrameTiming frameTiming) override
			{
				m_task.resume(frameTiming);
			}

			[[nodiscard]]
			virtual bool done() const override
			{
				return m_task.done();
			}

			[[nodiscard]]
			bool await_ready()
			{
				resume(detail::CoTaskBackend::CurrentFrameTiming());
				return m_task.done();
			}

			template <typename TResultOther>
			void await_suspend(std::coroutine_handle<detail::Promise<TResultOther>> handle)
			{
				handle.promise().setSubAwaiter(this);
			}

			TResult await_resume() const
			{
				return m_task.value();
			}

			[[nodiscard]]
			TResult value() const
			{
				return m_task.value();
			}
		};
	}

	template <typename TResult>
	auto operator co_await(CoTask<TResult>&& rhs)
	{
		return detail::CoTaskAwaiter<TResult>{ std::move(rhs) };
	}

	template <typename TResult>
	auto operator co_await(const CoTask<TResult>& rhs) = delete;

	namespace detail
	{
		class PromiseBase
		{
		protected:
			IAwaiter* m_pSubAwaiter = nullptr;
			FrameTiming m_nextResumeTiming = FrameTiming::Update;

			std::exception_ptr m_exception;

		public:
			virtual ~PromiseBase() = 0;

			PromiseBase() = default;

			PromiseBase(const PromiseBase&) = delete;

			PromiseBase& operator=(const PromiseBase&) = delete;

			PromiseBase(PromiseBase&&) = default;

			PromiseBase& operator=(PromiseBase&&) = default;

			[[nodiscard]]
			auto initial_suspend()
			{
				return std::suspend_always{};
			}

			[[nodiscard]]
			auto final_suspend() noexcept
			{
				return std::suspend_always{};
			}

			[[nodiscard]]
			auto yield_value(FrameTiming frameTiming)
			{
				if (frameTiming == FrameTiming::Init)
				{
					throw Error{ U"CoTask: FrameTiming::Init is not allowed in co_yield" };
				}
				m_nextResumeTiming = frameTiming;
				return std::suspend_always{};
			}

			void unhandled_exception()
			{
				m_exception = std::current_exception();
			}

			void rethrowIfException() const
			{
				if (m_exception)
				{
					std::rethrow_exception(m_exception);
				}
			}

			[[nodiscard]]
			bool resumeSubAwaiter(FrameTiming frameTiming)
			{
				if (!m_pSubAwaiter)
				{
					return false;
				}

				m_pSubAwaiter->resume(frameTiming);

				if (m_pSubAwaiter->done())
				{
					m_pSubAwaiter = nullptr;
					return false;
				}

				return true;
			}

			void setSubAwaiter(IAwaiter* pSubAwaiter)
			{
				m_pSubAwaiter = pSubAwaiter;
			}

			[[nodiscard]]
			FrameTiming nextResumeTiming() const
			{
				return m_nextResumeTiming;
			}
		};

		inline PromiseBase::~PromiseBase() = default;

		template <typename TResult>
		class Promise : public PromiseBase
		{
		private:
			std::optional<TResult> m_value;

		public:
			Promise() = default;

			Promise(Promise<TResult>&&) = default;

			Promise& operator=(Promise<TResult>&&) = default;

			void return_value(const TResult& v)
			{
				m_value = v;
			}

			void return_value(TResult&& v)
			{
				m_value = std::move(v);
			}

			[[nodiscard]]
			TResult value() const
			{
				rethrowIfException();
				if (!m_value)
				{
					throw Error{ U"CoTask is not completed. Make sure that all paths in the coroutine return a value." };
				}
				return m_value.value();
			}

			[[nodiscard]]
			CoTask<TResult> get_return_object()
			{
				return CoTask<TResult>{ CoTask<TResult>::handle_type::from_promise(*this) };
			}
		};

		template <>
		class Promise<void> : public PromiseBase
		{
		public:
			Promise() = default;

			Promise(Promise<void>&&) = default;

			Promise<void>& operator=(Promise<void>&&) = default;

			void return_void() const
			{
			}

			void value() const
			{
				rethrowIfException();
			}

			[[nodiscard]]
			CoTask<void> get_return_object()
			{
				return CoTask<void>{ CoTask<void>::handle_type::from_promise(*this) };
			}
		};
	}

	namespace Co
	{
		inline void Init()
		{
			detail::CoTaskBackend::Init();
		}

		inline CoTask<void> DelayFrame()
		{
			co_yield detail::FrameTiming::Update;
		}

		inline CoTask<void> DelayFrame(int32 frames)
		{
			for (int32 i = 0; i < frames; ++i)
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		inline CoTask<void> Delay(const Duration duration)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			const Timer timer{ duration, StartImmediately::Yes };
			while (!timer.reachedZero())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		inline CoTask<void> Delay(const Duration duration, std::function<void(const Timer&)> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			const Timer timer{ duration, StartImmediately::Yes };
			while (!timer.reachedZero())
			{
				func(timer);
				co_yield detail::FrameTiming::Update;
			}
		}

		inline CoTask<void> WaitUntil(std::function<bool()> predicate)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!predicate())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		inline CoTask<void> WaitForTimer(const Timer* pTimer)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!pTimer->reachedZero())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TInput>
		CoTask<void> WaitForDown(const TInput input)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!input.down())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TInput>
		CoTask<void> WaitForUp(const TInput input)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!input.up())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		CoTask<void> WaitForLeftClicked(const TArea area)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!area.leftClicked())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		CoTask<void> WaitForLeftReleased(const TArea area)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!area.leftReleased())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		CoTask<void> WaitForLeftClickedThenReleased(const TArea area)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.leftClicked())
				{
					Print << U"clickeda5";
					const auto scopedTaskRun = EveryFrame([]() {Print << U"clicked"; }).runScoped();
					const auto [releasedInArea, _] = co_await WhenAny(WaitForLeftReleased(area), WaitForUp(MouseL));
					if (releasedInArea.has_value())
					{
						break;
					}
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		CoTask<void> WaitForRightClicked(const TArea area)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!area.rightClicked())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		CoTask<void> WaitForRightReleased(const TArea area)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!area.rightReleased())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		CoTask<void> WaitForRightClickedThenReleased(const TArea area)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.rightClicked())
				{
					const auto [releasedInArea, _] = co_await WhenAny(WaitForRightReleased(area), WaitForUp(MouseR));
					if (releasedInArea.has_value())
					{
						break;
					}
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		CoTask<void> WaitForMouseOver(const TArea area)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!area.mouseOver())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		inline CoTask<void> WaitWhile(std::function<bool()> predicate)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (predicate())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		inline CoTask<void> WaitForever()
		{
			while (true)
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		inline CoTask<void> EveryFrame(std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				func();
				co_yield detail::FrameTiming::Update;
			}
		}

		inline CoTask<void> EveryFrameDraw(std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Draw)
			{
				co_yield detail::FrameTiming::Draw;
			}

			while (true)
			{
				func();
				co_yield detail::FrameTiming::Draw;
			}
		}

		inline CoTask<void> EveryFramePostPresent(std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::PostPresent)
			{
				co_yield detail::FrameTiming::PostPresent;
			}

			while (true)
			{
				func();
				co_yield detail::FrameTiming::PostPresent;
			}
		}

		template <class TInput>
		inline CoTask<void> ExecOnDown(const TInput input, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (input.down())
				{
					func();
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TInput>
		inline CoTask<void> ExecOnUp(const TInput input, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (input.up())
				{
					func();
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TInput>
		inline CoTask<void> ExecOnPressed(const TInput input, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (input.pressed())
				{
					func();
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		inline CoTask<void> ExecOnLeftClicked(const TArea area, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.leftClicked())
				{
					func();
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		inline CoTask<void> ExecOnLeftPressed(const TArea area, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.leftPressed())
				{
					func();
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		inline CoTask<void> ExecOnLeftReleased(const TArea area, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.leftReleased())
				{
					func();
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		inline CoTask<void> ExecOnLeftClickedThenReleased(const TArea area, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.leftClicked())
				{
					const auto [releasedInArea, _] = co_await WhenAny(WaitForLeftReleased(area), WaitForUp(MouseL));
					if (releasedInArea.has_value())
					{
						func();
					}
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		inline CoTask<void> ExecOnRightClicked(const TArea area, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.rightClicked())
				{
					func();
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		inline CoTask<void> ExecOnRightPressed(const TArea area, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.rightPressed())
				{
					func();
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		inline CoTask<void> ExecOnRightReleased(const TArea area, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.rightReleased())
				{
					func();
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		inline CoTask<void> ExecOnRightClickedThenReleased(const TArea area, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.rightClicked())
				{
					const auto [releasedInArea, _] = co_await WhenAny(WaitForRightReleased(area), WaitForUp(MouseR));
					if (releasedInArea.has_value())
					{
						func();
					}
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		inline CoTask<void> ExecOnMouseOver(const TArea area, std::function<void()> func)
		{
			if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.mouseOver())
				{
					func();
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		// voidを含むタプルは使用できないため、voidの代わりに戻り値として返すための空の構造体を用意
		struct VoidResult
		{
		};
	}

	namespace detail
	{
		template <typename TResult>
		using VoidResultTypeReplace = std::conditional_t<std::is_void_v<TResult>, Co::VoidResult, TResult>;

		template <typename TResult>
		auto ConvertVoidResult(const CoTask<TResult>& task) -> VoidResultTypeReplace<TResult>
		{
			if constexpr (std::is_void_v<TResult>)
			{
				return Co::VoidResult{};
			}
			else
			{
				return task.value();
			}
		}

		template <typename TResult>
		auto ConvertOptionalVoidResult(const CoTask<TResult>& task) -> Optional<VoidResultTypeReplace<TResult>>
		{
			if (!task.done())
			{
				return none;
			}

			if constexpr (std::is_void_v<TResult>)
			{
				return MakeOptional(Co::VoidResult{});
			}
			else
			{
				return MakeOptional(task.value());
			}
		}

		template <typename TScene>
		concept CoSceneConcept = std::derived_from<TScene, CoSceneBase<typename TScene::result_type>>;

		template <CoSceneConcept TScene>
		CoTask<typename TScene::result_type> ScenePtrToTask(std::unique_ptr<TScene> scene)
		{
			const auto scopedRun = Co::EveryFrameDraw([&scene]() { scene->draw(); }).runScoped();
			co_return co_await scene->start();
		}
	}

	namespace Co
	{
		template <typename TResult>
		CoTask<TResult> ToTask(CoTask<TResult>&& task)
		{
			return task;
		}

		template <detail::CoSceneConcept TScene>
		CoTask<typename TScene::result_type> ToTask(TScene&& scene)
		{
			return detail::ScenePtrToTask(std::make_unique<TScene>(std::move(scene)));
		}

		template <detail::CoSceneConcept TScene, class... Args>
		CoTask<typename TScene::result_type> MakeTask(Args&&... args)
		{
			return detail::ScenePtrToTask(std::make_unique<TScene>(std::forward<Args>(args)...));
		}

		template <class... TTasks>
		auto WhenAll(TTasks&&... args) -> CoTask<std::tuple<detail::VoidResultTypeReplace<typename TTasks::result_type>...>>
		{
			if constexpr ((!std::is_same_v<TTasks, CoTask<typename TTasks::result_type>> || ...))
			{
				// TTasksの中にCoTaskでないものが1つでも含まれる場合は、ToTaskで変換して呼び出し直す
				co_return co_await WhenAll(ToTask(std::forward<TTasks>(args))...);
			}
			else
			{
				if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
				{
					co_yield detail::FrameTiming::Update;
				}

				if ((args.done() && ...))
				{
					co_return std::make_tuple(detail::ConvertVoidResult(args)...);
				}

				while (true)
				{
					(args.resume(detail::FrameTiming::Update), ...);
					if ((args.done() && ...))
					{
						co_return std::make_tuple(detail::ConvertVoidResult(args)...);
					}
					co_yield detail::FrameTiming::Draw;
					(args.resume(detail::FrameTiming::Draw), ...);
					co_yield detail::FrameTiming::PostPresent;
					(args.resume(detail::FrameTiming::PostPresent), ...);
					co_yield detail::FrameTiming::Update;
				}
			}
		}

		template <class... TTasks>
		auto WhenAny(TTasks&&... args) -> CoTask<std::tuple<Optional<detail::VoidResultTypeReplace<typename TTasks::result_type>>...>>
		{
			if constexpr ((!std::is_same_v<TTasks, CoTask<typename TTasks::result_type>> || ...))
			{
				// TTasksの中にCoTaskでないものが1つでも含まれる場合は、ToTaskで変換して呼び出し直す
				co_return co_await WhenAny(ToTask(std::forward<TTasks>(args))...);
			}
			else
			{
				if (detail::CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
				{
					co_yield detail::FrameTiming::Update;
				}

				if ((args.done() || ...))
				{
					co_return std::make_tuple(detail::ConvertOptionalVoidResult(args)...);
				}

				while (true)
				{
					(args.resume(detail::FrameTiming::Update), ...);
					if ((args.done() || ...))
					{
						co_return std::make_tuple(detail::ConvertOptionalVoidResult(args)...);
					}
					co_yield detail::FrameTiming::Draw;
					(args.resume(detail::FrameTiming::Draw), ...);
					co_yield detail::FrameTiming::PostPresent;
					(args.resume(detail::FrameTiming::PostPresent), ...);
					co_yield detail::FrameTiming::Update;
				}
			}
		}
	}

	template <typename TResult>
	class [[nodiscard]] CoSceneBase
	{
	public:
		using result_type = TResult;

		CoSceneBase() = default;

		CoSceneBase(const CoSceneBase&) = delete;

		CoSceneBase& operator=(const CoSceneBase&) = delete;

		CoSceneBase(CoSceneBase&&) = default;

		CoSceneBase& operator=(CoSceneBase&&) = default;

		virtual ~CoSceneBase() = default;

		virtual CoTask<TResult> start() = 0;

		virtual void draw() const
		{
		}
	};

	template <detail::CoSceneConcept TScene>
	auto operator co_await(TScene&& scene)
	{
		// CoSceneをCo::MakeTaskを使わずco_awaitに直接渡すには、ムーブ構築可能である必要がある
		static_assert(std::is_move_constructible_v<TScene>, "To pass a CoScene directly to co_await, it must be move-constructible. Otherwise, use Co::MakeTask<TScene>() instead.");
		return detail::CoTaskAwaiter<typename TScene::result_type>{ detail::ScenePtrToTask(std::make_unique<TScene>(std::move(scene))) };
	}

	template <detail::CoSceneConcept TScene>
	auto operator co_await(TScene& scene) = delete;

	namespace detail
	{
		inline uint64 s_fadeCount = 0;
	}

	namespace Co
	{
		[[nodiscard]]
		inline bool IsFading()
		{
			return detail::s_fadeCount > 0;
		}

		class ScopedSetIsFadingToTrue
		{
		public:
			ScopedSetIsFadingToTrue()
			{
				++detail::s_fadeCount;
			}

			~ScopedSetIsFadingToTrue()
			{
				if (detail::s_fadeCount > 0)
				{
					--detail::s_fadeCount;
				}
			}

			ScopedSetIsFadingToTrue(const ScopedSetIsFadingToTrue&) = delete;

			ScopedSetIsFadingToTrue& operator=(const ScopedSetIsFadingToTrue&) = delete;

			ScopedSetIsFadingToTrue(ScopedSetIsFadingToTrue&&) = default;

			ScopedSetIsFadingToTrue& operator=(ScopedSetIsFadingToTrue&&) = default;
		};
	}

	namespace detail
	{
		class [[nodiscard]] FadeSceneBase : public CoSceneBase<void>
		{
		private:
			Timer m_timer;
			double m_t = 0.0;

		public:
			explicit FadeSceneBase(const Duration& duration)
				: m_timer(duration, StartImmediately::No)
			{
			}

			virtual ~FadeSceneBase() = default;

			CoTask<void> start() override final
			{
				const Co::ScopedSetIsFadingToTrue scopedSetIsFadingToTrue;

				m_timer.start();
				while (true)
				{
					m_t = m_timer.progress0_1();
					if (m_t >= 1.0)
					{
						break;
					}
					co_yield FrameTiming::Update;
				}

				// 最後に必ずt=1.0で描画されるように
				m_t = 1.0;
				co_yield FrameTiming::Update;
			}

			void draw() const override final
			{
				drawFade(m_t);
			}

			// tには時間が0.0～1.0で渡される
			virtual void drawFade(double t) const = 0;
		};

		class [[nodiscard]] FadeInScene : public FadeSceneBase
		{
		private:
			ColorF m_color;

		public:
			explicit FadeInScene(const Duration& duration, const ColorF& color)
				: FadeSceneBase(duration)
				, m_color(color)
			{
			}

			void drawFade(double t) const override
			{
				const Transformer2D transform{ Mat3x2::Identity(), Transformer2D::Target::SetLocal };

				Scene::Rect().draw(ColorF{ m_color, 1.0 - t });
			}
		};

		class [[nodiscard]] FadeOutScene : public FadeSceneBase
		{
		private:
			ColorF m_color;

		public:
			explicit FadeOutScene(const Duration& duration, const ColorF& color)
				: FadeSceneBase(duration)
				, m_color(color)
			{
			}

			void drawFade(double t) const override
			{
				const Transformer2D transform{ Mat3x2::Identity(), Transformer2D::Target::SetLocal };

				Scene::Rect().draw(ColorF{ m_color, t });
			}
		};
	}

	namespace Co
	{
		inline CoTask<void> FadeIn(const Duration& duration, const ColorF& color = Palette::Black)
		{
			return detail::ScenePtrToTask(std::make_unique<detail::FadeInScene>(duration, color));
		}

		inline CoTask<void> FadeOut(const Duration& duration, const ColorF& color = Palette::Black)
		{
			return detail::ScenePtrToTask(std::make_unique<detail::FadeOutScene>(duration, color));
		}
	}
}
