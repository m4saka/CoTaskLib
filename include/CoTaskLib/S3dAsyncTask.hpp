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

namespace cotasklib::Co
{
	namespace detail
	{
		template <typename TResult>
		class S3dAsyncTaskAwaiter : public IAwaiter
		{
		private:
			AsyncTask<TResult> m_asyncTask;
			bool m_isDone = false;

		public:
			explicit S3dAsyncTaskAwaiter(AsyncTask<TResult>&& asyncTask)
				: m_asyncTask(std::move(asyncTask))
			{
			}

			S3dAsyncTaskAwaiter(const S3dAsyncTaskAwaiter<TResult>&) = delete;

			S3dAsyncTaskAwaiter<TResult>& operator=(const S3dAsyncTaskAwaiter<TResult>&) = delete;

			S3dAsyncTaskAwaiter(S3dAsyncTaskAwaiter<TResult>&&) noexcept = default;

			S3dAsyncTaskAwaiter<TResult>& operator=(S3dAsyncTaskAwaiter<TResult>&&) = delete;

			void resume() override
			{
				if (m_isDone)
				{
					return;
				}
				m_isDone = m_asyncTask.isReady();
			}

			bool done() const override
			{
				return m_isDone;
			}

			bool await_ready() const
			{
				return m_isDone;
			}

			template <typename TResultOther>
			bool await_suspend(std::coroutine_handle<detail::Promise<TResultOther>> handle)
			{
				resume();
				if (m_isDone)
				{
					// フレーム待ちなしで終了した場合は登録不要
					return false;
				}
				handle.promise().setSubAwaiter(this);
				return true;
			}

			TResult await_resume()
			{
				return m_asyncTask.get();
			}
		};

		class S3dAsyncHTTPTaskAwaiter : public IAwaiter
		{
		private:
			AsyncHTTPTask m_asyncHTTPTask;
			bool m_isDone = false;

		public:
			explicit S3dAsyncHTTPTaskAwaiter(AsyncHTTPTask&& asyncHTTPTask)
				: m_asyncHTTPTask(std::move(asyncHTTPTask))
			{
			}

			explicit S3dAsyncHTTPTaskAwaiter(const AsyncHTTPTask& asyncHTTPTask)
				: m_asyncHTTPTask(asyncHTTPTask)
			{
			}

			// AwaitHTTPTaskはコピー可能だが、他に合わせて保守的にコピー・ムーブ代入禁止にしている

			S3dAsyncHTTPTaskAwaiter(const S3dAsyncHTTPTaskAwaiter&) = delete;

			S3dAsyncHTTPTaskAwaiter& operator=(const S3dAsyncHTTPTaskAwaiter&) = delete;

			S3dAsyncHTTPTaskAwaiter(S3dAsyncHTTPTaskAwaiter&&) = default;

			S3dAsyncHTTPTaskAwaiter& operator=(S3dAsyncHTTPTaskAwaiter&&) = delete;

			void resume() override
			{
				if (m_isDone)
				{
					return;
				}
				m_isDone = m_asyncHTTPTask.isReady();
			}

			bool done() const override
			{
				return m_isDone;
			}

			bool await_ready() const
			{
				return m_isDone;
			}

			template <typename TResult>
			bool await_suspend(std::coroutine_handle<detail::Promise<TResult>> handle)
			{
				resume();
				if (m_isDone)
				{
					// フレーム待ちなしで終了した場合は登録不要
					return false;
				}
				handle.promise().setSubAwaiter(this);
				return true;
			}

			HTTPResponse await_resume()
			{
				return m_asyncHTTPTask.getResponse();
			}
		};
	}
}

namespace cotasklib
{
	template <typename TResult>
	[[nodiscard]]
	auto operator co_await(AsyncTask<TResult>&& asyncTask)
	{
		return Co::detail::S3dAsyncTaskAwaiter{ std::move(asyncTask) };
	}

	[[nodiscard]]
	inline auto operator co_await(AsyncHTTPTask&& asyncHTTPTask)
	{
		return Co::detail::S3dAsyncHTTPTaskAwaiter{ std::move(asyncHTTPTask) };
	}

	[[nodiscard]]
	inline auto operator co_await(const AsyncHTTPTask& asyncHTTPTask)
	{
		return Co::detail::S3dAsyncHTTPTaskAwaiter{ asyncHTTPTask };
	}
}

namespace cotasklib::Co
{
	namespace detail
	{
		template <typename TResult>
		[[nodiscard]]
		Task<TResult> WaitForResultImpl(std::unique_ptr<AsyncTask<TResult>> asyncTask)
		{
			using ::cotasklib::operator co_await;
			co_return co_await std::move(*asyncTask);
		}

		[[nodiscard]]
		inline Task<HTTPResponse> WaitForResultImpl(std::unique_ptr<AsyncHTTPTask> asyncHTTPTask)
		{
			using ::cotasklib::operator co_await;
			co_return co_await std::move(*asyncHTTPTask);
		}
	}

	template <typename TResult>
	[[nodiscard]]
	Task<TResult> WaitForResult(AsyncTask<TResult>&& asyncTask)
	{
		return detail::WaitForResultImpl(std::make_unique<AsyncTask<TResult>>(std::move(asyncTask)));
	}

	[[nodiscard]]
	inline Task<HTTPResponse> WaitForResult(AsyncHTTPTask&& asyncHTTPTask)
	{
		return detail::WaitForResultImpl(std::make_unique<AsyncHTTPTask>(std::move(asyncHTTPTask)));
	}

	[[nodiscard]]
	inline Task<HTTPResponse> WaitForResult(const AsyncHTTPTask& asyncHTTPTask)
	{
		return detail::WaitForResultImpl(std::make_unique<AsyncHTTPTask>(asyncHTTPTask));
	}
}

#ifndef NO_COTASKLIB_USING
using namespace cotasklib;
using cotasklib::operator co_await;
#endif
