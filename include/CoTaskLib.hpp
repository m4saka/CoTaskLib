﻿//----------------------------------------------------------------------------------------
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
namespace cotasklib
#else
inline namespace cotasklib
#endif
{
	namespace Co
	{
		namespace detail
		{
			enum class FrameTiming
			{
				Init,
				Update,
				Draw,
				LateDraw,
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
			class TaskAwaiter;
		}

		template <typename TResult>
		class Task;

		class SceneBase;

		using SceneFactory = std::function<std::unique_ptr<SceneBase>()>;

		namespace detail
		{
			class Backend
			{
			private:
				static constexpr StringView AddonName{ U"Co::BackendAddon" };

				static inline Backend* s_pInstance = nullptr;

				// Note: draw関数がconstであることの対処用にアドオンと実体を分離し、実体はポインタで持つようにしている
				class BackendAddon : public IAddon
				{
				private:
					bool m_isFirstUpdated = false;
					std::unique_ptr<Backend> m_instance;

				public:
					BackendAddon()
						: m_instance{ std::make_unique<Backend>() }
					{
						if (s_pInstance)
						{
							throw Error{ U"Co::BackendAddon: Instance already exists" };
						}
						s_pInstance = m_instance.get();
					}

					virtual ~BackendAddon()
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
						m_instance->resume(FrameTiming::LateDraw);
					}

					[[nodiscard]]
					Backend* instance() const
					{
						return m_instance.get();
					}
				};

				FrameTiming m_currentFrameTiming = FrameTiming::Init;

				AwaiterID m_nextAwaiterID = 1;

				std::optional<AwaiterID> m_currentAwaiterID = none;

				std::map<AwaiterID, std::unique_ptr<IAwaiter>> m_awaiters;

				SceneFactory m_currentSceneFactory;

			public:
				Backend() = default;

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
					Addon::Register(AddonName, std::make_unique<BackendAddon>());
				}

				[[nodiscard]]
				static AwaiterID Add(std::unique_ptr<IAwaiter>&& awaiter)
				{
					if (!awaiter)
					{
						throw Error{ U"awaiter must not be nullptr" };
					}

					if (!s_pInstance)
					{
						throw Error{ U"Backend is not initialized" };
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
						throw Error{ U"Backend::UnregisterTask: Cannot unregister the currently running task" };
					}
					s_pInstance->m_awaiters.erase(id);
				}

				[[nodiscard]]
				static bool IsDone(AwaiterID id)
				{
					if (!s_pInstance)
					{
						throw Error{ U"Backend is not initialized" };
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
						throw Error{ U"Backend is not initialized" };
					}
					return s_pInstance->m_currentFrameTiming;
				}

				[[nodiscard]]
				static void SetCurrentSceneFactory(SceneFactory factory)
				{
					if (!s_pInstance)
					{
						throw Error{ U"Backend is not initialized" };
					}
					s_pInstance->m_currentSceneFactory = std::move(factory);
				}

				[[nodiscard]]
				static SceneFactory CurrentSceneFactory()
				{
					if (!s_pInstance)
					{
						throw Error{ U"Backend is not initialized" };
					}
					return s_pInstance->m_currentSceneFactory;
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
							Backend::Remove(m_id.value());
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
						Backend::Remove(m_id.value());
					}
				}

				[[nodiscard]]
				bool done() const
				{
					return !m_id || Backend::IsDone(m_id.value());
				}
			};

			template <typename TResult>
			[[nodiscard]]
			std::optional<AwaiterID> ResumeAwaiterOnceAndRegisterIfNotDone(TaskAwaiter<TResult>&& awaiter)
			{
				if (awaiter.done())
				{
					// 既に終了済み
					return none;
				}

				awaiter.resume(Backend::CurrentFrameTiming());
				if (awaiter.done())
				{
					// フレーム待ちなしで終了した場合は登録不要
					return none;
				}
				return Backend::Add(std::make_unique<TaskAwaiter<TResult>>(std::move(awaiter)));
			}

			template <typename TResult>
			std::optional<AwaiterID> ResumeAwaiterOnceAndRegisterIfNotDone(const TaskAwaiter<TResult>& awaiter) = delete;
		}

		class ScopedTaskRun
		{
		private:
			detail::ScopedTaskRunLifetime m_lifetime;

		public:
			template <typename TResult>
			explicit ScopedTaskRun(detail::TaskAwaiter<TResult>&& awaiter)
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


			template <typename TResult>
			class ThenCaller
			{
			public:
				using function_type = std::function<void(TResult)>;

			private:
				std::vector<function_type> m_funcs;
				bool m_called = false;

			public:
				ThenCaller() = default;

				ThenCaller(const ThenCaller&) = delete;

				ThenCaller& operator=(const ThenCaller&) = delete;

				ThenCaller(ThenCaller&&) = default;

				ThenCaller& operator=(ThenCaller&&) = default;

				void add(function_type func)
				{
					m_funcs.push_back(std::move(func));
				}

				void callOnce(const Task<TResult>& task)
				{
					if (m_called)
					{
						return;
					}
					const TResult result = task.value();
					for (const auto& func : m_funcs)
					{
						func(result);
					}
					m_called = true;
				}
			};

			template <>
			class ThenCaller<void>
			{
			public:
				using function_type = std::function<void()>;

			private:
				std::vector<function_type> m_funcs;
				bool m_called = false;

			public:
				ThenCaller() = default;

				ThenCaller(const ThenCaller&) = delete;

				ThenCaller& operator=(const ThenCaller&) = delete;

				ThenCaller(ThenCaller&&) = default;

				ThenCaller& operator=(ThenCaller&&) = default;

				void add(function_type func)
				{
					m_funcs.push_back(std::move(func));
				}

				void callOnce(const Task<void>&)
				{
					if (m_called)
					{
						return;
					}
					for (const auto& func : m_funcs)
					{
						func();
					}
					m_called = true;
				}
			};
		}

		template <typename TResult>
		class SequenceBase;

		enum class WithTiming
		{
			Before,
			After,
		};

		class [[nodiscard]] ITask
		{
		public:
			virtual ~ITask() = default;

			virtual void resume(detail::FrameTiming frameTiming) = 0;

			[[nodiscard]]
			virtual bool done() const = 0;
		};

		template <typename TResult>
		class [[nodiscard]] Task : public ITask
		{
		private:
			detail::CoroutineHandleWrapper<TResult> m_handle;
			std::vector<std::unique_ptr<ITask>> m_concurrentTasksBefore;
			std::vector<std::unique_ptr<ITask>> m_concurrentTasksAfter;
			std::vector<std::function<void()>> m_updateFuncs;
			std::vector<std::function<void()>> m_drawFuncs;
			std::vector<std::function<void()>> m_lateDrawFuncs;
			detail::ThenCaller<TResult> m_thenCaller;

		public:
			using promise_type = detail::Promise<TResult>;
			using handle_type = std::coroutine_handle<promise_type>;
			using result_type = TResult;
			using then_function_type = typename detail::ThenCaller<TResult>::function_type;

			explicit Task(handle_type h)
				: m_handle(std::move(h))
			{
			}

			Task(const Task<TResult>&) = delete;

			Task<TResult>& operator=(const Task<TResult>&) = delete;

			Task(Task<TResult>&& rhs) = default;

			Task<TResult>& operator=(Task<TResult>&& rhs) = default;

			virtual void resume(detail::FrameTiming frameTiming)
			{
				if (m_handle.done())
				{
					m_thenCaller.callOnce(*this);
					return;
				}

				for (auto& task : m_concurrentTasksBefore)
				{
					task->resume(frameTiming);
				}

				m_handle.resume(frameTiming);

				if (m_handle.done())
				{
					m_thenCaller.callOnce(*this);
				}

				for (auto& task : m_concurrentTasksAfter)
				{
					task->resume(frameTiming);
				}

				switch (frameTiming)
				{
				case detail::FrameTiming::Update:
					for (const auto& func : m_updateFuncs)
					{
						func();
					}
					break;

				case detail::FrameTiming::Draw:
					for (const auto& func : m_drawFuncs)
					{
						func();
					}
					break;

				case detail::FrameTiming::LateDraw:
					for (const auto& func : m_lateDrawFuncs)
					{
						func();
					}
					break;
				};
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
			Co::ScopedTaskRun runScoped() &&
			{
				return Co::ScopedTaskRun{ detail::TaskAwaiter<TResult>{ std::move(*this) } };
			}

			void runForget() &&
			{
				resume(detail::Backend::CurrentFrameTiming());
				if (m_handle.done())
				{
					// フレーム待ちなしで終了した場合は登録不要
					return;
				}
				(void)detail::Backend::Add(std::make_unique<detail::TaskAwaiter<TResult>>(std::move(*this)));
			}

			template <typename TResultOther>
			[[nodiscard]]
			Task<TResult> with(Task<TResultOther>&& task) &&
			{
				m_concurrentTasksAfter.push_back(std::make_unique<Task<TResultOther>>(std::move(task)));
				return std::move(*this);
			}

			template <typename TResultOther>
			[[nodiscard]]
			Task<TResult> with(Task<TResultOther>&& task, WithTiming timing) &&
			{
				switch (timing)
				{
				case WithTiming::Before:
					m_concurrentTasksBefore.push_back(std::make_unique<Task<TResultOther>>(std::move(task)));
					break;

				case WithTiming::After:
					m_concurrentTasksAfter.push_back(std::make_unique<Task<TResultOther>>(std::move(task)));
					break;

				default:
					throw Error{ U"Task: Invalid WithTiming" };
				}
				return std::move(*this);
			}

			[[nodiscard]]
			Task<TResult>&& withUpdate(std::function<void()> func) &&
			{
				m_updateFuncs.push_back(std::move(func));
				return std::move(*this);
			}

			[[nodiscard]]
			Task<TResult>&& withUpdate(std::function<void()> func, WithTiming timing) &&
			{
				switch (timing)
				{
				case WithTiming::Before:
					m_updateFuncs.insert(m_updateFuncs.begin(), std::move(func));
					break;

				case WithTiming::After:
					m_updateFuncs.push_back(std::move(func));
					break;

				default:
					throw Error{ U"Task: Invalid WithTiming" };
				}
				return std::move(*this);
			}

			[[nodiscard]]
			Task<TResult>&& withDraw(std::function<void()> func) &&
			{
				m_drawFuncs.push_back(std::move(func));
				return std::move(*this);
			}

			[[nodiscard]]
			Task<TResult>&& withDraw(std::function<void()> func, WithTiming timing) &&
			{
				switch (timing)
				{
				case WithTiming::Before:
					m_drawFuncs.insert(m_drawFuncs.begin(), std::move(func));
					break;

				case WithTiming::After:
					m_drawFuncs.push_back(std::move(func));
					break;

				default:
					throw Error{ U"Task: Invalid WithTiming" };
				}
				return std::move(*this);
			}

			[[nodiscard]]
			Task<TResult>&& withLateDraw(std::function<void()> func) &&
			{
				m_lateDrawFuncs.push_back(std::move(func));
				return std::move(*this);
			}

			[[nodiscard]]
			Task<TResult>&& withLateDraw(std::function<void()> func, WithTiming timing) &&
			{
				switch (timing)
				{
				case WithTiming::Before:
					m_lateDrawFuncs.insert(m_lateDrawFuncs.begin(), std::move(func));
					break;

				case WithTiming::After:
					m_lateDrawFuncs.push_back(std::move(func));
					break;

				default:
					throw Error{ U"Task: Invalid WithTiming" };
				}
				return std::move(*this);
			}

			[[nodiscard]]
			Task<TResult>&& then(then_function_type func) &&
			{
				m_thenCaller.add(std::move(func));
				return std::move(*this);
			}
		};

		namespace detail
		{
			template <typename TResult>
			class [[nodiscard]] TaskAwaiter : public detail::IAwaiter
			{
			private:
				Task<TResult> m_task;

			public:
				explicit TaskAwaiter(Task<TResult>&& task)
					: m_task(std::move(task))
				{
				}

				TaskAwaiter(const TaskAwaiter<TResult>&) = delete;

				TaskAwaiter<TResult>& operator=(const TaskAwaiter<TResult>&) = delete;

				TaskAwaiter(TaskAwaiter<TResult>&& rhs) = default;

				TaskAwaiter<TResult>& operator=(TaskAwaiter<TResult>&& rhs) = default;

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
					resume(detail::Backend::CurrentFrameTiming());
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
		auto operator co_await(Task<TResult>&& rhs)
		{
			return detail::TaskAwaiter<TResult>{ std::move(rhs) };
		}

		template <typename TResult>
		auto operator co_await(const Task<TResult>& rhs) = delete;

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
						throw Error{ U"Task: FrameTiming::Init is not allowed in co_yield" };
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
						throw Error{ U"Task is not completed. Make sure that all paths in the coroutine return a value." };
					}
					return m_value.value();
				}

				[[nodiscard]]
				Task<TResult> get_return_object()
				{
					return Task<TResult>{ Task<TResult>::handle_type::from_promise(*this) };
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
				Task<void> get_return_object()
				{
					return Task<void>{ Task<void>::handle_type::from_promise(*this) };
				}
			};
		}

		inline void Init()
		{
			detail::Backend::Init();
		}

		[[nodiscard]]
		inline Task<void> DelayFrame()
		{
			co_yield detail::FrameTiming::Update;
		}

		[[nodiscard]]
		inline Task<void> DelayFrame(int32 frames)
		{
			for (int32 i = 0; i < frames; ++i)
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		[[nodiscard]]
		inline Task<void> Delay(const Duration duration)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			const Timer timer{ duration, StartImmediately::Yes };
			while (!timer.reachedZero())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		[[nodiscard]]
		inline Task<void> Delay(const Duration duration, std::function<void(const Timer&)> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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

		[[nodiscard]]
		inline Task<void> WaitUntil(std::function<bool()> predicate)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!predicate())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <typename T>
		[[nodiscard]]
		inline Task<T> WaitForResult(const std::optional<T>* pOptional)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!pOptional->has_value())
			{
				co_yield detail::FrameTiming::Update;
			}

			co_return **pOptional;
		}

		template <typename T>
		[[nodiscard]]
		inline Task<T> WaitForResult(const Optional<T>* pOptional)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!pOptional->has_value())
			{
				co_yield detail::FrameTiming::Update;
			}

			co_return **pOptional;
		}

		[[nodiscard]]
		inline Task<void> WaitForTimer(const Timer* pTimer)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!pTimer->reachedZero())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TInput>
		[[nodiscard]]
		Task<void> WaitForDown(const TInput input)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!input.down())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TInput>
		[[nodiscard]]
		Task<void> WaitForUp(const TInput input)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!input.up())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForLeftClicked(const TArea area)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!area.leftClicked())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForLeftReleased(const TArea area)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!area.leftReleased())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForLeftClickedThenReleased(const TArea area)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.leftClicked())
				{
					const auto [releasedInArea, _] = co_await Any(WaitForLeftReleased(area), WaitForUp(MouseL));
					if (releasedInArea.has_value())
					{
						break;
					}
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForRightClicked(const TArea area)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!area.rightClicked())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForRightReleased(const TArea area)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!area.rightReleased())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForRightClickedThenReleased(const TArea area)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.rightClicked())
				{
					const auto [releasedInArea, _] = co_await Any(WaitForRightReleased(area), WaitForUp(MouseR));
					if (releasedInArea.has_value())
					{
						break;
					}
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForMouseOver(const TArea area)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (!area.mouseOver())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		[[nodiscard]]
		inline Task<void> WaitWhile(std::function<bool()> predicate)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (predicate())
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		[[nodiscard]]
		inline Task<void> WaitForever()
		{
			while (true)
			{
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TInput>
		[[nodiscard]]
		inline Task<void> ExecOnDown(const TInput input, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
		[[nodiscard]]
		inline Task<void> ExecOnUp(const TInput input, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
		[[nodiscard]]
		inline Task<void> ExecOnPressed(const TInput input, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
		[[nodiscard]]
		inline Task<void> ExecOnLeftClicked(const TArea area, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
		[[nodiscard]]
		inline Task<void> ExecOnLeftPressed(const TArea area, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
		[[nodiscard]]
		inline Task<void> ExecOnLeftReleased(const TArea area, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
		[[nodiscard]]
		inline Task<void> ExecOnLeftClickedThenReleased(const TArea area, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.leftClicked())
				{
					const auto [releasedInArea, _] = co_await Any(WaitForLeftReleased(area), WaitForUp(MouseL));
					if (releasedInArea.has_value())
					{
						func();
					}
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		[[nodiscard]]
		inline Task<void> ExecOnRightClicked(const TArea area, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
		[[nodiscard]]
		inline Task<void> ExecOnRightPressed(const TArea area, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
		[[nodiscard]]
		inline Task<void> ExecOnRightReleased(const TArea area, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
		[[nodiscard]]
		inline Task<void> ExecOnRightClickedThenReleased(const TArea area, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
			{
				co_yield detail::FrameTiming::Update;
			}

			while (true)
			{
				if (area.rightClicked())
				{
					const auto [releasedInArea, _] = co_await Any(WaitForRightReleased(area), WaitForUp(MouseR));
					if (releasedInArea.has_value())
					{
						func();
					}
				}
				co_yield detail::FrameTiming::Update;
			}
		}

		template <class TArea>
		[[nodiscard]]
		inline Task<void> ExecOnMouseOver(const TArea area, std::function<void()> func)
		{
			if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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

		namespace detail
		{
			template <typename TResult>
			using VoidResultTypeReplace = std::conditional_t<std::is_void_v<TResult>, Co::VoidResult, TResult>;

			template <typename TResult>
			[[nodiscard]]
			auto ConvertVoidResult(const Task<TResult>& task) -> VoidResultTypeReplace<TResult>
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
			[[nodiscard]]
			auto ConvertOptionalVoidResult(const Task<TResult>& task) -> Optional<VoidResultTypeReplace<TResult>>
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

			template <typename TSequence>
			concept SequenceConcept = std::derived_from<TSequence, SequenceBase<typename TSequence::result_type>>;

			template <SequenceConcept TSequence>
			[[nodiscard]]
			Task<typename TSequence::result_type> SequencePtrToTask(std::unique_ptr<TSequence> sequence)
			{
				co_return co_await sequence->start()
					.withDraw([&sequence] { sequence->draw(); })
					.withLateDraw([&sequence] { sequence->lateDraw(); });
			}

			template <typename TScene>
			concept SceneConcept = std::derived_from<TScene, SceneBase>;
		}

		template <typename TResult>
		[[nodiscard]]
		Task<TResult> ToTask(Task<TResult>&& task)
		{
			return task;
		}

		template <detail::SequenceConcept TSequence>
		[[nodiscard]]
		Task<typename TSequence::result_type> ToTask(TSequence&& sequence)
		{
			return detail::SequencePtrToTask(std::make_unique<TSequence>(std::move(sequence)));
		}

		template <detail::SequenceConcept TSequence, class... Args>
		[[nodiscard]]
		Task<typename TSequence::result_type> AsTask(Args&&... args)
		{
			return detail::SequencePtrToTask(std::make_unique<TSequence>(std::forward<Args>(args)...));
		}

		template <class... TTasks>
		auto All(TTasks&&... args) -> Task<std::tuple<detail::VoidResultTypeReplace<typename TTasks::result_type>...>>
		{
			if constexpr ((!std::is_same_v<TTasks, Task<typename TTasks::result_type>> || ...))
			{
				// TTasksの中にTaskでないものが1つでも含まれる場合は、ToTaskで変換して呼び出し直す
				co_return co_await All(ToTask(std::forward<TTasks>(args))...);
			}
			else
			{
				if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
					co_yield detail::FrameTiming::LateDraw;
					(args.resume(detail::FrameTiming::LateDraw), ...);
					co_yield detail::FrameTiming::Update;
				}
			}
		}

		template <class... TTasks>
		auto Any(TTasks&&... args) -> Task<std::tuple<Optional<detail::VoidResultTypeReplace<typename TTasks::result_type>>...>>
		{
			if constexpr ((!std::is_same_v<TTasks, Task<typename TTasks::result_type>> || ...))
			{
				// TTasksの中にTaskでないものが1つでも含まれる場合は、ToTaskで変換して呼び出し直す
				co_return co_await Any(ToTask(std::forward<TTasks>(args))...);
			}
			else
			{
				if (detail::Backend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
					co_yield detail::FrameTiming::LateDraw;
					(args.resume(detail::FrameTiming::LateDraw), ...);
					co_yield detail::FrameTiming::Update;
				}
			}
		}

		template <typename TResult>
		class [[nodiscard]] SequenceBase
		{
		public:
			using result_type = TResult;

			SequenceBase() = default;

			SequenceBase(const SequenceBase&) = delete;

			SequenceBase& operator=(const SequenceBase&) = delete;

			SequenceBase(SequenceBase&&) = default;

			SequenceBase& operator=(SequenceBase&&) = default;

			virtual ~SequenceBase() = default;

			virtual Task<TResult> start() = 0;

			virtual void draw() const
			{
			}

			virtual void lateDraw() const
			{
			}
		};

		// 毎フレーム呼ばれるupdate関数を記述するタイプのシーケンス基底クラス
		template <typename TResult>
		class [[nodiscard]] UpdateSequenceBase : public SequenceBase<TResult>
		{
		private:
			std::optional<TResult> m_result;

		protected:
			void finish(const TResult& result)
			{
				m_result = result;
			}

		public:
			[[nodiscard]]
			virtual Task<TResult> start() override final
			{
				// TResultのコピーコストが大きい場合を考慮して、WaitForResultは使わない
				co_await WaitUntil([this] { return m_result.has_value(); }).withUpdate([this] { update(); });
				co_return *m_result;
			}

			virtual void update() = 0;
		};

		// 毎フレーム呼ばれるupdate関数を記述するタイプのシーケンス基底クラス(void特殊化)
		template <>
		class [[nodiscard]] UpdateSequenceBase<void> : public SequenceBase<void>
		{
		private:
			bool m_isFinished;

		protected:
			void finish()
			{
				m_isFinished = true;
			}

		public:
			[[nodiscard]]
			virtual Task<void> start() override final
			{
				co_await WaitUntil([this] { return m_isFinished; }).withUpdate([this] { update(); });
			}

			virtual void update() = 0;
		};

#ifdef __cpp_deleted_function_with_reason
		template <detail::SequenceConcept TSequence>
		auto operator co_await(TSequence&& sequence) = delete("To co_await a Sequence, use Co::AsTask<TSequence>() instead.");

		template <detail::SequenceConcept TSequence>
		auto operator co_await(TSequence& sequence) = delete("To co_await a Sequence, use Co::AsTask<TSequence>() instead.");
#else
		template <detail::SequenceConcept TSequence>
		auto operator co_await(TSequence&& sequence) = delete;

		template <detail::SequenceConcept TSequence>
		auto operator co_await(TSequence& sequence) = delete;
#endif

		namespace detail
		{
			class [[nodiscard]] FadeSequenceBase : public SequenceBase<void>
			{
			private:
				Timer m_timer;
				double m_t = 0.0;

			public:
				explicit FadeSequenceBase(const Duration& duration)
					: m_timer(duration, StartImmediately::No)
				{
				}

				virtual ~FadeSequenceBase() = default;

				Task<void> start() override final
				{
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

				void lateDraw() const override final
				{
					drawFade(m_t);
				}

				// tには時間が0.0～1.0で渡される
				virtual void drawFade(double t) const = 0;
			};

			class [[nodiscard]] SimpleFadeInSequence : public FadeSequenceBase
			{
			private:
				ColorF m_color;

			public:
				explicit SimpleFadeInSequence(const Duration& duration, const ColorF& color)
					: FadeSequenceBase(duration)
					, m_color(color)
				{
				}

				void drawFade(double t) const override
				{
					const Transformer2D transform{ Mat3x2::Identity(), Transformer2D::Target::SetLocal };

					Scene::Rect().draw(ColorF{ m_color, 1.0 - t });
				}
			};

			class [[nodiscard]] SimpleFadeOutSequence : public FadeSequenceBase
			{
			private:
				ColorF m_color;

			public:
				explicit SimpleFadeOutSequence(const Duration& duration, const ColorF& color)
					: FadeSequenceBase(duration)
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

		[[nodiscard]]
		inline Task<void> SimpleFadeIn(const Duration& duration, const ColorF& color = Palette::Black)
		{
			return AsTask<detail::SimpleFadeInSequence>(duration, color);
		}

		[[nodiscard]]
		inline Task<void> SimpleFadeOut(const Duration& duration, const ColorF& color = Palette::Black)
		{
			return AsTask<detail::SimpleFadeOutSequence>(duration, color);
		}

		template <detail::SceneConcept TScene, typename... Args>
		[[nodiscard]]
		SceneFactory MakeSceneFactory(Args&&... args)
		{
			// Args...はコピー構築可能である必要がある
			static_assert((std::is_copy_constructible_v<Args> && ...), "Scene constructor arguments must be copy-constructible to use MakeSceneFactory.");
			return [=] { return std::make_unique<TScene>(args...); };
		}

		[[nodiscard]]
		inline SceneFactory SceneFinish()
		{
			// SceneFactoryがnullptrの場合はシーン遷移を終了する
			return nullptr;
		}

		class [[nodiscard]] SceneBase
		{
		private:
			bool m_isFadingIn = true;
			bool m_isFadingOut = false;

			[[nodiscard]]
			Task<void> fadeInInternal()
			{
				co_await fadeIn();
				m_isFadingIn = false;
			}

			[[nodiscard]]
			Task<void> fadeOutInternal()
			{
				m_isFadingOut = true;
				co_await fadeOut();
			}

			[[nodiscard]]
			Task<SceneFactory> startAndFadeOut()
			{
				SceneFactory nextSceneFactory = co_await start();
				co_await fadeOutInternal();
				co_return std::move(nextSceneFactory);
			}

		public:
			SceneBase() = default;

			SceneBase(const SceneBase&) = delete;

			SceneBase& operator=(const SceneBase&) = delete;

			SceneBase(SceneBase&&) = default;

			SceneBase& operator=(SceneBase&&) = default;

			virtual ~SceneBase() = default;

			// 戻り値は次シーンのSceneFactoryをCo::MakeSceneFactory<TScene>()で作成して返す
			// もしくは、Co::SceneFinish()を返してシーン遷移を終了する
			[[nodiscard]]
			virtual Task<SceneFactory> start() = 0;

			virtual void draw() const
			{
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
			Task<void> waitForFadeIn()
			{
				while (m_isFadingIn)
				{
					co_yield detail::FrameTiming::Update;
				}
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
			Task<SceneFactory> run()
			{
				co_return co_await startAndFadeOut()
					.withDraw([this] { draw(); })
					.with(fadeInInternal(), WithTiming::Before);
			}
		};

		// 毎フレーム呼ばれるupdate関数を記述するタイプのシーン基底クラス
		class [[nodiscard]] UpdateSceneBase : public SceneBase
		{
		private:
			std::optional<SceneFactory> m_nextSceneFactory;

		protected:
			template <class TScene, typename... Args>
			void requestNextScene(Args&&... args)
			{
				m_nextSceneFactory = MakeSceneFactory<TScene>(std::forward<Args>(args)...);
			}

			void requestSceneFinish()
			{
				m_nextSceneFactory = SceneFinish();
			}

		public:
			[[nodiscard]]
			virtual Task<SceneFactory> start() override final
			{
				co_return co_await WaitForResult(&m_nextSceneFactory).withUpdate([this] { update(); });
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
					const SceneFactory nextSceneFactory = co_await currentScene->run();

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

		template <detail::SceneConcept TScene>
		[[nodiscard]]
		Task<void> ToTask(TScene&& scene)
		{
			return detail::ScenePtrToTask(std::make_unique<TScene>(std::move(scene)));
		}

		template <detail::SceneConcept TScene, class... Args>
		[[nodiscard]]
		Task<void> AsTask(Args&&... args)
		{
			return detail::ScenePtrToTask(std::make_unique<TScene>(std::forward<Args>(args)...));
		}

		inline Task<void> AsTask(SceneFactory sceneFactory)
		{
			auto scenePtr = sceneFactory();
			return detail::ScenePtrToTask(std::move(scenePtr));
		}

#ifdef __cpp_deleted_function_with_reason
		template <detail::SceneConcept TScene>
		auto operator co_await(TScene&& scene) = delete("To co_await a Scene, use Co::AsTask<TScene>() instead.");

		template <detail::SceneConcept TScene>
		auto operator co_await(TScene& scene) = delete("To co_await a Scene, use Co::AsTask<TScene>() instead.");

		auto operator co_await(SceneFactory sceneFactory) = delete("To co_await a Scene created by a SceneFactory, use Co::AsTask(SceneFactory) instead.");
#else
		template <detail::SceneConcept TScene>
		auto operator co_await(TScene&& scene) = delete;

		template <detail::SceneConcept TScene>
		auto operator co_await(TScene& scene) = delete;

		auto operator co_await(SceneFactory sceneFactory) = delete;
#endif
	}
}
