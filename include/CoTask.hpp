#pragma once
#include <Siv3D.hpp>
#include <coroutine>

namespace cotask
{
	namespace detail
	{
		class ICoTask
		{
		public:
			virtual ~ICoTask() = default;

			virtual void update() = 0;

			virtual bool done() const = 0;
		};

		using TaskID = uint64;
	}

	template <typename T>
	class CoTask;

	class CoTaskBackend
	{
	private:
		static constexpr StringView AddonName{ U"CoTaskBackendAddon" };

		// Note: draw関数がconstであることの対処用にアドオンと実体を分離し、実体はポインタで持つようにしている
		class CoTaskBackendAddon : public IAddon
		{
		private:
			bool m_isFirstUpdated = false;
			std::unique_ptr<CoTaskBackend> m_instance;

		public:
			CoTaskBackendAddon()
				: m_instance{ std::unique_ptr<CoTaskBackend>{ new CoTaskBackend{} } } // Note: コンストラクタがprivateなのでmake_unique不使用
			{
			}

			virtual bool update() override
			{
				m_isFirstUpdated = true;
				m_instance->update();
				return true;
			}

			virtual void draw() const override
			{
				if (!m_isFirstUpdated)
				{
					// Addonの初回drawがupdateより先に実行される挙動を回避
					return;
				}
				m_instance->draw();
			}

			[[nodiscard]]
			CoTaskBackend* instance() const
			{
				return m_instance.get();
			}
		};

		detail::TaskID m_nextExecID = 1;

		Optional<detail::TaskID> m_currentRunningTaskID = none;

		std::map<detail::TaskID, std::unique_ptr<detail::ICoTask>> m_updateTasks;
		std::map<detail::TaskID, std::unique_ptr<detail::ICoTask>> m_drawTasks;

		CoTaskBackend() = default;

		[[nodiscard]]
		static CoTaskBackend* Instance()
		{
			if (const auto* pAddon = Addon::GetAddon<CoTaskBackendAddon>(AddonName))
			{
				return pAddon->instance();
			}
			return nullptr;
		}

		void updateTasksImpl(std::map<detail::TaskID, std::unique_ptr<detail::ICoTask>>& tasks)
		{
			for (auto it = tasks.begin(); it != tasks.end();)
			{
				m_currentRunningTaskID = it->first;

				it->second->update();
				if (it->second->done())
				{
					it = tasks.erase(it);
				}
				else
				{
					++it;
				}
			}
			m_currentRunningTaskID = none;
		}

	public:
		void update()
		{
			updateTasksImpl(m_updateTasks);
		}

		void draw()
		{
			updateTasksImpl(m_drawTasks);
		}

		static void Init()
		{
			Addon::Register(AddonName, std::make_unique<CoTaskBackendAddon>());
		}

		[[nodiscard]]
		static detail::TaskID RegisterTask(std::unique_ptr<detail::ICoTask>&& task)
		{
			if (!task)
			{
				throw Error{ U"CoTask is nullptr" };
			}

			auto* pInstance = Instance();
			if (!pInstance)
			{
				throw Error{ U"CoTaskBackend is not initialized" };
			}
			const detail::TaskID id = pInstance->m_nextExecID++;
			pInstance->m_updateTasks.emplace(id, std::move(task));
			return id;
		}

		[[nodiscard]]
		static detail::TaskID RegisterDrawTask(std::unique_ptr<detail::ICoTask>&& task)
		{
			if (!task)
			{
				throw Error{ U"CoTask is nullptr" };
			}

			auto* pInstance = Instance();
			if (!pInstance)
			{
				throw Error{ U"CoTaskBackend is not initialized" };
			}
			const detail::TaskID id = pInstance->m_nextExecID++;
			pInstance->m_drawTasks.emplace(id, std::move(task));
			return id;
		}

		static void UnregisterTask(detail::TaskID id)
		{
			auto* pInstance = Instance();
			if (!pInstance)
			{
				// Note: ユーザーがScopedExec系のクラスをstaticで持ってしまった場合にAddon解放後に呼ばれるケースが起こりうるので、ここでは例外を出さない
				return;
			}
			if (id == pInstance->m_currentRunningTaskID)
			{
				throw Error{ U"CoTaskBackend::UnregisterTask: Cannot unregister the currently running task" };
			}
			pInstance->m_updateTasks.erase(id);
			pInstance->m_drawTasks.erase(id);
		}

