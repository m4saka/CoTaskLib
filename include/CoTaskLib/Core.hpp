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
namespace cotasklib
#else
inline namespace cotasklib
#endif
{
	namespace Co
	{
		namespace detail
		{
			class IAwaiter
			{
			public:
				virtual ~IAwaiter() = default;

				virtual void resume() = 0;

				virtual bool isFinished() const = 0;
			};

			struct AwaiterEntry
			{
				std::unique_ptr<IAwaiter> awaiter;
				std::function<void(const IAwaiter*)> finishCallback;
				std::function<void()> cancelCallback;

				void callEndCallback() const
				{
					if (awaiter->isFinished())
					{
						if (finishCallback)
						{
							finishCallback(awaiter.get());
						}
					}
					else
					{
						if (cancelCallback)
						{
							cancelCallback();
						}
					}
				}
			};

			using AwaiterID = uint64;

			using UpdaterID = uint64;

			using DrawerID = uint64;

			template <typename TResult>
			class TaskAwaiter;

			template <typename TResult>
			struct FinishCallbackTypeTrait
			{
				using type = std::function<void(const TResult&)>;
			};

			template <>
			struct FinishCallbackTypeTrait<void>
			{
				using type = std::function<void()>;
			};
		}

		template <typename TResult>
		using FinishCallbackType = typename detail::FinishCallbackTypeTrait<TResult>::type;

		template <typename TResult>
		class Task;

		class SceneBase;

		using SceneFactory = std::function<std::unique_ptr<SceneBase>()>;

		namespace detail
		{
			template <typename IDType>
			class OrderedExecutor
			{
			private:
				struct CallerKey
				{
					IDType id;
					int32 sortingOrder;

					CallerKey(IDType id, int32 sortingOrder)
						: id(id)
						, sortingOrder(sortingOrder)
					{
					}

					CallerKey(const CallerKey&) = default;

					CallerKey& operator=(const CallerKey&) = default;

					CallerKey(CallerKey&&) = default;

					CallerKey& operator=(CallerKey&&) = default;

					bool operator<(const CallerKey& other) const
					{
						if (sortingOrder != other.sortingOrder)
						{
							return sortingOrder < other.sortingOrder;
						}
						return id < other.id;
					}
				};

				struct Caller
				{
					std::function<void()> func;
					std::function<int32()> sortingOrderFunc;

					Caller(std::function<void()> func, std::function<int32()> sortingOrderFunc)
						: func(std::move(func))
						, sortingOrderFunc(std::move(sortingOrderFunc))
					{
					}

					Caller(const Caller&) = default;

					Caller& operator=(const Caller&) = default;
				};

				IDType m_nextID = 1;
				std::map<CallerKey, Caller> m_callers;
				std::unordered_map<IDType, CallerKey> m_callerKeyByID;
				std::vector<std::pair<CallerKey, int32>> m_tempNewSortingOrders;

				using CallersIterator = typename decltype(m_callers)::iterator;

				void refreshSortingOrder()
				{
					m_tempNewSortingOrders.clear();

					// まず、sortingOrderの変更をリストアップ
					for (const auto& [key, caller] : m_callers)
					{
						const int32 newSortingOrder = caller.sortingOrderFunc();
						if (newSortingOrder != key.sortingOrder)
						{
							m_tempNewSortingOrders.emplace_back(key, newSortingOrder);
						}
					}

					// sortingOrderに変更があったものを再挿入
					for (const auto& [oldKey, newSortingOrder] : m_tempNewSortingOrders)
					{
						const IDType id = oldKey.id;
						const auto it = m_callers.find(oldKey);
						if (it == m_callers.end())
						{
							throw Error{ U"OrderedExecutor::refreshSortingOrder: ID={} not found"_fmt(id) };
						}
						Caller newCaller = it->second;
						m_callers.erase(it);
						const auto [newIt, inserted] = m_callers.insert(std::make_pair(CallerKey{ id, newSortingOrder }, std::move(newCaller)));
						if (!inserted)
						{
							throw Error{ U"OrderedExecutor::refreshSortingOrder: ID={} cannot be inserted"_fmt(id) };
						}
						m_callerKeyByID.insert_or_assign(id, newIt->first);
					}
				}

			public:
				IDType add(std::function<void()> func, std::function<int32()> sortingOrderFunc)
				{
					const int32 sortingOrder = sortingOrderFunc();
					const auto [it, inserted] = m_callers.insert(std::make_pair(CallerKey{ m_nextID, sortingOrder }, Caller{ std::move(func), std::move(sortingOrderFunc) }));
					if (!inserted)
					{
						throw Error{ U"OrderedExecutor::add: ID={} cannot be inserted"_fmt(m_nextID) };
					}
					if (m_callerKeyByID.contains(m_nextID))
					{
						throw Error{ U"OrderedExecutor::add: ID={} inconsistency detected"_fmt(m_nextID) };
					}
					m_callerKeyByID.insert_or_assign(m_nextID, it->first);
					return m_nextID++;
				}

				CallersIterator findByID(IDType id)
				{
					const auto it = m_callerKeyByID.find(id);
					if (it == m_callerKeyByID.end())
					{
						return m_callers.end();
					}
					return m_callers.find(it->second);
				}

				void remove(IDType id)
				{
					const auto it = findByID(id);
					if (it == m_callers.end())
					{
						if (m_callerKeyByID.contains(id))
						{
							throw Error{ U"OrderedExecutor::remove: ID={} inconsistency detected"_fmt(id) };
						}
						return;
					}
					m_callers.erase(it);
					m_callerKeyByID.erase(id);
				}

				void call()
				{
					refreshSortingOrder();

					for (const auto& [key, caller] : m_callers)
					{
						caller.func();
					}
				}
			};

			class Backend
			{
			private:
				static constexpr StringView AddonName{ U"Co::BackendAddon" };

				static inline Backend* s_pInstance = nullptr;

				// Note: draw関数がconstであることの対処用にアドオンと実体を分離し、実体はポインタで持つようにしている
				class BackendAddon : public IAddon
				{
				private:
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
						m_instance->update();
						return true;
					}

					virtual void draw() const override
					{
						m_instance->draw();
					}

					[[nodiscard]]
					Backend* instance() const
					{
						return m_instance.get();
					}
				};

				AwaiterID m_nextAwaiterID = 1;

				Optional<AwaiterID> m_currentAwaiterID = none;

				bool m_currentAwaiterRemovalNeeded = false;

				std::map<AwaiterID, AwaiterEntry> m_awaiterEntries;

				OrderedExecutor<DrawerID> m_drawExecutor;

				SceneFactory m_currentSceneFactory;

			public:
				Backend() = default;

				void update()
				{
					for (auto it = m_awaiterEntries.begin(); it != m_awaiterEntries.end();)
					{
						m_currentAwaiterID = it->first;

						const auto& entry = it->second;
						entry.awaiter->resume();
						if (m_currentAwaiterRemovalNeeded || entry.awaiter->isFinished())
						{
							entry.callEndCallback();
							it = m_awaiterEntries.erase(it);
							m_currentAwaiterRemovalNeeded = false;
						}
						else
						{
							++it;
						}
					}
					m_currentAwaiterID.reset();
				}

				void draw()
				{
					m_drawExecutor.call();
				}

				static void Init()
				{
					Addon::Register(AddonName, std::make_unique<BackendAddon>());
				}

				template <typename TResult>
				[[nodiscard]]
				static AwaiterID Add(std::unique_ptr<TaskAwaiter<TResult>>&& awaiter, FinishCallbackType<TResult> finishCallback, std::function<void()> cancelCallback)
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
					std::function<void(const IAwaiter*)> finishCallbackTypeErased = nullptr;
					if (finishCallback)
					{
						finishCallbackTypeErased = [finishCallback = std::move(finishCallback)]([[maybe_unused]] const IAwaiter* awaiter)
							{
								if constexpr (std::is_void_v<TResult>)
								{
									finishCallback();
								}
								else
								{
									// awaiterの型がTaskAwaiter<TResult>であることは保証されるため、static_castでキャストして問題ない
									finishCallback(static_cast<const TaskAwaiter<TResult>*>(awaiter)->value());
								}
							};
					}
					s_pInstance->m_awaiterEntries.emplace(id,
						AwaiterEntry
						{
							.awaiter = std::move(awaiter),
							.finishCallback = std::move(finishCallbackTypeErased),
							.cancelCallback = std::move(cancelCallback),
						});
					return id;
				}

				static void Remove(AwaiterID id)
				{
					if (!s_pInstance)
					{
						// Note: ユーザーがインスタンスをstaticで持ってしまった場合にAddon解放後に呼ばれるケースが起こりうるので、ここでは例外を出さない
						return;
					}
					if (id == s_pInstance->m_currentAwaiterID)
					{
						// 実行中タスクのAwaiterをここで削除するとアクセス違反やイテレータ破壊が起きるため、代わりに削除フラグを立てて実行完了時に削除
						// (例えば、タスク実行のライフタイムをOptional<ScopedTaskRunner>型のメンバ変数として持ち、タスク実行中にそこへnoneを代入して実行を止める場合が該当)
						s_pInstance->m_currentAwaiterRemovalNeeded = true;
						return;
					}
					const auto it = s_pInstance->m_awaiterEntries.find(id);
					if (it != s_pInstance->m_awaiterEntries.end())
					{
						it->second.callEndCallback();
						s_pInstance->m_awaiterEntries.erase(it);
					}
				}

				[[nodiscard]]
				static bool IsFinished(AwaiterID id)
				{
					if (!s_pInstance)
					{
						throw Error{ U"Backend is not initialized" };
					}
					const auto it = s_pInstance->m_awaiterEntries.find(id);
					if (it != s_pInstance->m_awaiterEntries.end())
					{
						return it->second.awaiter->isFinished();
					}
					return id < s_pInstance->m_nextAwaiterID;
				}

				[[nodiscard]]
				static DrawerID AddDrawer(std::function<void()> func, std::function<int32()> drawIndexFunc)
				{
					if (!s_pInstance)
					{
						throw Error{ U"Backend is not initialized" };
					}
					return s_pInstance->m_drawExecutor.add(std::move(func), std::move(drawIndexFunc));
				}

				static void RemoveDrawer(DrawerID id)
				{
					if (!s_pInstance)
					{
						// Note: ユーザーがインスタンスをstaticで持ってしまった場合にAddon解放後に呼ばれるケースが起こりうるので、ここでは例外を出さない
						return;
					}
					s_pInstance->m_drawExecutor.remove(id);
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
				Optional<AwaiterID> m_id;

			public:
				explicit ScopedTaskRunLifetime(const Optional<AwaiterID>& id)
					: m_id(id)
				{
				}

				ScopedTaskRunLifetime(const ScopedTaskRunLifetime&) = delete;

				ScopedTaskRunLifetime& operator=(const ScopedTaskRunLifetime&) = delete;

				ScopedTaskRunLifetime(ScopedTaskRunLifetime&& rhs) noexcept
					: m_id(rhs.m_id)
				{
					rhs.m_id.reset();
				}

				ScopedTaskRunLifetime& operator=(ScopedTaskRunLifetime&& rhs) noexcept
				{
					if (this != &rhs)
					{
						if (m_id.has_value())
						{
							Backend::Remove(*m_id);
						}
						m_id = rhs.m_id;
						rhs.m_id.reset();
					}
					return *this;
				}

				~ScopedTaskRunLifetime()
				{
					if (m_id.has_value())
					{
						Backend::Remove(*m_id);
					}
				}

				[[nodiscard]]
				bool isFinished() const
				{
					return !m_id.has_value() || Backend::IsFinished(*m_id);
				}

				void forget()
				{
					m_id.reset();
				}
			};

			template <typename TResult>
			[[nodiscard]]
			Optional<AwaiterID> ResumeAwaiterOnceAndRegisterIfNotDone(TaskAwaiter<TResult>&& awaiter, FinishCallbackType<TResult> finishCallback, std::function<void()> cancelCallback)
			{
				const auto fnCallFinishCallback = [&]
					{
						if (finishCallback)
						{
							if constexpr (std::is_void_v<TResult>)
							{
								finishCallback();
							}
							else
							{
								finishCallback(awaiter.value());
							}
						}
					};

				// フレーム待ちなしで終了した場合は登録不要
				// (ここで一度resumeするのは、runScoped実行まで開始を遅延させるためにinitial_suspendをsuspend_alwaysにしているため)
				if (awaiter.isFinished())
				{
					fnCallFinishCallback();
					return none;
				}
				awaiter.resume();
				if (awaiter.isFinished())
				{
					fnCallFinishCallback();
					return none;
				}

				return Backend::Add(std::make_unique<TaskAwaiter<TResult>>(std::move(awaiter)), std::move(finishCallback), std::move(cancelCallback));
			}

			template <typename TResult>
			Optional<AwaiterID> ResumeAwaiterOnceAndRegisterIfNotDone(const TaskAwaiter<TResult>& awaiter) = delete;
		}

		class MultiScoped;

		namespace detail
		{
			class IScoped
			{
			public:
				IScoped() = default;
				virtual ~IScoped() = default;
				IScoped(const IScoped&) = delete;
				IScoped& operator=(const IScoped&) = delete;
				IScoped(IScoped&&) = default;
				IScoped& operator=(IScoped&&) = default;

				virtual void addTo(MultiScoped& ms) && = 0;
			};
		}

		class MultiScoped
		{
		private:
			std::vector<std::unique_ptr<detail::IScoped>> m_scopedInstances;

		public:
			MultiScoped() = default;

			MultiScoped(const MultiScoped&) = delete;

			MultiScoped& operator=(const MultiScoped&) = delete;

			MultiScoped(MultiScoped&&) = default;

			MultiScoped& operator=(MultiScoped&&) = default;

			~MultiScoped() = default;

			template <typename TScoped>
			void add(TScoped&& runner) requires std::derived_from<TScoped, detail::IScoped>
			{
				m_scopedInstances.push_back(std::make_unique<TScoped>(std::forward<TScoped>(runner)));
			}

			void reserve(std::size_t size)
			{
				m_scopedInstances.reserve(size);
			}

			void clear()
			{
				m_scopedInstances.clear();
			}
		};

		class ScopedTaskRunner : public detail::IScoped
		{
		private:
			detail::ScopedTaskRunLifetime m_lifetime;

		public:
			template <typename TResult>
			explicit ScopedTaskRunner(Task<TResult>&& task, FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr)
				: m_lifetime(ResumeAwaiterOnceAndRegisterIfNotDone(detail::TaskAwaiter<TResult>{ std::move(task) }, std::move(finishCallback), std::move(cancelCallback)))
			{
			}

			ScopedTaskRunner(const ScopedTaskRunner&) = delete;

			ScopedTaskRunner& operator=(const ScopedTaskRunner&) = delete;

			ScopedTaskRunner(ScopedTaskRunner&&) = default;

			ScopedTaskRunner& operator=(ScopedTaskRunner&&) = default;

			~ScopedTaskRunner() = default;

			[[nodiscard]]
			bool isFinished() const
			{
				return m_lifetime.isFinished();
			}

			void forget()
			{
				m_lifetime.forget();
			}

			void addTo(MultiScoped& ms) && override
			{
				ms.add(std::move(*this));
			}
		};

		namespace DrawIndex
		{
			constexpr int32 Back = -1;
			constexpr int32 Default = 0;
			constexpr int32 Front = 1;
			constexpr int32 FadeIn = 100000;
			constexpr int32 FadeOut = 200000;
		}

		class ScopedDrawer : public detail::IScoped
		{
		private:
			Optional<detail::DrawerID> m_id;

		public:
			ScopedDrawer(std::function<void()> func)
				: m_id(detail::Backend::AddDrawer(std::move(func), [] { return DrawIndex::Default; }))
			{
			}

			ScopedDrawer(std::function<void()> func, int32 drawIndex)
				: m_id(detail::Backend::AddDrawer(std::move(func), [drawIndex] { return drawIndex; }))
			{
			}

			ScopedDrawer(std::function<void()> func, std::function<int32()> drawIndexFunc)
				: m_id(detail::Backend::AddDrawer(std::move(func), std::move(drawIndexFunc)))
			{
			}

			ScopedDrawer(const ScopedDrawer&) = delete;

			ScopedDrawer& operator=(const ScopedDrawer&) = delete;

			ScopedDrawer(ScopedDrawer&& rhs) noexcept
				: m_id(rhs.m_id)
			{
				rhs.m_id.reset();
			}

			ScopedDrawer& operator=(ScopedDrawer&& rhs) noexcept
			{
				if (this != &rhs)
				{
					if (m_id.has_value())
					{
						detail::Backend::RemoveDrawer(*m_id);
					}
					m_id = rhs.m_id;
					rhs.m_id.reset();
				}
				return *this;
			}

			~ScopedDrawer()
			{
				if (m_id.has_value())
				{
					detail::Backend::RemoveDrawer(*m_id);
				}
			}

			void addTo(MultiScoped& ms)&& override
			{
				ms.add(std::move(*this));
			}
		};

		namespace detail
		{
			struct Yield
			{
				bool await_ready() const
				{
					return false;
				}

				void await_suspend(std::coroutine_handle<>) const
				{
				}

				void await_resume() const
				{
				}
			};

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

				void resume() const
				{
					if (done())
					{
						return;
					}

					if (m_handle.promise().resumeSubAwaiter())
					{
						return;
					}

					m_handle.resume();
					m_handle.promise().rethrowIfException();
				}
			};
		}

		enum class WithTiming
		{
			// with実行タイミングで既に初回resumeは既に完了してしまっているので、
			// もし初回resumeも前に実行する必要がある場合は、withに渡す子Taskを
			// 親Taskより先に生成し、withへ子タスクをstd::moveで渡す必要がある点に注意
			Before,

			After,
		};

		class [[nodiscard]] ITask
		{
		public:
			virtual ~ITask() = default;

			virtual void resume() = 0;

			[[nodiscard]]
			virtual bool isFinished() const = 0;
		};

		template <typename TResult = void>
		class [[nodiscard]] Task : public ITask
		{
			static_assert(!std::is_reference_v<TResult>, "TResult must not be a reference type");
			static_assert(std::is_copy_constructible_v<TResult> || std::is_void_v<TResult>, "TResult must be copy constructible");
			static_assert(!std::is_const_v<TResult>, "TResult must not have 'const' qualifier");

		private:
			detail::CoroutineHandleWrapper<TResult> m_handle;
			std::vector<std::unique_ptr<ITask>> m_concurrentTasksBefore;
			std::vector<std::unique_ptr<ITask>> m_concurrentTasksAfter;

		public:
			using promise_type = detail::Promise<TResult>;
			using handle_type = std::coroutine_handle<promise_type>;
			using result_type = TResult;
			using finish_callback_type = FinishCallbackType<TResult>;

			explicit Task(handle_type h)
				: m_handle(std::move(h))
			{
			}

			Task(const Task<TResult>&) = delete;

			Task<TResult>& operator=(const Task<TResult>&) = delete;

			Task(Task<TResult>&& rhs) = default;

			Task<TResult>& operator=(Task<TResult>&& rhs) = default;

			virtual void resume()
			{
				if (m_handle.done())
				{
					return;
				}

				for (auto& task : m_concurrentTasksBefore)
				{
					task->resume();
				}

				m_handle.resume();

				for (auto& task : m_concurrentTasksAfter)
				{
					task->resume();
				}
			}

			[[nodiscard]]
			bool isFinished() const override
			{
				return m_handle.done();
			}

			[[nodiscard]]
			TResult value() const
			{
				return m_handle.value();
			}

			template <typename TResultOther>
			[[nodiscard]]
			Task<TResult> with(Task<TResultOther>&& task)&&
			{
				m_concurrentTasksAfter.push_back(std::make_unique<Task<TResultOther>>(std::move(task)));
				return std::move(*this);
			}

			template <typename TResultOther>
			[[nodiscard]]
			Task<TResult> with(Task<TResultOther>&& task, WithTiming timing)&&
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
			ScopedTaskRunner runScoped(FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr)&&
			{
				return ScopedTaskRunner{ std::move(*this), std::move(finishCallback), std::move(cancelCallback) };
			}
			
			[[nodiscard]]
			void runAddTo(MultiScoped& ms, FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr)&&
			{
				ms.add(ScopedTaskRunner{ std::move(*this), std::move(finishCallback), std::move(cancelCallback) });
			}
		};

		namespace detail
		{
			template <typename TResult>
			class [[nodiscard]] TaskAwaiter : public detail::IAwaiter
			{
				static_assert(!std::is_reference_v<TResult>, "TResult must not be a reference type");
				static_assert(std::is_copy_constructible_v<TResult> || std::is_void_v<TResult>, "TResult must be copy constructible");
				static_assert(!std::is_const_v<TResult>, "TResult must not have 'const' qualifier");

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

				void resume() override
				{
					m_task.resume();
				}

				[[nodiscard]]
				bool isFinished() const override
				{
					return m_task.isFinished();
				}

				[[nodiscard]]
				bool await_ready()
				{
					return m_task.isFinished();
				}

				template <typename TResultOther>
				bool await_suspend(std::coroutine_handle<detail::Promise<TResultOther>> handle)
				{
					resume();
					if (m_task.isFinished())
					{
						// フレーム待ちなしで終了した場合は登録不要
						return false;
					}
					handle.promise().setSubAwaiter(this);
					return true;
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

			class PromiseBase
			{
			protected:
				IAwaiter* m_pSubAwaiter = nullptr;

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
					// suspend_neverにすれば関数呼び出し時点で実行開始されるが、
					// その場合Co::AllやCo::Anyに渡す際など引数の評価順が不定になり扱いづらいため、
					// ここではsuspend_alwaysにしてrunScoped実行まで実行を遅延させている
					return std::suspend_always{};
				}

				[[nodiscard]]
				auto final_suspend() noexcept
				{
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
				bool resumeSubAwaiter()
				{
					if (!m_pSubAwaiter)
					{
						return false;
					}

					m_pSubAwaiter->resume();

					if (m_pSubAwaiter->isFinished())
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
			};

			inline PromiseBase::~PromiseBase() = default;

			template <typename TResult>
			class Promise : public PromiseBase
			{
				static_assert(!std::is_reference_v<TResult>, "TResult must not be a reference type");
				static_assert(std::is_copy_constructible_v<TResult> || std::is_void_v<TResult>, "TResult must be copy constructible");
				static_assert(!std::is_const_v<TResult>, "TResult must not have 'const' qualifier");

			private:
				Optional<TResult> m_value;

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

		// voidの参照やvoidを含むタプルは使用できないため、voidの代わりに戻り値として返すための空の構造体を用意
		struct VoidResult
		{
		};

		namespace detail
		{
			template <typename TResult>
			using VoidResultTypeReplace = std::conditional_t<std::is_void_v<TResult>, VoidResult, TResult>;
		}

		template <typename TResult = void>
		class [[nodiscard]] TaskFinishSource
		{
			static_assert(!std::is_reference_v<TResult>, "TResult must not be a reference type");
			static_assert(std::is_copy_constructible_v<TResult> || std::is_void_v<TResult>, "TResult must be copy constructible");
			static_assert(!std::is_const_v<TResult>, "TResult must not have 'const' qualifier");

		private:
			Optional<TResult> m_result;

		public:
			TaskFinishSource() = default;

			TaskFinishSource(const TaskFinishSource&) = delete;

			TaskFinishSource& operator=(const TaskFinishSource&) = delete;

			TaskFinishSource(TaskFinishSource&&) = default;

			TaskFinishSource& operator=(TaskFinishSource&&) = default;

			~TaskFinishSource() = default;

			void requestFinish(const TResult& result)
			{
				if (m_result.has_value())
				{
					return;
				}
				m_result = result;
			}

			void requestFinish(TResult&& result)
			{
				if (m_result.has_value())
				{
					return;
				}
				m_result = std::move(result);
			}

			[[nodiscard]]
			Task<TResult> waitForFinish() const
			{
				while (!m_result.has_value())
				{
					co_await detail::Yield{};
				}

				co_return *m_result;
			}

			[[nodiscard]]
			bool isFinishRequested() const
			{
				return m_result.has_value();
			}

			[[nodiscard]]
			bool hasResult() const
			{
				return m_result.has_value();
			}

			[[nodiscard]]
			const TResult& result() const
			{
				return *m_result;
			}

			[[nodiscard]]
			const Optional<TResult>& resultOpt() const
			{
				return m_result;
			}
		};

		template <>
		class [[nodiscard]] TaskFinishSource<void>
		{
		private:
			Optional<VoidResult> m_result;

		public:
			TaskFinishSource() = default;

			TaskFinishSource(const TaskFinishSource&) = delete;

			TaskFinishSource& operator=(const TaskFinishSource&) = delete;

			TaskFinishSource(TaskFinishSource&&) = default;

			TaskFinishSource& operator=(TaskFinishSource&&) = default;

			~TaskFinishSource() = default;

			void requestFinish()
			{
				m_result = VoidResult{};
			}

			[[nodiscard]]
			Task<void> waitForFinish() const
			{
				while (!m_result.has_value())
				{
					co_await detail::Yield{};
				}
			}

			[[nodiscard]]
			bool isFinishRequested() const
			{
				return m_result.has_value();
			}

			[[nodiscard]]
			bool hasResult() const
			{
				return m_result.has_value();
			}

			[[nodiscard]]
			const VoidResult& result() const
			{
				return *m_result;
			}

			[[nodiscard]]
			const Optional<VoidResult>& resultOpt() const
			{
				return m_result;
			}
		};

		[[nodiscard]]
		inline Task<void> UpdaterTask(std::function<void()> updateFunc)
		{
			while (true)
			{
				updateFunc();
				co_await detail::Yield{};
			}
		}

		template <typename TResult>
		[[nodiscard]]
		Task<TResult> UpdaterTask(std::function<void(TaskFinishSource<TResult>&)> updateFunc)
		{
			TaskFinishSource<TResult> taskFinishSource;

			while (true)
			{
				updateFunc(taskFinishSource);
				if (taskFinishSource.hasResult())
				{
					co_return taskFinishSource.result();
				}
				co_await detail::Yield{};
			}
		}

		template <>
		[[nodiscard]]
		inline Task<void> UpdaterTask(std::function<void(TaskFinishSource<void>&)> updateFunc)
		{
			TaskFinishSource<void> taskFinishSource;

			while (true)
			{
				updateFunc(taskFinishSource);
				if (taskFinishSource.isFinishRequested())
				{
					co_return;
				}
				co_await detail::Yield{};
			}
		}

		class ScopedUpdater : public detail::IScoped
		{
		private:
			ScopedTaskRunner m_runner;

		public:
			explicit ScopedUpdater(std::function<void()> func)
				: m_runner(UpdaterTask(std::move(func)))
			{
			}

			ScopedUpdater(const ScopedUpdater&) = delete;

			ScopedUpdater& operator=(const ScopedUpdater&) = delete;

			ScopedUpdater(ScopedUpdater&&) = default;

			ScopedUpdater& operator=(ScopedUpdater&&) = default;

			~ScopedUpdater() = default;

			void addTo(MultiScoped& ms)&&
			{
				ms.add(std::move(m_runner));
			}
		};

		template <typename TResult>
		auto operator co_await(Task<TResult>&& rhs)
		{
			return detail::TaskAwaiter<TResult>{ std::move(rhs) };
		}

		template <typename TResult>
		auto operator co_await(const Task<TResult>& rhs) = delete;

		inline void Init()
		{
			detail::Backend::Init();
		}

		[[nodiscard]]
		inline Task<void> DelayFrame()
		{
			co_await detail::Yield{};
		}

		[[nodiscard]]
		inline Task<void> DelayFrame(int32 frames)
		{
			for (int32 i = 0; i < frames; ++i)
			{
				co_await detail::Yield{};
			}
		}

		[[nodiscard]]
		inline Task<void> Delay(const Duration duration, ISteadyClock* pSteadyClock = nullptr)
		{
			const Timer timer{ duration, StartImmediately::Yes, pSteadyClock };
			while (!timer.reachedZero())
			{
				co_await detail::Yield{};
			}
		}

		[[nodiscard]]
		inline Task<void> WaitForever()
		{
			while (true)
			{
				co_await detail::Yield{};
			}
		}

		[[nodiscard]]
		inline Task<void> WaitUntil(std::function<bool()> predicate)
		{
			while (!predicate())
			{
				co_await detail::Yield{};
			}
		}

		template <typename T>
		[[nodiscard]]
		inline Task<T> WaitForResult(const std::optional<T>* pOptional)
		{
			while (!pOptional->has_value())
			{
				co_await detail::Yield{};
			}

			co_return **pOptional;
		}

		template <typename T>
		[[nodiscard]]
		inline Task<T> WaitForResult(const Optional<T>* pOptional)
		{
			while (!pOptional->has_value())
			{
				co_await detail::Yield{};
			}

			co_return **pOptional;
		}

		[[nodiscard]]
		inline Task<void> WaitForTimer(const Timer* pTimer)
		{
			while (!pTimer->reachedZero())
			{
				co_await detail::Yield{};
			}
		}

		template <class TInput>
		[[nodiscard]]
		Task<void> WaitForDown(const TInput input)
		{
			while (!input.down())
			{
				co_await detail::Yield{};
			}
		}

		template <class TInput>
		[[nodiscard]]
		Task<void> WaitForUp(const TInput input)
		{
			while (!input.up())
			{
				co_await detail::Yield{};
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForLeftClicked(const TArea area)
		{
			while (!area.leftClicked())
			{
				co_await detail::Yield{};
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForLeftReleased(const TArea area)
		{
			while (!area.leftReleased())
			{
				co_await detail::Yield{};
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForLeftClickedThenReleased(const TArea area)
		{
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
				co_await detail::Yield{};
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForRightClicked(const TArea area)
		{
			while (!area.rightClicked())
			{
				co_await detail::Yield{};
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForRightReleased(const TArea area)
		{
			while (!area.rightReleased())
			{
				co_await detail::Yield{};
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForRightClickedThenReleased(const TArea area)
		{
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
				co_await detail::Yield{};
			}
		}

		template <class TArea>
		[[nodiscard]]
		Task<void> WaitForMouseOver(const TArea area)
		{
			while (!area.mouseOver())
			{
				co_await detail::Yield{};
			}
		}

		[[nodiscard]]
		inline Task<void> WaitWhile(std::function<bool()> predicate)
		{
			while (predicate())
			{
				co_await detail::Yield{};
			}
		}

		namespace detail
		{
			template <typename TResult>
			[[nodiscard]]
			auto ConvertVoidResult(const Task<TResult>& task) -> VoidResultTypeReplace<TResult>
			{
				if constexpr (std::is_void_v<TResult>)
				{
					return VoidResult{};
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
				if (!task.isFinished())
				{
					return none;
				}

				if constexpr (std::is_void_v<TResult>)
				{
					return MakeOptional(VoidResult{});
				}
				else
				{
					return MakeOptional(task.value());
				}
			}

			template <typename TTask>
			concept TaskConcept = std::is_same_v<TTask, Task<typename TTask::result_type>>;

			template <typename TScene>
			concept SceneConcept = std::derived_from<TScene, SceneBase>;
		}

		template <detail::TaskConcept... TTasks>
		auto All(TTasks... args) -> Task<std::tuple<detail::VoidResultTypeReplace<typename TTasks::result_type>...>>
		{
			if ((args.isFinished() && ...))
			{
				co_return std::make_tuple(detail::ConvertVoidResult(args)...);
			}

			while (true)
			{
				(args.resume(), ...);
				if ((args.isFinished() && ...))
				{
					co_return std::make_tuple(detail::ConvertVoidResult(args)...);
				}
				co_await detail::Yield{};
			}
		}

		template <detail::TaskConcept... TTasks>
		auto Any(TTasks... args) -> Task<std::tuple<Optional<detail::VoidResultTypeReplace<typename TTasks::result_type>>...>>
		{
			if ((args.isFinished() || ...))
			{
				co_return std::make_tuple(detail::ConvertOptionalVoidResult(args)...);
			}

			while (true)
			{
				(args.resume(), ...);
				if ((args.isFinished() || ...))
				{
					co_return std::make_tuple(detail::ConvertOptionalVoidResult(args)...);
				}
				co_await detail::Yield{};
			}
		}
	}
}
