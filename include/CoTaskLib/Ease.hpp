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
		namespace detail
		{
			template <typename T>
			concept StdLerpable = requires(T a, T b, double t)
			{
				{ std::lerp(a, b, t) } -> std::convertible_to<T>;
			};

			template <typename T>
			concept MemberFuncLerpable = requires(T a, const T& b, double t)
			{
				{ a.lerp(b, t) } -> std::convertible_to<T>;
			};

			template <typename T>
			concept Lerpable = StdLerpable<T> || MemberFuncLerpable<T>;

			template <StdLerpable T>
			T GenericLerp(const T& a, const T& b, double t)
			{
				return std::lerp(a, b, t);
			}

			template <MemberFuncLerpable T>
			T GenericLerp(const T& a, const T& b, double t)
			{
				return a.lerp(b, t);
			}

			[[nodiscard]]
			inline Task<void> EaseTask(const Duration duration, double easeFunc(double), std::function<void(double)> callback)
			{
				if (duration.count() <= 0.0)
				{
					// durationが0の場合は何もしない
					co_return;
				}

				const Timer timer{ duration, StartImmediately::Yes };
				while (!timer.reachedZero())
				{
					callback(easeFunc(timer.progress0_1()));
					co_await detail::Yield{};
				}

				// 最後は必ず1.0になるようにする
				callback(1.0);
				co_await detail::Yield{};
			}
		}

		template <detail::Lerpable T>
		[[nodiscard]]
		class EaseTaskBuilder
		{
		private:
			Duration m_duration;
			T m_from;
			T m_to;
			double(*m_easeFunc)(double);
			T* m_pValue = nullptr;
			std::function<void(T)> m_updateFunc = nullptr;

		public:
			explicit EaseTaskBuilder(Duration duration, T from, T to, double(*easeFunc)(double))
				: m_duration(duration)
				, m_from(std::move(from))
				, m_to(std::move(to))
				, m_easeFunc(easeFunc)
			{
			}

			EaseTaskBuilder(const EaseTaskBuilder&) = default;
			EaseTaskBuilder(EaseTaskBuilder&&) = default;
			EaseTaskBuilder& operator=(const EaseTaskBuilder&) = default;
			EaseTaskBuilder& operator=(EaseTaskBuilder&&) = default;

			EaseTaskBuilder& from(T from)
			{
				m_from = std::move(from);
				return *this;
			}

			EaseTaskBuilder& to(T to)
			{
				m_to = std::move(to);
				return *this;
			}

			EaseTaskBuilder& setEase(double(*easeFunc)(double))
			{
				m_easeFunc = easeFunc;
				return *this;
			}

			EaseTaskBuilder& withUpdater(std::function<void(T)> updateFunc)
			{
				m_updateFunc = std::move(updateFunc);
				return *this;
			}

			EaseTaskBuilder& assignTo(T* pValue)
			{
				m_pValue = pValue;
				return *this;
			}

			[[nodiscard]]
			Task<void> asTask() const
			{
				auto callback = [from = m_from, to = m_to, pValue = m_pValue, updateFunc = m_updateFunc](double t)
					{
						if (pValue)
						{
							*pValue = detail::GenericLerp(from, to, t);
						}
						if (updateFunc)
						{
							updateFunc(detail::GenericLerp(from, to, t));
						}
					};
				return detail::EaseTask(m_duration, m_easeFunc, std::move(callback));
			}

			[[nodiscard]]
			ScopedTaskRunner runScoped() const
			{
				return asTask().runScoped();
			}
		};

		template <detail::Lerpable T>
		[[nodiscard]]
		EaseTaskBuilder<T> Ease(Duration duration)
		{
			if constexpr (std::is_floating_point_v<T>)
			{
				// 浮動小数点数の場合は0.0から1.0までの補間をデフォルトにする
				return EaseTaskBuilder<T>(duration, 0.0, 1.0, EaseOutQuad);
			}
			else
			{
				return EaseTaskBuilder<T>(duration, T{}, T{}, EaseOutQuad);
			}
		}

		template <detail::Lerpable T>
		[[nodiscard]]
		EaseTaskBuilder<T> Ease(Duration duration, T from, T to)
		{
			return EaseTaskBuilder<T>(duration, std::move(from), std::move(to), EaseOutQuad);
		}

		template <detail::Lerpable T>
		[[nodiscard]]
		EaseTaskBuilder<T> Ease(Duration duration, T from, T to, double(*easeFunc)(double))
		{
			return EaseTaskBuilder<T>(duration, std::move(from), std::move(to), easeFunc);
		}
	}
}