		static bool IsTaskRunning(detail::TaskID id)
		{
			auto* pInstance = Instance();
			if (!pInstance)
			{
				throw Error{ U"CoTaskBackend is not initialized" };
			}
			return pInstance->m_updateTasks.contains(id) || pInstance->m_drawTasks.contains(id);
		}
	};

	class ScopedCoTaskRunLifetime : Uncopyable
	{
	private:
		Optional<detail::TaskID> m_id;

	public:
		explicit ScopedCoTaskRunLifetime(const Optional<detail::TaskID>& id)
			: m_id(id)
		{
		}

		ScopedCoTaskRunLifetime(ScopedCoTaskRunLifetime&& rhs) noexcept
			: m_id(rhs.m_id)
		{
			rhs.m_id = none;
		}

		ScopedCoTaskRunLifetime& operator=(ScopedCoTaskRunLifetime&& rhs) noexcept
		{
			if (this != &rhs)
			{
				if (m_id)
				{
					CoTaskBackend::UnregisterTask(m_id.value());
				}
				m_id = rhs.m_id;
				rhs.m_id = none;
			}
			return *this;
		}

		~ScopedCoTaskRunLifetime();

		[[nodiscard]]
		bool isRunning() const
		{
			return m_id && CoTaskBackend::IsTaskRunning(m_id.value());
		}
	};

	class ScopedCoTaskRun
	{
	private:
		ScopedCoTaskRunLifetime m_lifetime;

	public:
		template <typename T>
		ScopedCoTaskRun(CoTask<T>&& task);

		ScopedCoTaskRun(ScopedCoTaskRun&&) = default;

		ScopedCoTaskRun& operator=(ScopedCoTaskRun&&) = default;

		~ScopedCoTaskRun() = default;

		[[nodiscard]]
		bool isRunning() const
		{
			return m_lifetime.isRunning();
		}
	};

	class ScopedCoTaskRunDraw
	{
	private:
		ScopedCoTaskRunLifetime m_lifetime;

	public:
		template <typename T>
		ScopedCoTaskRunDraw(CoTask<T>&& task);

		ScopedCoTaskRunDraw(ScopedCoTaskRunDraw&&) = default;

		ScopedCoTaskRunDraw& operator=(ScopedCoTaskRunDraw&&) = default;

		~ScopedCoTaskRunDraw() = default;

		[[nodiscard]]
		bool isRunning() const
		{
			return m_lifetime.isRunning();
		}
	};

	inline ScopedCoTaskRunLifetime::~ScopedCoTaskRunLifetime()
	{
		if (m_id)
		{
			CoTaskBackend::UnregisterTask(m_id.value());
		}
	}

	namespace detail
	{
		template <typename T>
		Optional<TaskID> UpdateTaskOnceAndRegisterIfNotDone(CoTask<T>&& task, std::function<TaskID(std::unique_ptr<ICoTask>&&)> registerFunc)
		{
			// Taskを初回updateして、もし即時終了しなければ登録する
			task.update();
			if (!task.done())
			{
				return registerFunc(std::make_unique<CoTask<T>>(std::move(task)));
			}
			return none;
		}
	}

	template <typename T>
	ScopedCoTaskRun::ScopedCoTaskRun(CoTask<T>&& task)
		: m_lifetime(detail::UpdateTaskOnceAndRegisterIfNotDone(std::move(task), CoTaskBackend::RegisterTask))
	{
	}

	template <typename T>
	ScopedCoTaskRunDraw::ScopedCoTaskRunDraw(CoTask<T>&& task)
		: m_lifetime(detail::UpdateTaskOnceAndRegisterIfNotDone(std::move(task), CoTaskBackend::RegisterDrawTask))
	{
	}

	namespace detail
	{
		template <typename T>
		class Promise;

		template <typename T>
		class CoroutineHandleWrapper
		{
		private:
			using HandleType = std::coroutine_handle<Promise<T>>;

			HandleType m_handle;

		public:
			explicit CoroutineHandleWrapper(HandleType handle)
				: m_handle(handle)
			{
			}

			~CoroutineHandleWrapper()
			{
				if (m_handle)
				{
					m_handle.destroy();
				}
			}

			CoroutineHandleWrapper(const CoroutineHandleWrapper<T>&) = delete;

			CoroutineHandleWrapper& operator=(const CoroutineHandleWrapper<T>&) = delete;

