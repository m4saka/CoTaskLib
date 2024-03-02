#pragma once
#include <Siv3D.hpp>
#include <coroutine>

namespace cotask
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

		class ICoTask
		{
		public:
			virtual ~ICoTask() = default;

			virtual void resume(FrameTiming) = 0;

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
				m_instance->resume(detail::FrameTiming::Update);
				return true;
			}

			virtual void draw() const override
			{
				if (!m_isFirstUpdated)
				{
					// Addonの初回drawがupdateより先に実行される挙動を回避
					return;
				}
				m_instance->resume(detail::FrameTiming::Draw);
			}

			virtual void postPresent() override
			{
				if (!m_isFirstUpdated)
				{
					// Addonの初回postPresentがupdateより先に実行される挙動を回避
					return;
				}
				m_instance->resume(detail::FrameTiming::PostPresent);
			}

			[[nodiscard]]
			CoTaskBackend* instance() const
			{
				return m_instance.get();
			}
		};

		detail::FrameTiming m_currentFrameTiming = detail::FrameTiming::Init;

		detail::TaskID m_nextTaskID = 1;

		std::optional<detail::TaskID> m_currentRunningTaskID = none;

		std::map<detail::TaskID, std::unique_ptr<detail::ICoTask>> m_tasks;

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

	public:
		void resume(detail::FrameTiming frameTiming)
		{
			m_currentFrameTiming = frameTiming;
			for (auto it = m_tasks.begin(); it != m_tasks.end();)
			{
				m_currentRunningTaskID = it->first;

				it->second->resume(frameTiming);
				if (it->second->done())
				{
					it = m_tasks.erase(it);
				}
				else
				{
					++it;
				}
			}
			m_currentRunningTaskID = none;
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
			const detail::TaskID id = pInstance->m_nextTaskID++;
			pInstance->m_tasks.emplace(id, std::move(task));
			return id;
		}

		static void UnregisterTask(detail::TaskID id)
		{
			auto* pInstance = Instance();
			if (!pInstance)
			{
				// Note: ユーザーがScopedCoTaskRunをstaticで持ってしまった場合にAddon解放後に呼ばれるケースが起こりうるので、ここでは例外を出さない
				return;
			}
			if (id == pInstance->m_currentRunningTaskID)
			{
				throw Error{ U"CoTaskBackend::UnregisterTask: Cannot unregister the currently running task" };
			}
			pInstance->m_tasks.erase(id);
		}

		static bool IsTaskRunning(detail::TaskID id)
		{
			auto* pInstance = Instance();
			if (!pInstance)
			{
				throw Error{ U"CoTaskBackend is not initialized" };
			}
			return pInstance->m_tasks.contains(id);
		}

		static detail::FrameTiming CurrentFrameTiming()
		{
			auto* pInstance = Instance();
			if (!pInstance)
			{
				throw Error{ U"CoTaskBackend is not initialized" };
			}
			return pInstance->m_currentFrameTiming;
		}
	};

	class ScopedCoTaskRunLifetime : Uncopyable
	{
	private:
		std::optional<detail::TaskID> m_id;

	public:
		explicit ScopedCoTaskRunLifetime(const std::optional<detail::TaskID>& id)
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
		explicit ScopedCoTaskRun(CoTask<T>&& task);

		ScopedCoTaskRun(ScopedCoTaskRun&&) = default;

		ScopedCoTaskRun& operator=(ScopedCoTaskRun&&) = default;

		~ScopedCoTaskRun() = default;

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
		std::optional<TaskID> UpdateTaskOnceAndRegisterIfNotDone(CoTask<T>&& task)
		{
			if (task.done())
			{
				// 既に終了済み
				return none;
			}

			task.resume(CoTaskBackend::CurrentFrameTiming());
			if (task.done())
			{
				// フレーム待ちなしで終了した場合は登録不要
				return none;
			}
			return CoTaskBackend::RegisterTask(std::make_unique<CoTask<T>>(std::move(task)));
		}
	}

	template <typename T>
	ScopedCoTaskRun::ScopedCoTaskRun(CoTask<T>&& task)
		: m_lifetime(detail::UpdateTaskOnceAndRegisterIfNotDone(std::move(task)))
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

			void resume(FrameTiming frameTiming) const
			{
				if (done())
				{
					return;
				}

				if (m_handle.promise().resumeSubTask(frameTiming))
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

			void setNestLevel(uint64 level)
			{
				m_handle.promise().setNestLevel(level);
			}

			[[nodiscard]]
			uint64 nestLevel() const
			{
				return m_handle.promise().nestLevel();
			}

			FrameTiming nextResumeTiming() const
			{
				return m_handle.promise().nextResumeTiming();
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

		virtual void resume(detail::FrameTiming frameTiming) override
		{
			if (m_handle.done())
			{
				return;
			}
			m_handle.resume(frameTiming);
		}

		[[nodiscard]]
		virtual bool done() const override
		{
			return m_handle.done();
		}

		[[nodiscard]]
		bool await_ready()
		{
			resume(CoTaskBackend::CurrentFrameTiming());
			return m_handle.done();
		}

		template <typename U>
		void await_suspend(std::coroutine_handle<detail::Promise<U>> handle)
		{
			m_handle.setNestLevel(handle.promise().nestLevel() + 1);
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

		void runForget()&&
		{
			resume(CoTaskBackend::CurrentFrameTiming());
			if (m_handle.done())
			{
				// フレーム待ちなしで終了した場合は登録不要
				return;
			}
			(void)CoTaskBackend::RegisterTask(std::make_unique<CoTask<T>>(std::move(*this)));
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
			FrameTiming m_nextResumeTiming = FrameTiming::Update;

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
			bool resumeSubTask(FrameTiming frameTiming)
			{
				if (!m_pSubTask)
				{
					return false;
				}

				m_pSubTask->resume(frameTiming);

				if (m_pSubTask->done())
				{
					m_pSubTask = nullptr;
					return false;
				}

				return true;
			}

			void setSubTask(detail::ICoTask* pSubTask)
			{
				m_pSubTask = pSubTask;
			}

			void setNestLevel(uint64 level)
			{
				if (level > MaxNestLevel)
				{
					throw Error{ U"CoTask is too deeply nested" };
				}
				m_nestLevel = level;
			}

			[[nodiscard]]
			uint64 nestLevel() const
			{
				return m_nestLevel;
			}

			[[nodiscard]]
			FrameTiming nextResumeTiming() const
			{
				return m_nextResumeTiming;
			}
		};

		inline PromiseBase::~PromiseBase() = default;

		template <typename T>
		class Promise : public PromiseBase
		{
		private:
			std::optional<T> m_value;

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
					throw Error{ U"CoTask is not completed. Make sure that all paths in the coroutine return a value." };
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

	inline CoTask<void> Delay(const Duration& duration)
	{
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
		{
			co_yield detail::FrameTiming::Update;
		}

		const Timer timer{ duration, StartImmediately::Yes };
		while (!timer.reachedZero())
		{
			co_yield detail::FrameTiming::Update;
		}
	}

	inline CoTask<void> WaitUntil(std::function<bool()> predicate)
	{
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
		{
			co_yield detail::FrameTiming::Update;
		}

		while (!predicate())
		{
			co_yield detail::FrameTiming::Update;
		}
	}

	inline CoTask<Input> WaitForKeyDown(bool clearInput = true)
	{
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
		{
			co_yield detail::FrameTiming::Update;
		}

		while (true)
		{
			const Array<Input> allInputs = Keyboard::GetAllInputs();
			for (const auto& input : allInputs)
			{
				if (input.down())
				{
					if (clearInput)
					{
						input.clearInput();
					}
					co_return input;
				}
			}
			co_yield detail::FrameTiming::Update;
		}
	}

	inline CoTask<void> WaitForMouseLDown(bool clearInput = true)
	{
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
		{
			co_yield detail::FrameTiming::Update;
		}

		while (true)
		{
			if (MouseL.down())
			{
				if (clearInput)
				{
					MouseL.clearInput();
				}
				co_return;
			}
			co_yield detail::FrameTiming::Update;
		}
	}

	inline CoTask<void> WaitWhile(std::function<bool()> predicate)
	{
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
		{
			co_yield detail::FrameTiming::Update;
		}

		while (predicate())
		{
			co_yield detail::FrameTiming::Update;
		}
	}

	inline CoTask<void> EveryFrame(std::function<void()> func)
	{
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
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
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Draw)
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
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::PostPresent)
		{
			co_yield detail::FrameTiming::PostPresent;
		}

		while (true)
		{
			func();
			co_yield detail::FrameTiming::PostPresent;
		}
	}

	template <class... Args>
	CoTask<std::tuple<Args...>> WhenAll(CoTask<Args>... args)
	{
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
		{
			co_yield detail::FrameTiming::Update;
		}

		if ((args.done() && ...))
		{
			co_return std::make_tuple(args.value()...);
		}

		while (true)
		{
			(args.resume(CoTaskBackend::CurrentFrameTiming()), ...);
			if ((args.done() && ...))
			{
				co_return std::make_tuple(args.value()...);
			}
			co_yield detail::FrameTiming::Update;
		}
	}

	template <class... Args>
	CoTask<void> WhenAny(CoTask<Args>... args)
	{
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
		{
			co_yield detail::FrameTiming::Update;
		}

		if ((args.done() || ...))
		{
			co_return;
		}

		while (true)
		{
			(args.resume(CoTaskBackend::CurrentFrameTiming()), ...);
			if ((args.done() || ...))
			{
				co_return;
			}
			co_yield detail::FrameTiming::Update;
		}
	}

	inline CoTask<void> CoLinear(const Duration& duration, std::function<void(double)> func)
	{
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
		{
			co_yield detail::FrameTiming::Update;
		}

		const Timer timer{ duration, StartImmediately::Yes };
		while (!timer.reachedZero())
		{
			func(timer.progress0_1());
			co_yield detail::FrameTiming::Update;
		}
		func(1.0);
	}

	inline CoTask<void> CoEase(const Duration& duration, std::function<double(double)> easingFunc, std::function<void(double)> func)
	{
		if (CoTaskBackend::CurrentFrameTiming() != detail::FrameTiming::Update)
		{
			co_yield detail::FrameTiming::Update;
		}

		const Timer timer{ duration, StartImmediately::Yes };
		while (!timer.reachedZero())
		{
			func(easingFunc(timer.progress0_1()));
			co_yield detail::FrameTiming::Update;
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
		const auto scopedTaskRun = EveryFrameDraw([&scene]() { scene.draw(); }).runScoped();
		co_return co_await scene.start();
	}

	template <typename Scene>
	CoTask<typename Scene::RetType> CoSceneToTask(Scene&& scene)
		requires std::derived_from<Scene, CoSceneBase<typename Scene::RetType>>
	{
		const auto scopedTaskRun = EveryFrameDraw([&scene]() { scene.draw(); }).runScoped();
		co_return co_await scene.start();
	}
}

#ifndef NO_COTASK_USING
using namespace cotask;
#endif
