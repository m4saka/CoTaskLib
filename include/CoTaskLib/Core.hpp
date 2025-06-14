﻿//----------------------------------------------------------------------------------------
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
#include <Siv3D.hpp>
#include <coroutine>
#include "License.hpp"
#include "LazyDeletionFlatMap.hpp"

namespace cotasklib::Co
{
	using AddsLicenseYN = YesNo<struct AddsLicenseYN_tag>;
	using ClearsInputYN = YesNo<struct ClearsInputYN_tag>;

	class [[nodiscard]] ITask
	{
	public:
		virtual ~ITask() = default;

		virtual void resume() = 0;

		[[nodiscard]]
		virtual bool done() const = 0;
	};

	namespace detail
	{
		constexpr static size_t InitialCapacity = 32;
		constexpr static size_t CompactionThreshold = 64;

		class IAwaiter
		{
		public:
			virtual ~IAwaiter() = default;

			virtual void resume() = 0;

			[[nodiscard]]
			virtual bool done() const = 0;
		};

		struct TaskEntry
		{
			std::unique_ptr<ITask> task;
			std::function<void(const ITask*)> finishCallback;
			std::function<void()> cancelCallback;

			void callEndCallback() const
			{
				if (task->done())
				{
					if (finishCallback)
					{
						finishCallback(task.get());
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

		using TaskID = uint64;

		using UpdaterID = uint64;

		using DrawerID = uint64;

		template <typename TResult>
		class TaskAwaiter;

		template <typename TResult>
		struct FinishCallbackTypeTrait
		{
			using type = std::function<void(TResult)>;
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

	// レイヤー
	// (将来的な拡張のために隙間を空けている)
	enum class Layer : uint8
	{
		User_PreDefault_1 = 32,
		User_PreDefault_2,
		User_PreDefault_3,
		User_PreDefault_4,
		User_PreDefault_5,
		User_PreDefault_6,
		User_PreDefault_7,
		User_PreDefault_8,
		User_PreDefault_9,
		User_PreDefault_10,

		Default = 64,

		User_PostDefault_1 = 65,
		User_PostDefault_2,
		User_PostDefault_3,
		User_PostDefault_4,
		User_PostDefault_5,
		User_PostDefault_6,
		User_PostDefault_7,
		User_PostDefault_8,
		User_PostDefault_9,
		User_PostDefault_10,

		Modal = 128,

		User_PostModal_1 = 129,
		User_PostModal_2,
		User_PostModal_3,
		User_PostModal_4,
		User_PostModal_5,
		User_PostModal_6,
		User_PostModal_7,
		User_PostModal_8,
		User_PostModal_9,
		User_PostModal_10,

		Transition_FadeIn = 192,
		Transition_General = 193,
		Transition_FadeOut = 194,

		User_PostTransition_1 = 195,
		User_PostTransition_2,
		User_PostTransition_3,
		User_PostTransition_4,
		User_PostTransition_5,
		User_PostTransition_6,
		User_PostTransition_7,
		User_PostTransition_8,
		User_PostTransition_9,
		User_PostTransition_10,

		Debug = 255,
	};

	namespace detail
	{
		struct DrawerKey
		{
			Layer layer;
			int32 drawIndex;
			DrawerID id;

			[[nodiscard]]
			auto operator<=>(const DrawerKey&) const = default;
		};

		class IDrawerInternal
		{
		public:
			virtual ~IDrawerInternal() = default;

			virtual void drawInternal() const = 0;
		};

		class DrawExecutor
		{
		private:
			DrawerID m_nextID = 1;
			LazyDeletionFlatMap<DrawerKey, IDrawerInternal*, false> m_drawers;
			std::unordered_map<DrawerID, DrawerKey> m_drawerKeyByID;
			std::unordered_map<Layer, uint64> m_layerDrawerCount;

			[[nodiscard]]
			auto findByID(DrawerID id)
			{
				const auto it = m_drawerKeyByID.find(id);
				if (it == m_drawerKeyByID.end())
				{
					return m_drawers.end();
				}
				return m_drawers.find(it->second);
			}

			void incrementLayerDrawerCount(Layer layer)
			{
				const auto it = m_layerDrawerCount.find(layer);
				if (it == m_layerDrawerCount.end())
				{
					m_layerDrawerCount.emplace(layer, 1);
				}
				else
				{
					++it->second;
				}
			}

			void decrementLayerDrawerCount(Layer layer)
			{
				const auto it = m_layerDrawerCount.find(layer);
				if (it == m_layerDrawerCount.end())
				{
					throw Error{ U"DrawExecutor::decrementLayerDrawerCount: Layer drawer count underflow (layer={})"_fmt(static_cast<uint8>(layer)) };
				}
				if (it->second == 0)
				{
					throw Error{ U"DrawExecutor::decrementLayerDrawerCount: Layer drawer count underflow (layer={})"_fmt(static_cast<uint8>(layer)) };
				}
				--it->second;
			}

		public:
			DrawExecutor()
			{
				m_drawers.reserve(InitialCapacity);
				m_drawerKeyByID.reserve(InitialCapacity);
				m_layerDrawerCount.reserve(InitialCapacity);
			}

			DrawerID add(Layer layer, int32 drawIndex, IDrawerInternal* pDrawable)
			{
				const DrawerID id = m_nextID++;
				DrawerKey key{ layer, drawIndex, id };
				const bool alreadyExists = m_drawers.find(key) != m_drawers.end();
				if (alreadyExists)
				{
					throw Error{ U"DrawExecutor::add: ID={} already exists"_fmt(id) };
				}
				m_drawers.emplace(key, pDrawable);
				m_drawerKeyByID.emplace(id, std::move(key));
				incrementLayerDrawerCount(layer);
				return id;
			}

			void setDrawerLayer(DrawerID id, Layer layer)
			{
				const auto it = findByID(id);
				if (it == m_drawers.end())
				{
					throw Error{ U"DrawExecutor::setDrawerLayer: ID={} not found"_fmt(id) };
				}
				const auto& key = it.key();
				if (key.layer == layer)
				{
					return;
				}
				const auto prevLayer = key.layer;

				// 一度削除して再挿入
				DrawerKey newKey = key;
				newKey.layer = layer;
				const auto pDrawable = *it;
				m_drawers.erase(it);
				m_drawers.emplace(std::move(newKey), pDrawable);
				m_drawerKeyByID[id].layer = layer;

				decrementLayerDrawerCount(prevLayer);
				incrementLayerDrawerCount(layer);
			}

			void setDrawerDrawIndex(DrawerID id, int32 drawIndex)
			{
				const auto it = findByID(id);
				if (it == m_drawers.end())
				{
					throw Error{ U"DrawExecutor::setDrawerDrawIndex: ID={} not found"_fmt(id) };
				}
				const auto& key = it.key();
				if (key.drawIndex == drawIndex)
				{
					return;
				}

				// 一度削除して再挿入
				DrawerKey newKey = key;
				newKey.drawIndex = drawIndex;
				const auto pDrawable = *it;
				m_drawers.erase(it);
				m_drawers.emplace(std::move(newKey), pDrawable);
				m_drawerKeyByID[id].drawIndex = drawIndex;
			}

			void remove(DrawerID id)
			{
				const auto it = findByID(id);
				if (it == m_drawers.end())
				{
					throw Error{ U"DrawExecutor::remove: ID={} not found"_fmt(id) };
				}
				const auto& key = it.key();
				decrementLayerDrawerCount(key.layer);
				m_drawers.erase(it);
				m_drawerKeyByID.erase(id);
			}

			void execute() const
			{
				for (const auto& pDrawer : m_drawers)
				{
					pDrawer->drawInternal();
				}
			}

			[[nodiscard]]
			bool drawerExistsInLayer(Layer layer) const
			{
				const auto it = m_layerDrawerCount.find(layer);
				if (it == m_layerDrawerCount.end())
				{
					return false;
				}
				return it->second > 0;
			}

			void compactIfNeeded()
			{
				if (m_drawers.nulloptCount() > CompactionThreshold)
				{
					m_drawers.compact();
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

			TaskID m_nextTaskID = 1;

			Optional<TaskID> m_currentTaskID = none;

			bool m_currentTaskRemovalNeeded = false;

			LazyDeletionFlatMap<TaskID, TaskEntry, true> m_taskEntries;

			struct PendingTaskEntry
			{
				TaskID id;
				TaskEntry entry;
			};
			Array<PendingTaskEntry> m_pendingTaskEntries;

			DrawExecutor m_drawExecutor;

		public:
			Backend()
			{
				m_taskEntries.reserve(InitialCapacity);
				m_pendingTaskEntries.reserve(InitialCapacity);
			}

			void update()
			{
				if (!m_pendingTaskEntries.empty())
				{
					for (auto& pendingTaskEntry : m_pendingTaskEntries)
					{
						m_taskEntries.emplace(pendingTaskEntry.id, std::move(pendingTaskEntry.entry));
					}
					m_pendingTaskEntries.clear();
				}

				std::exception_ptr exceptionPtr;
				for (auto it = m_taskEntries.begin(); it != m_taskEntries.end();)
				{
					m_currentTaskID = it.key();

					const auto& entry = *it;
					entry.task->resume();
					if (m_currentTaskRemovalNeeded || entry.task->done())
					{
						try
						{
							entry.callEndCallback();
						}
						catch (...)
						{
							if (!exceptionPtr)
							{
								exceptionPtr = std::current_exception();
							}
						}
						it = m_taskEntries.erase(it);
						m_currentTaskRemovalNeeded = false;
					}
					else
					{
						++it;
					}
				}
				m_currentTaskID.reset();

				if (!m_pendingTaskEntries.empty())
				{
					for (auto& pendingTaskEntry : m_pendingTaskEntries)
					{
						m_taskEntries.emplace(pendingTaskEntry.id, std::move(pendingTaskEntry.entry));
					}
					m_pendingTaskEntries.clear();
				}

				if (exceptionPtr)
				{
					std::rethrow_exception(exceptionPtr);
				}

				if (m_taskEntries.nulloptCount() > CompactionThreshold)
				{
					m_taskEntries.compact();
				}

				m_drawExecutor.compactIfNeeded();
			}

			void draw()
			{
				m_drawExecutor.execute();
			}

			static void Init()
			{
				Addon::Register(AddonName, std::make_unique<BackendAddon>());
			}

			template <typename TResult>
			[[nodiscard]]
			static TaskID Add(std::unique_ptr<Task<TResult>>&& task, FinishCallbackType<TResult> finishCallback, std::function<void()> cancelCallback)
			{
				if (!task)
				{
					throw Error{ U"task must not be nullptr" };
				}

				if (!s_pInstance)
				{
					throw Error{ U"Backend is not initialized" };
				}
				const TaskID id = s_pInstance->m_nextTaskID++;
				std::function<void(const ITask*)> finishCallbackTypeErased =
					[finishCallback = std::move(finishCallback), cancelCallback/*コピーキャプチャ*/](const ITask* task)
					{
						auto fnGetResult = [task, cancelCallback]() -> TResult
							{
								try
								{
									// taskの型がTask<TResult>であることは保証されるため、static_castでキャストして問題ない
									return static_cast<const Task<TResult>*>(task)->value();
								}
								catch (...)
								{
									// 例外を捕捉した場合はキャンセル扱いにした上で例外を投げ直す
									if (cancelCallback)
									{
										cancelCallback();
									}
									throw;
								}
							};
						if constexpr (std::is_void_v<TResult>)
						{
							fnGetResult(); // 例外伝搬のためにvoidでも呼び出す
							if (finishCallback)
							{
								finishCallback();
							}
						}
						else
						{
							auto result = fnGetResult();
							if (finishCallback)
							{
								finishCallback(std::move(result));
							}
						}
					};
				s_pInstance->m_pendingTaskEntries.push_back(
					PendingTaskEntry
					{
						.id = id,
						.entry = TaskEntry
						{
							.task = std::move(task),
							.finishCallback = std::move(finishCallbackTypeErased),
							.cancelCallback = std::move(cancelCallback),
						}
					});
				return id;
			}

			static bool Remove(TaskID id)
			{
				if (!s_pInstance)
				{
					// Note: ユーザーがインスタンスをstaticで持ってしまった場合にAddon解放後に呼ばれるケースが起こりうるので、ここでは例外を出さない
					return false;
				}
				if (!s_pInstance->m_pendingTaskEntries.empty())
				{
					// まだ登録されていないタスクの削除は、PendingTaskEntryから削除する
					const auto it = std::find_if(s_pInstance->m_pendingTaskEntries.begin(), s_pInstance->m_pendingTaskEntries.end(),
						[id](const PendingTaskEntry& pending) { return pending.id == id; });
					if (it != s_pInstance->m_pendingTaskEntries.end())
					{
						it->entry.callEndCallback();
						s_pInstance->m_pendingTaskEntries.erase(it);
						return true;
					}
				}
				if (id == s_pInstance->m_currentTaskID)
				{
					// 実行中タスクのエントリをここで削除するとアクセス違反やイテレータ破壊が起きるため、代わりに削除フラグを立てて実行完了時に削除
					// (例えば、タスク実行のライフタイムをOptional<ScopedTaskRunner>型のメンバ変数として持ち、タスク実行中にそこへnoneを代入して実行を止める場合が該当)
					if (!s_pInstance->m_currentTaskRemovalNeeded)
					{
						s_pInstance->m_currentTaskRemovalNeeded = true;
						return true;
					}
					return false;
				}
				const auto it = s_pInstance->m_taskEntries.find(id);
				if (it != s_pInstance->m_taskEntries.end())
				{
					it->callEndCallback();
					s_pInstance->m_taskEntries.erase(it);
					return true;
				}
				return false;
			}

			[[nodiscard]]
			static bool IsDone(TaskID id)
			{
				if (!s_pInstance)
				{
					throw Error{ U"Backend is not initialized" };
				}
				if (!s_pInstance->m_pendingTaskEntries.empty())
				{
					// まだ登録されていないタスクの完了状態は、PendingTaskEntryから取得する
					const auto it = std::find_if(s_pInstance->m_pendingTaskEntries.begin(), s_pInstance->m_pendingTaskEntries.end(),
						[id](const PendingTaskEntry& pending) { return pending.id == id; });
					if (it != s_pInstance->m_pendingTaskEntries.end())
					{
						return it->entry.task->done();
					}
				}
				const auto it = s_pInstance->m_taskEntries.find(id);
				if (it != s_pInstance->m_taskEntries.end())
				{
					return it->task->done();
				}
				return id < s_pInstance->m_nextTaskID;
			}

			static void ManualUpdate()
			{
				if (!s_pInstance)
				{
					throw Error{ U"Backend is not initialized" };
				}
				s_pInstance->update();
			}

			[[nodiscard]]
			static DrawerID AddDrawer(IDrawerInternal* pDrawer, Layer layer, int32 drawIndex)
			{
				if (!s_pInstance)
				{
					throw Error{ U"Backend is not initialized" };
				}
				return s_pInstance->m_drawExecutor.add(layer, drawIndex, pDrawer);
			}

			static void SetDrawerLayer(DrawerID id, Layer layer)
			{
				if (!s_pInstance)
				{
					throw Error{ U"Backend is not initialized" };
				}
				s_pInstance->m_drawExecutor.setDrawerLayer(id, layer);
			}

			static void SetDrawerDrawIndex(DrawerID id, int32 drawIndex)
			{
				if (!s_pInstance)
				{
					throw Error{ U"Backend is not initialized" };
				}
				s_pInstance->m_drawExecutor.setDrawerDrawIndex(id, drawIndex);
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
			static bool HasActiveDrawerInLayer(Layer layer)
			{
				if (!s_pInstance)
				{
					throw Error{ U"Backend is not initialized" };
				}
				return s_pInstance->m_drawExecutor.drawerExistsInLayer(layer);
			}
		};

		template <typename TResult>
		[[nodiscard]]
		Optional<TaskID> ResumeTaskOnceAndRegisterIfNotDone(Task<TResult>&& task, FinishCallbackType<TResult> finishCallback, std::function<void()> cancelCallback)
		{
			const auto fnCallFinishCallback = [&]
				{
					if constexpr (std::is_void_v<TResult>)
					{
						try
						{
							task.value(); // 例外伝搬のためにvoidでも呼び出す
						}
						catch (...)
						{
							if (cancelCallback)
							{
								cancelCallback();
							}
							throw;
						}

						if (finishCallback)
						{
							finishCallback();
						}
					}
					else
					{
						auto result = [&]() -> TResult
							{
								try
								{
									return task.value();
								}
								catch (...)
								{
									if (cancelCallback)
									{
										cancelCallback();
									}
									throw;
								}
							}();
						if (finishCallback)
						{
							finishCallback(std::move(result));
						}
					}
				};

			// フレーム待ちなしで終了した場合は登録不要
			// (ここで一度resumeするのは、runScoped実行まで開始を遅延させるためにinitial_suspendをsuspend_alwaysにしているため)
			if (task.done())
			{
				fnCallFinishCallback();
				return none;
			}
			task.resume();
			if (task.done())
			{
				fnCallFinishCallback();
				return none;
			}

			return Backend::Add(std::make_unique<Task<TResult>>(std::move(task)), std::move(finishCallback), std::move(cancelCallback));
		}

		template <typename TResult>
		Optional<TaskID> ResumeTaskOnceAndRegisterIfNotDone(const Task<TResult>& task) = delete;
	}

	[[nodiscard]]
	inline auto NextFrame() noexcept
	{
		return std::suspend_always{};
	}

	class MultiRunner;

	class ScopedTaskRunner
	{
	private:
		Optional<detail::TaskID> m_id;

	public:
		template <typename TResult>
		explicit ScopedTaskRunner(Task<TResult>&& task, FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr)
			: m_id(detail::ResumeTaskOnceAndRegisterIfNotDone(std::move(task), std::move(finishCallback), std::move(cancelCallback)))
		{
		}

		ScopedTaskRunner(const ScopedTaskRunner&) = delete;

		ScopedTaskRunner& operator=(const ScopedTaskRunner&) = delete;

		ScopedTaskRunner(ScopedTaskRunner&& rhs) noexcept
			: m_id(rhs.m_id)
		{
			rhs.m_id.reset();
		}

		ScopedTaskRunner& operator=(ScopedTaskRunner&& rhs)
		{
			if (m_id.has_value())
			{
				detail::Backend::Remove(*m_id);
			}
			m_id = rhs.m_id;
			rhs.m_id.reset();
			return *this;
		}

		~ScopedTaskRunner()
		{
			if (m_id.has_value())
			{
				detail::Backend::Remove(*m_id);
			}
		}

		[[nodiscard]]
		bool done() const
		{
			return !m_id.has_value() || detail::Backend::IsDone(*m_id);
		}

		void forget()
		{
			m_id.reset();
		}

		bool requestCancel()
		{
			if (m_id.has_value())
			{
				const bool removed = detail::Backend::Remove(*m_id);
				m_id.reset();
				return removed;
			}
			return false;
		}

		void addTo(MultiRunner& mr)&&;

		[[nodiscard]]
		Task<void> waitUntilDone() const&;

		[[nodiscard]]
		Task<void> waitUntilDone() const&& = delete;
	};

	class MultiRunner
	{
	private:
		Array<ScopedTaskRunner> m_runners;

	public:
		MultiRunner() = default;

		MultiRunner(const MultiRunner&) = delete;

		MultiRunner& operator=(const MultiRunner&) = delete;

		MultiRunner(MultiRunner&&) = default;

		MultiRunner& operator=(MultiRunner&&) = default;

		~MultiRunner() = default;

		void add(ScopedTaskRunner&& runner)
		{
			m_runners.push_back(std::move(runner));
		}

		void reserve(size_t size)
		{
			m_runners.reserve(size);
		}

		void clear()
		{
			m_runners.clear();
		}

		[[nodiscard]]
		size_t size() const noexcept
		{
			return m_runners.size();
		}

		[[nodiscard]]
		bool empty() const noexcept
		{
			return m_runners.empty();
		}

		void shrinkToFit()
		{
			m_runners.shrink_to_fit();
		}

		[[nodiscard]]
		auto begin() noexcept
		{
			return m_runners.begin();
		}

		[[nodiscard]]
		auto begin() const noexcept
		{
			return m_runners.begin();
		}

		[[nodiscard]]
		auto end() noexcept
		{
			return m_runners.end();
		}

		[[nodiscard]]
		auto end() const noexcept
		{
			return m_runners.end();
		}

		[[nodiscard]]
		auto cbegin() const noexcept
		{
			return m_runners.cbegin();
		}

		[[nodiscard]]
		auto cend() const noexcept
		{
			return m_runners.cend();
		}

		[[nodiscard]]
		auto rbegin() noexcept
		{
			return m_runners.rbegin();
		}

		[[nodiscard]]
		auto rbegin() const noexcept
		{
			return m_runners.rbegin();
		}

		[[nodiscard]]
		auto rend() noexcept
		{
			return m_runners.rend();
		}

		[[nodiscard]]
		auto rend() const noexcept
		{
			return m_runners.rend();
		}

		[[nodiscard]]
		auto crbegin() const noexcept
		{
			return m_runners.crbegin();
		}

		[[nodiscard]]
		auto crend() const noexcept
		{
			return m_runners.crend();
		}

		[[nodiscard]]
		ScopedTaskRunner& operator[](size_t index)
		{
			return m_runners[index];
		}

		[[nodiscard]]
		const ScopedTaskRunner& operator[](size_t index) const
		{
			return m_runners[index];
		}

		[[nodiscard]]
		ScopedTaskRunner& at(size_t index)
		{
			return m_runners.at(index);
		}

		[[nodiscard]]
		const ScopedTaskRunner& at(size_t index) const
		{
			return m_runners.at(index);
		}

		void removeDone()
		{
			m_runners.remove_if([](const ScopedTaskRunner& runner) { return runner.done(); });
		}

		bool requestCancelAll()
		{
			bool anyCanceled = false;
			for (ScopedTaskRunner& runner : m_runners)
			{
				if (runner.requestCancel())
				{
					anyCanceled = true;
				}
			}
			return anyCanceled;
		}

		[[nodiscard]]
		bool allDone() const
		{
			return std::all_of(m_runners.begin(), m_runners.end(), [](const ScopedTaskRunner& runner) { return runner.done(); });
		}

		[[nodiscard]]
		bool anyDone() const
		{
			return std::any_of(m_runners.begin(), m_runners.end(), [](const ScopedTaskRunner& runner) { return runner.done(); });
		}

		[[nodiscard]]
		Task<void> waitUntilAllDone() const&;

		[[nodiscard]]
		Task<void> waitUntilAllDone() const&& = delete;

		[[nodiscard]]
		Task<void> waitUntilAnyDone() const&;

		[[nodiscard]]
		Task<void> waitUntilAnyDone() const&& = delete;
	};

	inline void ScopedTaskRunner::addTo(MultiRunner& mr)&&
	{
		mr.add(std::move(*this));
	}

	namespace DrawIndex
	{
		constexpr int32 Back = -1;
		constexpr int32 Default = 0;
		constexpr int32 Front = 1;
	}

	class ScopedDrawer
	{
	private:
		class Drawer : public detail::IDrawerInternal
		{
		private:
			std::function<void()> m_func;

			void drawInternal() const override
			{
				m_func();
			}

		public:
			explicit Drawer(std::function<void()> func)
				: m_func(std::move(func))
			{
			}

			Drawer(const Drawer&) = delete;
			Drawer& operator=(const Drawer&) = delete;
			Drawer(Drawer&&) = default;
			Drawer& operator=(Drawer&&) = delete;
		};

		Drawer m_drawer;
		Optional<detail::DrawerID> m_drawerID;

	public:
		explicit ScopedDrawer(std::function<void()> func, Layer layer = Layer::Default, int32 drawIndex = DrawIndex::Default)
			: m_drawer(std::move(func))
			, m_drawerID(detail::Backend::AddDrawer(&m_drawer, layer, drawIndex))
		{
		}

		ScopedDrawer(const ScopedDrawer&) = delete;

		ScopedDrawer& operator=(const ScopedDrawer&) = delete;

		ScopedDrawer(ScopedDrawer&& rhs) noexcept = delete;

		ScopedDrawer& operator=(ScopedDrawer&& rhs) = delete;

		~ScopedDrawer()
		{
			if (m_drawerID.has_value())
			{
				detail::Backend::RemoveDrawer(*m_drawerID);
			}
		}

		void setLayer(Layer layer)
		{
			if (m_drawerID.has_value())
			{
				detail::Backend::SetDrawerLayer(*m_drawerID, layer);
			}
		}

		void setDrawIndex(int32 drawIndex)
		{
			if (m_drawerID.has_value())
			{
				detail::Backend::SetDrawerDrawIndex(*m_drawerID, drawIndex);
			}
		}
	};

	namespace detail
	{
		class ScopedDrawerInternal
		{
		private:
			DrawerID m_drawerID;
			ScopedDrawerInternal** m_pThis;

		public:
			ScopedDrawerInternal(IDrawerInternal* pDrawer, Layer layer, int32 drawIndex, ScopedDrawerInternal** pThis)
				: m_drawerID(Backend::AddDrawer(pDrawer, layer, drawIndex))
				, m_pThis(pThis)
			{
				*m_pThis = this;
			}

			ScopedDrawerInternal(const ScopedDrawerInternal&) = delete;

			ScopedDrawerInternal& operator=(const ScopedDrawerInternal&) = delete;

			ScopedDrawerInternal(ScopedDrawerInternal&& rhs) = delete;

			ScopedDrawerInternal& operator=(ScopedDrawerInternal&& rhs) = delete;

			~ScopedDrawerInternal()
			{
				Backend::RemoveDrawer(m_drawerID);
				*m_pThis = nullptr;
			}

			void setLayer(Layer layer)
			{
				Backend::SetDrawerLayer(m_drawerID, layer);
			}

			void setDrawIndex(int32 drawIndex)
			{
				Backend::SetDrawerDrawIndex(m_drawerID, drawIndex);
			}
		};

		template <typename TResult>
		class Promise;
	}

	enum class WithTiming : uint8
	{
		Before,
		After,
	};

	template <typename TResult = void>
	class [[nodiscard]] Task : public ITask
	{
		static_assert(!std::is_reference_v<TResult>, "TResult must not be a reference type");
		static_assert(std::is_move_constructible_v<TResult> || std::is_void_v<TResult>, "TResult must be move constructible");
		static_assert(!std::is_const_v<TResult>, "TResult must not have 'const' qualifier");

	public:
		using promise_type = detail::Promise<TResult>;
		using handle_type = std::coroutine_handle<promise_type>;
		using result_type = TResult;
		using finish_callback_type = FinishCallbackType<TResult>;

	private:
		handle_type m_handle;
		Array<std::unique_ptr<ITask>> m_concurrentTasksBefore;
		Array<std::unique_ptr<ITask>> m_concurrentTasksAfter;

	public:
		explicit Task(handle_type h)
			: m_handle(std::move(h))
		{
		}

		Task(const Task<TResult>&) = delete;

		Task<TResult>& operator=(const Task<TResult>&) = delete;

		Task(Task<TResult>&& rhs) noexcept
			: m_handle(rhs.m_handle)
			, m_concurrentTasksBefore(std::move(rhs.m_concurrentTasksBefore))
			, m_concurrentTasksAfter(std::move(rhs.m_concurrentTasksAfter))
		{
			rhs.m_handle = nullptr;
		}

		Task<TResult>& operator=(Task<TResult>&& rhs) = delete;

		~Task()
		{
			if (m_handle)
			{
				m_handle.destroy();
			}
		}

		virtual void resume() override
		{
			if (!m_handle || m_handle.done())
			{
				return;
			}

			for (auto& task : m_concurrentTasksBefore)
			{
				task->resume();
			}

			if (!m_handle.promise().resumeSubAwaiter())
			{
				m_handle.resume();
			}

			for (auto& task : m_concurrentTasksAfter)
			{
				task->resume();
			}
		}

		[[nodiscard]]
		bool done() const override
		{
			return !m_handle || m_handle.done();
		}

		[[nodiscard]]
		bool empty() const
		{
			return !m_handle;
		}

		[[nodiscard]]
		TResult value() const
		{
			if constexpr (std::is_void_v<TResult>)
			{
				if (m_handle)
				{
					m_handle.promise().value(); // 例外伝搬のためにvoidでも呼び出す
				}
				return;
			}
			else
			{
				if (!m_handle)
				{
					// 戻り値ありの場合は空のハンドルを許容しない
					throw Error{ U"Task::value() called on empty handle" };
				}
				return m_handle.promise().value();
			}
		}

		template <typename TResultOther>
		[[nodiscard]]
		Task<TResult> with(Task<TResultOther>&& task)&&
		{
			if (!m_handle || m_handle.done())
			{
				return std::move(*this);
			}
			m_concurrentTasksAfter.push_back(std::make_unique<Task<TResultOther>>(std::move(task)));
			return std::move(*this);
		}

		template <typename TResultOther>
		[[nodiscard]]
		Task<TResult> with(Task<TResultOther>&& task, WithTiming timing)&&
		{
			if (!m_handle || m_handle.done())
			{
				return std::move(*this);
			}

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
		Task<void> discardResult()&&
		{
			return [](Task<TResult> task) -> Task<void>
				{
					if (task.done())
					{
						co_return;
					}
					while (true)
					{
						task.resume();
						if (task.done())
						{
							break;
						}
						co_await NextFrame();
					}
				}(std::move(*this));
		}

		[[nodiscard]]
		ScopedTaskRunner runScoped(FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr)&&
		{
			return ScopedTaskRunner{ std::move(*this), std::move(finishCallback), std::move(cancelCallback) };
		}

		void runAddTo(MultiRunner& mr, FinishCallbackType<TResult> finishCallback = nullptr, std::function<void()> cancelCallback = nullptr)&&
		{
			mr.add(ScopedTaskRunner{ std::move(*this), std::move(finishCallback), std::move(cancelCallback) });
		}

		[[nodiscard]]
		Task<TResult> pausedWhile(std::function<bool()> fnIsPaused)&&
		{
			return [](Task<TResult> task, std::function<bool()> fnIsPaused) -> Task<TResult>
				{
					if (task.done())
					{
						co_return task.value();
					}
					while (true)
					{
						if (!fnIsPaused())
						{
							task.resume();
							if (task.done())
							{
								break;
							}
						}
						co_await NextFrame();
					}
					co_return task.value();
				}(std::move(*this), std::move(fnIsPaused));
		}

		[[nodiscard]]
		Task<TResult> delayed(const Duration& duration)&&;

		[[nodiscard]]
		Task<TResult> delayed(const Duration& duration, ISteadyClock* pSteadyClock)&&;
	};

	[[nodiscard]]
	inline Task<void> EmptyTask()
	{
		return Task<void>{ nullptr };
	}

	namespace detail
	{
		template <typename TResult>
		class [[nodiscard]] TaskAwaiter : public detail::IAwaiter
		{
			static_assert(!std::is_reference_v<TResult>, "TResult must not be a reference type");
			static_assert(std::is_move_constructible_v<TResult> || std::is_void_v<TResult>, "TResult must be move constructible");
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

			TaskAwaiter(TaskAwaiter<TResult>&& rhs) noexcept = default;

			TaskAwaiter<TResult>& operator=(TaskAwaiter<TResult>&& rhs) = delete;

			void resume() override
			{
				m_task.resume();
			}

			[[nodiscard]]
			bool done() const override
			{
				return m_task.done();
			}

			[[nodiscard]]
			bool await_ready()
			{
				return m_task.done();
			}

			template <typename TResultOther>
			bool await_suspend(std::coroutine_handle<detail::Promise<TResultOther>> handle)
			{
				resume();
				if (m_task.done())
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
		};

		class PromiseBase
		{
		protected:
			IAwaiter* m_pSubAwaiter = nullptr;

		public:
			PromiseBase() = default;

			PromiseBase(const PromiseBase&) = delete;

			PromiseBase& operator=(const PromiseBase&) = delete;

			PromiseBase(PromiseBase&& rhs) noexcept
				: m_pSubAwaiter(rhs.m_pSubAwaiter)
			{
				rhs.m_pSubAwaiter = nullptr;
			}

			PromiseBase& operator=(PromiseBase&& rhs) = delete;

			virtual ~PromiseBase() = 0;

			auto initial_suspend() noexcept
			{
				// suspend_neverにすれば関数呼び出し時点で実行開始されるが、
				// その場合Co::AllやCo::Anyに渡す際など引数の評価順が不定になり扱いづらいため、
				// ここではsuspend_alwaysにしてrunScoped実行まで実行を遅延させている
				return std::suspend_always{};
			}

			auto final_suspend() noexcept
			{
				return std::suspend_always{};
			}

			[[nodiscard]]
			bool resumeSubAwaiter()
			{
				if (!m_pSubAwaiter)
				{
					return false;
				}

				m_pSubAwaiter->resume();

				if (m_pSubAwaiter->done())
				{
					m_pSubAwaiter = nullptr;
					return false;
				}

				return true;
			}

			void setSubAwaiter(IAwaiter* pSubAwaiter) noexcept
			{
				m_pSubAwaiter = pSubAwaiter;
			}
		};

		inline PromiseBase::~PromiseBase() = default;

		template <typename TResult>
		class Promise : public PromiseBase
		{
			static_assert(!std::is_reference_v<TResult>, "TResult must not be a reference type");
			static_assert(std::is_move_constructible_v<TResult> || std::is_void_v<TResult>, "TResult must be move constructible");
			static_assert(!std::is_const_v<TResult>, "TResult must not have 'const' qualifier");

		private:
			std::unique_ptr<TResult> m_value;
			std::exception_ptr m_exception;
			bool m_resultConsumed = false;

		public:
			Promise() = default;

			Promise(Promise<TResult>&&) noexcept = default;

			Promise& operator=(Promise<TResult>&&) = delete;

			void return_value(const TResult& v) requires std::is_copy_constructible_v<TResult>
			{
				m_value = std::make_unique<TResult>(v);
			}

			void return_value(TResult&& v)
			{
				m_value = std::make_unique<TResult>(std::move(v));
			}

			[[nodiscard]]
			TResult value()
			{
				if (!m_value && !m_exception)
				{
					throw Error{ U"Task is not completed. Make sure that all paths in the coroutine return a value." };
				}
				if (m_resultConsumed)
				{
					throw Error{ U"Task result can be get only once." };
				}
				m_resultConsumed = true;
				if (m_exception)
				{
					std::rethrow_exception(m_exception);
				}
				return std::move(*m_value);
			}

			[[nodiscard]]
			Task<TResult> get_return_object()
			{
				return Task<TResult>{ Task<TResult>::handle_type::from_promise(*this) };
			}

			void unhandled_exception()
			{
				m_exception = std::current_exception();
			}
		};

		template <>
		class Promise<void> : public PromiseBase
		{
		private:
			std::exception_ptr m_exception;
			bool m_isResultSet = false;
			bool m_resultConsumed = false;

		public:
			Promise() = default;

			Promise(Promise<void>&&) noexcept = default;

			Promise<void>& operator=(Promise<void>&&) = delete;

			void return_void()
			{
				m_isResultSet = true;
			}

			void value()
			{
				if (!m_isResultSet && !m_exception)
				{
					throw Error{ U"Task is not completed." };
				}
				if (m_resultConsumed)
				{
					throw Error{ U"Task result can be get only once." };
				}
				m_resultConsumed = true;
				if (m_exception)
				{
					std::rethrow_exception(m_exception);
				}
			}

			[[nodiscard]]
			Task<void> get_return_object()
			{
				return Task<void>{ Task<void>::handle_type::from_promise(*this) };
			}

			void unhandled_exception()
			{
				m_exception = std::current_exception();
			}
		};
	}

	inline Task<void> ScopedTaskRunner::waitUntilDone() const&
	{
		while (!done())
		{
			co_await NextFrame();
		}
	}

	inline Task<void> MultiRunner::waitUntilAllDone() const&
	{
		while (!allDone())
		{
			co_await NextFrame();
		}
	}

	inline Task<void> MultiRunner::waitUntilAnyDone() const&
	{
		while (!anyDone())
		{
			co_await NextFrame();
		}
	}

	template <typename TResult = void>
	class [[nodiscard]] TaskFinishSource
	{
		static_assert(!std::is_reference_v<TResult>, "TResult must not be a reference type");
		static_assert(std::is_move_constructible_v<TResult> || std::is_void_v<TResult>, "TResult must be move constructible");
		static_assert(!std::is_const_v<TResult>, "TResult must not have 'const' qualifier");

	private:
		std::unique_ptr<TResult> m_result;
		bool m_resultConsumed = false;

	public:
		TaskFinishSource() = default;

		TaskFinishSource(const TaskFinishSource&) = delete;

		TaskFinishSource& operator=(const TaskFinishSource&) = delete;

		TaskFinishSource(TaskFinishSource&&) noexcept = default;

		TaskFinishSource& operator=(TaskFinishSource&&) = delete;

		~TaskFinishSource() noexcept = default;

		bool requestFinish(const TResult& result) requires std::is_copy_constructible_v<TResult>
		{
			if (m_resultConsumed || hasResult())
			{
				return false;
			}
			m_result = std::make_unique<TResult>(result);
			return true;
		}

		bool requestFinish(TResult&& result)
		{
			if (m_resultConsumed || hasResult())
			{
				return false;
			}
			m_result = std::make_unique<TResult>(std::move(result));
			return true;
		}

		[[nodiscard]]
		bool hasResult() const noexcept
		{
			return m_result != nullptr;
		}

		// hasResult()がtrueを返す場合のみ呼び出し可能。1回だけ取得でき、2回目以降の呼び出しは例外を投げる
		[[nodiscard]]
		TResult result()
		{
			if (m_resultConsumed)
			{
				throw Error{ U"TaskFinishSource: result can be get only once. Make sure to check if hasResult() returns true before calling result()." };
			}
			if (m_result == nullptr)
			{
				throw Error{ U"TaskFinishSource: TaskFinishSource does not have a result. Make sure to check if hasResult() returns true before calling result()." };
			}
			m_resultConsumed = true;

			auto result = std::move(*m_result);
			m_result.reset();
			return result;
		}

		[[nodiscard]]
		Task<TResult> waitForResult()
		{
			while (!hasResult())
			{
				co_await NextFrame();
			}
			m_resultConsumed = true;
			auto result = std::move(*m_result);
			m_result.reset();
			co_return result;
		}

		[[nodiscard]]
		Task<void> waitUntilDone() const
		{
			while (!done())
			{
				co_await NextFrame();
			}
		}

		[[nodiscard]]
		bool done() const noexcept
		{
			return m_result != nullptr || m_resultConsumed;
		}
	};

	template <>
	class [[nodiscard]] TaskFinishSource<void>
	{
	private:
		bool m_finishRequested = false;

	public:
		TaskFinishSource() = default;

		TaskFinishSource(const TaskFinishSource&) = delete;

		TaskFinishSource& operator=(const TaskFinishSource&) = delete;

		TaskFinishSource(TaskFinishSource&&) noexcept = default;

		TaskFinishSource& operator=(TaskFinishSource&&) = delete;

		~TaskFinishSource() noexcept = default;

		bool requestFinish()
		{
			if (m_finishRequested)
			{
				return false;
			}
			m_finishRequested = true;
			return true;
		}

		[[nodiscard]]
		Task<void> waitUntilDone() const
		{
			while (!done())
			{
				co_await NextFrame();
			}
		}

		[[nodiscard]]
		bool done() const noexcept
		{
			return m_finishRequested;
		}
	};

	[[nodiscard]]
	inline Task<void> UpdaterTask(std::function<void()> updateFunc)
	{
		while (true)
		{
			updateFunc();
			co_await NextFrame();
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
			co_await NextFrame();
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
			if (taskFinishSource.done())
			{
				co_return;
			}
			co_await NextFrame();
		}
	}

	template <typename TResult>
	auto operator co_await(Task<TResult>&& rhs)
	{
		return detail::TaskAwaiter<TResult>{ std::move(rhs) };
	}

	template <typename TResult>
	auto operator co_await(const Task<TResult>& rhs) = delete;

	inline void Init(AddsLicenseYN addsLicense = AddsLicenseYN::Yes)
	{
		if (addsLicense)
		{
			AddLicense();
		}
		detail::Backend::Init();
	}

	[[nodiscard]]
	inline bool HasActiveDrawerInLayer(Layer layer)
	{
		return detail::Backend::HasActiveDrawerInLayer(layer);
	}

	[[nodiscard]]
	inline bool HasActiveModal()
	{
		return HasActiveDrawerInLayer(Layer::Modal);
	}

	[[nodiscard]]
	inline bool HasActiveTransition()
	{
		return HasActiveDrawerInLayer(Layer::Transition_FadeIn) || HasActiveDrawerInLayer(Layer::Transition_General) || HasActiveDrawerInLayer(Layer::Transition_FadeOut);
	}

	[[nodiscard]]
	inline bool HasActiveFadeInTransition()
	{
		return HasActiveDrawerInLayer(Layer::Transition_FadeIn);
	}

	[[nodiscard]]
	inline bool HasActiveGeneralTransition()
	{
		return HasActiveDrawerInLayer(Layer::Transition_General);
	}

	[[nodiscard]]
	inline bool HasActiveFadeOutTransition()
	{
		return HasActiveDrawerInLayer(Layer::Transition_FadeOut);
	}

	template <typename TResult>
	[[nodiscard]]
	Task<TResult> FromResult(TResult result)
	{
		co_return result;
	}

	[[nodiscard]]
	inline Task<void> DelayFrame(int32 frames)
	{
		for (int32 i = 0; i < frames; ++i)
		{
			co_await NextFrame();
		}
	}

	namespace detail
	{
		// ポーズ中や同一フレーム内での多重更新を考慮したタイマー
		template <typename TInnerDuration>
		class DeltaAggregateTimerImpl
		{
		private:
			TInnerDuration m_duration;
			TInnerDuration m_elapsed = TInnerDuration{ 0 };
			TInnerDuration m_prevTime;
			int32 m_prevFrameCount;

		public:
			using InnerDurationRep = typename TInnerDuration::rep;

			DeltaAggregateTimerImpl(Duration duration, InnerDurationRep initialTime)
				: m_duration(DurationCast<TInnerDuration>(duration))
				, m_prevTime(initialTime)
				, m_prevFrameCount(Scene::FrameCount())
			{
			}

			[[nodiscard]]
			bool reachedZero() const
			{
				return m_elapsed >= m_duration;
			}

			void update(InnerDurationRep timeRep)
			{
				const int32 frameCount = Scene::FrameCount();
				const TInnerDuration time = TInnerDuration{ timeRep };

				// ポーズ中や同一フレーム内での多重更新は時間を進行させない
				// (ポーズ前後の1フレーム分の時間を加算していないのは仕様で、ISteadyClockの場合に絶対時間しか取得できず加算しようがないため)
				if (frameCount - m_prevFrameCount == 1)
				{
					m_elapsed += time - m_prevTime;
				}

				m_prevFrameCount = frameCount;
				m_prevTime = time;
			}

			[[nodiscard]]
			double progress0_1() const
			{
				if (m_duration.count() == 0)
				{
					return 1.0;
				}
				return Min(static_cast<double>(m_elapsed.count()) / static_cast<double>(m_duration.count()), 1.0);
			}
		};

		class DeltaAggregateTimer
		{
		private:
			std::variant<DeltaAggregateTimerImpl<SecondsF>, DeltaAggregateTimerImpl<std::chrono::duration<uint64, std::micro>>> m_impl;
			ISteadyClock* m_pSteadyClock;

		public:
			DeltaAggregateTimer(Duration duration, ISteadyClock* pSteadyClock)
				: m_impl(pSteadyClock
					? decltype(m_impl){ DeltaAggregateTimerImpl<std::chrono::duration<uint64, std::micro>>{ duration, pSteadyClock->getMicrosec() } }
					: decltype(m_impl){ DeltaAggregateTimerImpl<SecondsF>{ duration, Scene::Time() } })
				, m_pSteadyClock(pSteadyClock)
			{
			}

			[[nodiscard]]
			bool reachedZero() const
			{
				return std::visit([](const auto& impl) { return impl.reachedZero(); }, m_impl);
			}

			void update()
			{
				if (m_pSteadyClock)
				{
					std::get<DeltaAggregateTimerImpl<std::chrono::duration<uint64, std::micro>>>(m_impl).update(m_pSteadyClock->getMicrosec());
				}
				else
				{
					std::get<DeltaAggregateTimerImpl<SecondsF>>(m_impl).update(Scene::Time());
				}
			}

			[[nodiscard]]
			double progress0_1() const
			{
				return std::visit([](const auto& impl) { return impl.progress0_1(); }, m_impl);
			}
		};
	}

	[[nodiscard]]
	inline Task<void> Delay(const Duration duration)
	{
		detail::DeltaAggregateTimerImpl<SecondsF> timer{ duration, Scene::Time() };
		while (!timer.reachedZero())
		{
			co_await NextFrame();
			timer.update(Scene::Time());
		}
	}

	[[nodiscard]]
	inline Task<void> Delay(const Duration duration, ISteadyClock* pSteadyClock)
	{
		detail::DeltaAggregateTimer timer{ duration, pSteadyClock };
		while (!timer.reachedZero())
		{
			co_await NextFrame();
			timer.update();
		}
	}

	template <typename TResult>
	[[nodiscard]]
	Task<TResult> Task<TResult>::delayed(const Duration& duration)&&
	{
		return [](Task<TResult> task, Duration duration) -> Task<TResult>
			{
				co_await Delay(duration);
				co_return co_await std::move(task);
			}(std::move(*this), duration);
	}

	template <typename TResult>
	[[nodiscard]]
	Task<TResult> Task<TResult>::delayed(const Duration& duration, ISteadyClock* pSteadyClock)&&
	{
		return [](Task<TResult> task, Duration duration, ISteadyClock* pSteadyClock) -> Task<TResult>
			{
				co_await Delay(duration, pSteadyClock);
				co_return co_await std::move(task);
			}(std::move(*this), duration, pSteadyClock);
	}

	[[nodiscard]]
	inline Task<void> WaitForever()
	{
		while (true)
		{
			co_await NextFrame();
		}
	}

	namespace detail
	{
		template <class T>
		concept Predicate = std::invocable<T> && std::same_as<std::invoke_result_t<T>, bool>;
	}

	template <detail::Predicate TPredicate>
	[[nodiscard]]
	inline Task<void> WaitUntil(TPredicate predicate)
	{
		while (!predicate())
		{
			co_await NextFrame();
		}
	}

	template <detail::Predicate TPredicate>
	[[nodiscard]]
	inline Task<void> WaitWhile(TPredicate predicate)
	{
		while (predicate())
		{
			co_await NextFrame();
		}
	}

	template <typename T>
	[[nodiscard]]
	Task<T> WaitForResult(const std::optional<T>* pOptional)
	{
		while (!pOptional->has_value())
		{
			co_await NextFrame();
		}
		co_return **pOptional;
	}

	template <typename T>
	[[nodiscard]]
	Task<T> WaitForResult(const Optional<T>* pOptional)
	{
		while (!pOptional->has_value())
		{
			co_await NextFrame();
		}
		co_return **pOptional;
	}

	template <typename T>
	[[nodiscard]]
	Task<void> WaitUntilHasValue(const std::optional<T>* pOptional)
	{
		while (!pOptional->has_value())
		{
			co_await NextFrame();
		}
	}

	template <typename T>
	[[nodiscard]]
	Task<void> WaitUntilHasValue(const Optional<T>* pOptional)
	{
		while (!pOptional->has_value())
		{
			co_await NextFrame();
		}
	}

	template <typename T>
	[[nodiscard]]
	Task<void> WaitUntilValueChanged(const T* pValue)
	{
		const T initialValue = *pValue;
		while (*pValue == initialValue)
		{
			co_await NextFrame();
		}
	}

	[[nodiscard]]
	inline Task<void> WaitForTimer(const Timer* pTimer)
	{
		while (!pTimer->reachedZero())
		{
			co_await NextFrame();
		}
	}

	template <class TInput>
	[[nodiscard]]
	Task<void> WaitUntilDown(const TInput input)
	{
		while (!input.down())
		{
			co_await NextFrame();
		}
		input.clearInput();
	}

	template <class TInput>
	[[nodiscard]]
	Task<void> WaitUntilDown(const TInput input, ClearsInputYN clearsInput)
	{
		while (!input.down())
		{
			co_await NextFrame();
		}
		if (clearsInput)
		{
			input.clearInput();
		}
	}

	template <class TInput>
	[[nodiscard]]
	Task<void> WaitUntilUp(const TInput input)
	{
		if (input.cleared())
		{
			// clearInput実行直後もupがtrueになるが、それは無視
			co_await NextFrame();
		}
		while (!input.up())
		{
			co_await NextFrame();
		}
		input.clearInput();
	}

	template <class TInput>
	[[nodiscard]]
	Task<void> WaitUntilUp(const TInput input, ClearsInputYN clearsInput)
	{
		if (input.cleared())
		{
			// clearInput実行直後もupがtrueになるが、それは無視
			co_await NextFrame();
		}
		while (!input.up())
		{
			co_await NextFrame();
		}
		if (clearsInput)
		{
			input.clearInput();
		}
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilLeftClicked(const TArea area)
	{
		while (!area.leftClicked())
		{
			co_await NextFrame();
		}
		MouseL.clearInput();
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilLeftClicked(const TArea area, ClearsInputYN clearsInput)
	{
		while (!area.leftClicked())
		{
			co_await NextFrame();
		}
		if (clearsInput)
		{
			MouseL.clearInput();
		}
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilLeftReleased(const TArea area)
	{
		if (MouseL.cleared())
		{
			// clearInput実行直後もleftReleasedがtrueになるが、それは無視
			co_await NextFrame();
		}
		while (!area.leftReleased())
		{
			co_await NextFrame();
		}
		MouseL.clearInput();
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilLeftReleased(const TArea area, ClearsInputYN clearsInput)
	{
		if (MouseL.cleared())
		{
			// clearInput実行直後もleftReleasedがtrueになるが、それは無視
			co_await NextFrame();
		}
		while (!area.leftReleased())
		{
			co_await NextFrame();
		}
		if (clearsInput)
		{
			MouseL.clearInput();
		}
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilLeftClickedThenReleased(const TArea area)
	{
		while (true)
		{
			if (area.leftClicked())
			{
				const auto [releasedInArea, _] = co_await Any(WaitUntilLeftReleased(area, ClearsInputYN::No), WaitUntilUp(MouseL, ClearsInputYN::No));
				if (releasedInArea.has_value())
				{
					break;
				}
			}
			co_await NextFrame();
		}
		MouseL.clearInput();
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilLeftClickedThenReleased(const TArea area, ClearsInputYN clearsInput)
	{
		while (true)
		{
			if (area.leftClicked())
			{
				const auto [releasedInArea, _] = co_await Any(WaitUntilLeftReleased(area, ClearsInputYN::No), WaitUntilUp(MouseL, ClearsInputYN::No));
				if (releasedInArea.has_value())
				{
					break;
				}
			}
			co_await NextFrame();
		}
		if (clearsInput)
		{
			MouseL.clearInput();
		}
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilRightClicked(const TArea area)
	{
		while (!area.rightClicked())
		{
			co_await NextFrame();
		}
		MouseR.clearInput();
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilRightClicked(const TArea area, ClearsInputYN clearsInput)
	{
		while (!area.rightClicked())
		{
			co_await NextFrame();
		}
		if (clearsInput)
		{
			MouseR.clearInput();
		}
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilRightReleased(const TArea area)
	{
		if (MouseR.cleared())
		{
			// clearInput実行直後もrightReleasedがtrueになるが、それは無視
			co_await NextFrame();
		}
		while (!area.rightReleased())
		{
			co_await NextFrame();
		}
		MouseR.clearInput();
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilRightReleased(const TArea area, ClearsInputYN clearsInput)
	{
		if (MouseR.cleared())
		{
			// clearInput実行直後もrightReleasedがtrueになるが、それは無視
			co_await NextFrame();
		}
		while (!area.rightReleased())
		{
			co_await NextFrame();
		}
		if (clearsInput)
		{
			MouseR.clearInput();
		}
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilRightClickedThenReleased(const TArea area)
	{
		while (true)
		{
			if (area.rightClicked())
			{
				const auto [releasedInArea, _] = co_await Any(WaitUntilRightReleased(area, ClearsInputYN::No), WaitUntilUp(MouseR, ClearsInputYN::No));
				if (releasedInArea.has_value())
				{
					break;
				}
			}
			co_await NextFrame();
		}
		MouseR.clearInput();
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilRightClickedThenReleased(const TArea area, ClearsInputYN clearsInput)
	{
		while (true)
		{
			if (area.rightClicked())
			{
				const auto [releasedInArea, _] = co_await Any(WaitUntilRightReleased(area, ClearsInputYN::No), WaitUntilUp(MouseR, ClearsInputYN::No));
				if (releasedInArea.has_value())
				{
					break;
				}
			}
			co_await NextFrame();
		}
		if (clearsInput)
		{
			MouseR.clearInput();
		}
	}

	template <class TArea>
	[[nodiscard]]
	Task<void> WaitUntilMouseOver(const TArea area)
	{
		while (!area.mouseOver())
		{
			co_await NextFrame();
		}
	}

	// voidの参照やvoidを含むタプルは使用できないため、voidの代わりに戻り値として返すための空の構造体を用意
	struct VoidResult
	{
	};

	namespace detail
	{
		template <typename TResult>
		using VoidResultTypeReplace = std::conditional_t<std::is_void_v<TResult>, VoidResult, TResult>;

		template <typename TResult>
		[[nodiscard]]
		auto ConvertVoidResult(const Task<TResult>& task) -> VoidResultTypeReplace<TResult>
		{
			if constexpr (std::is_void_v<TResult>)
			{
				task.value(); // 例外伝搬のためにvoidでも呼び出す
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
			if (!task.done())
			{
				return none;
			}

			if constexpr (std::is_void_v<TResult>)
			{
				task.value(); // 例外伝搬のためにvoidでも呼び出す
				return MakeOptional(VoidResult{});
			}
			else
			{
				return MakeOptional(task.value());
			}
		}

		template <typename TTask>
		concept TaskConcept = std::is_same_v<TTask, Task<typename TTask::result_type>>;
	}

	template <detail::TaskConcept... TTasks>
	auto All(TTasks... args) -> Task<std::tuple<detail::VoidResultTypeReplace<typename TTasks::result_type>...>>
	{
		if ((args.done() && ...))
		{
			co_return std::make_tuple(detail::ConvertVoidResult(args)...);
		}

		while (true)
		{
			(args.resume(), ...);
			if ((args.done() && ...))
			{
				co_return std::make_tuple(detail::ConvertVoidResult(args)...);
			}
			co_await NextFrame();
		}
	}

	template <detail::TaskConcept... TTasks>
	auto Any(TTasks... args) -> Task<std::tuple<Optional<detail::VoidResultTypeReplace<typename TTasks::result_type>>...>>
	{
		static_assert(
			((std::is_copy_constructible_v<typename TTasks::result_type> || std::is_void_v<typename TTasks::result_type>) && ...),
			"Co::Any does not support tasks that return non-copy-constructible results; use discardResult() if the result is not needed.");

		if ((args.done() || ...))
		{
			co_return std::make_tuple(detail::ConvertOptionalVoidResult(args)...);
		}

		while (true)
		{
			(args.resume(), ...);
			if ((args.done() || ...))
			{
				co_return std::make_tuple(detail::ConvertOptionalVoidResult(args)...);
			}
			co_await NextFrame();
		}
	}
}

#ifndef NO_COTASKLIB_USING
using namespace cotasklib;
#endif