			CoroutineHandleWrapper(CoroutineHandleWrapper<T>&& rhs) noexcept
				: m_handle(rhs.m_handle)
			{
				rhs.m_handle = nullptr;
			}

			CoroutineHandleWrapper& operator=(CoroutineHandleWrapper<T>&& rhs) noexcept
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
			T value() const
			{
				return m_handle.promise().value();
			}

			[[nodiscard]]
			bool done() const
			{
				return !m_handle || m_handle.done();
			}

			void update() const
			{
				if (done())
				{
					return;
				}

				if (m_handle.promise().updateSubTask())
				{
					return;
				}
				m_handle.resume();

				m_handle.promise().rethrowIfException();
			}

			void setNestLevel(uint64 level)
			{
				m_handle.promise().setNestLevel(level);
			}

			[[nodiscard]]
			uint64 nestLevel() const
			{
				return m_handle.promise().nestLevel();
			}
		};
	}

	template <typename T>
	class [[nodiscard]] CoTask : public detail::ICoTask
	{
	private:
		detail::CoroutineHandleWrapper<T> m_handle;

	public:
		using promise_type = detail::Promise<T>;
		using HandleType = std::coroutine_handle<promise_type>;

		explicit CoTask(HandleType h)
			: m_handle(h)
		{
		}

		CoTask(const CoTask<T>&) = delete;

		CoTask<T>& operator=(const CoTask<T>&) = delete;

		CoTask(CoTask<T>&& rhs) = default;

		CoTask<T>& operator=(CoTask<T>&& rhs) = default;

		virtual void update() override
		{
			if (m_handle.done())
			{
				return;
			}
			m_handle.update();
		}

		[[nodiscard]]
		virtual bool done() const override
		{
			return m_handle.done();
		}

		[[nodiscard]]
		bool await_ready() const
		{
			return m_handle.done();
		}

		template <typename U>
		void await_suspend(std::coroutine_handle<detail::Promise<U>> handle)
		{
			m_handle.setNestLevel(handle.promise().nestLevel() + 1);

			update();
			if (m_handle.done())
			{
				// フレーム待ちなしで終了した場合は登録不要
				return;
			}
			handle.promise().setSubTask(this);
		}

		T await_resume() const
		{
			return m_handle.value();
		}

		[[nodiscard]]
		T value() const
		{
			return m_handle.value();
		}

		[[nodiscard]]
		ScopedCoTaskRun runScoped()&&
		{
			return ScopedCoTaskRun{ std::move(*this) };
		}

		[[nodiscard]]
		ScopedCoTaskRunDraw runDrawScoped()&&
		{
			return ScopedCoTaskRunDraw{ std::move(*this) };
		}

		void runForget()&&
		{
			update();
			if (m_handle.done())
			{
				return;
			}
			(void)CoTaskBackend::RegisterTask(std::make_unique<CoTask<T>>(std::move(*this)));
		}

		void runDrawForget()&&
		{
			update();
			if (m_handle.done())
			{
				return;
			}
			(void)CoTaskBackend::RegisterDrawTask(std::make_unique<CoTask<T>>(std::move(*this)));
		}
	};

	template <typename T>
	auto operator co_await(CoTask<T>&& rhs)
	{
		return rhs;
	}

	namespace detail
	{
		class PromiseBase : Uncopyable
		{
		protected:
			detail::ICoTask* m_pSubTask = nullptr;

			std::exception_ptr m_exception;
			uint64 m_nestLevel = 0;

		public:
			static constexpr uint64 MaxNestLevel = 250; // Note: MSVCのDebugビルドだと300ネストあたりでアクセス違反が起きてクラッシュするので、一旦はそれより前に例外を吐くようにしている

			virtual ~PromiseBase() = 0;

			PromiseBase() = default;

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
			bool updateSubTask()
			{
				if (!m_pSubTask)
				{
					return false;
				}

				m_pSubTask->update();

				const bool isRunning = !m_pSubTask->done();
				if (!isRunning)
				{
					m_pSubTask = nullptr;
				}
				return isRunning;
			}

			void setSubTask(detail::ICoTask* pSubTask)
			{
				m_pSubTask = pSubTask;
			}

			void setNestLevel(uint64 level)
			{
				if (level > MaxNestLevel)
				{
					throw std::runtime_error("CoTask is too deeply nested");
				}
				m_nestLevel = level;
			}

			[[nodiscard]]
			uint64 nestLevel() const
			{
				return m_nestLevel;
			}
		};

		inline PromiseBase::~PromiseBase() = default;

		template <typename T>
		class Promise : public PromiseBase
		{
		private:
			Optional<T> m_value;

		public:
			Promise() = default;

			Promise(Promise<T>&&) = default;

			Promise& operator=(Promise<T>&&) = default;

			void return_value(const T& v)
			{
				m_value = v;
			}

			void return_value(T&& v)
			{
				m_value = std::move(v);
			}

			[[nodiscard]]
			T value() const
			{
				rethrowIfException();
				if (!m_value)
				{
					throw std::runtime_error("CoTask is not completed");
				}
				return m_value.value();
			}

			[[nodiscard]]
			CoTask<T> get_return_object()
			{
				return CoTask<T>{ CoTask<T>::HandleType::from_promise(*this) };
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

			CoTask<void> get_return_object()
			{
				return CoTask<void>{ CoTask<void>::HandleType::from_promise(*this) };
			}
		};

		struct Yield
		{
			bool await_ready() const
			{
				return false;
			}

			void await_suspend(std::coroutine_handle<>) const
			{
			}

			void await_resume()
			{
			}
		};
	}

	inline CoTask<void> DelayFrame()
	{
		co_await detail::Yield{};
	}

	inline CoTask<void> DelayFrame(int32 frames)
	{
		for (int32 i = 0; i < frames; ++i)
		{
			co_await detail::Yield{};
		}
	}

	inline CoTask<void> Delay(const Duration& duration)
	{
		const Timer timer{ duration, StartImmediately::Yes };
		while (!timer.reachedZero())
		{
			co_await detail::Yield{};
		}
	}

	inline CoTask<void> WaitUntil(std::function<bool()> predicate)
	{
		while (!predicate())
		{
			co_await detail::Yield{};
		}
	}

	inline CoTask<void> WaitWhile(std::function<bool()> predicate)
	{
		while (predicate())
		{
			co_await detail::Yield{};
		}
	}

	inline CoTask<void> EveryFrame(std::function<void()> func)
	{
		while (true)
		{
			func();
			co_await detail::Yield{};
		}
	}

	template <class... Args>
	CoTask<void> WhenAll(CoTask<Args>... args)
	{
		while ((args.done() && ...))
		{
			(args.update(), ...);
			co_await detail::Yield{};
		}
	}

	template <class... Args>
	CoTask<void> WhenAny(CoTask<Args>... args)
	{
		while ((args.done() || ...))
		{
			(args.update(), ...);
			co_await detail::Yield{};
		}
	}

	inline CoTask<void> CoLinear(const Duration& duration, std::function<void(double)> func)
	{
		const Timer timer{ duration, StartImmediately::Yes };
		while (!timer.reachedZero())
		{
			func(timer.progress0_1());
			co_await detail::Yield{};
		}
		func(1.0);
	}

	inline CoTask<void> CoEase(const Duration& duration, std::function<double(double)> easingFunc, std::function<void(double)> func)
	{
		const Timer timer{ duration, StartImmediately::Yes };
		while (!timer.reachedZero())
		{
			func(easingFunc(timer.progress0_1()));
			co_await detail::Yield{};
		}
		func(easingFunc(1.0));
	}

	template <typename T>
	class CoSceneBase
	{
	public:
		using RetType = T;

		virtual ~CoSceneBase() = 0;

		virtual CoTask<T> start()
		{
			co_return T{};
		}

		virtual void draw() const
		{
		}
	};

	template <typename T>
	CoSceneBase<T>::~CoSceneBase() = default;

	template <typename Scene, typename... Args>
	CoTask<typename Scene::RetType> CoSceneToTask(Args&&... args)
		requires std::derived_from<Scene, CoSceneBase<typename Scene::RetType>>
	{
		Scene scene{ std::forward<Args>(args)... };
		const auto scopedRunDraw = EveryFrame([&scene]() { scene.draw(); }).runDrawScoped();
		co_return co_await scene.start();
	}

	template <typename Scene>
	CoTask<typename Scene::RetType> CoSceneToTask(Scene&& scene)
		requires std::derived_from<Scene, CoSceneBase<typename Scene::RetType>>
	{
		const auto scopedRunDraw = EveryFrame([&scene]() { scene.draw(); }).runDrawScoped();
		co_return co_await scene.start();
	}
}

#ifndef NO_COTASK_USING
using namespace cotask;
#endif
