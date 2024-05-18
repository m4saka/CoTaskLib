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
			concept MemberFuncLerpable = requires(T a, T b, double t)
			{
				{ a.lerp(b, t) } -> std::convertible_to<T>;
			};

			template <typename T>
			concept Lerpable = StdLerpable<T> || MemberFuncLerpable<T>;

			template <typename T>
			T GenericLerp(const T& a, const T& b, double t) requires StdLerpable<T>
			{
				return std::lerp(a, b, t);
			}

			template <typename T>
			T GenericLerp(const T& a, const T& b, double t) requires MemberFuncLerpable<T>
			{
				return a.lerp(b, t);
			}

			template <typename T>
			[[nodiscard]]
			Task<void> Ease(const Duration duration, double easeFunc(double), std::function<void(T)> callback) requires std::floating_point<T>
			{
				if (duration.count() <= 0.0)
				{
					// durationが0の場合は何もしない
					co_return;
				}

				const Timer timer{ duration, StartImmediately::Yes };
				while (!timer.reachedZero())
				{
					callback(T{ easeFunc(timer.progress0_1()) });
					co_await detail::Yield{};
				}
				callback(T{ 1.0 });
				co_await detail::Yield{};
			}
		}

		#define DEFINE_EASE_FUNCTION(TYPE, EASE_FUNC) \
			template <typename T> \
			[[nodiscard]] \
			Task<void> TYPE(Duration duration, std::function<void(T)> callback) requires std::floating_point<T> \
			{ \
				return detail::Ease<T>(duration, EASE_FUNC, std::move(callback)); \
			} \
			template <typename T> \
			[[nodiscard]] \
			Task<void> TYPE(Duration duration, T* pValue) requires std::floating_point<T> \
			{ \
				return detail::Ease<T>(duration, EASE_FUNC, [pValue](T value) { *pValue = value; }); \
			} \
			template <typename T> \
			[[nodiscard]] \
			Task<void> TYPE(Duration duration, T from, T to, std::function<void(T)> callback) requires detail::Lerpable<T> \
			{ \
				return detail::Ease<T>(duration, EASE_FUNC, [from = std::move(from), to = std::move(to), callback = std::move(callback)](double t) { callback(detail::GenericLerp(from, to, t)); }); \
			} \
			template <typename T> \
			[[nodiscard]] \
			Task<void> TYPE(Duration duration, T from, T to, T* pValue) requires detail::Lerpable<T> \
			{ \
				return detail::Ease<T>(duration, EASE_FUNC, [from = std::move(from), to = std::move(to), pValue](double t) { *pValue = detail::GenericLerp(from, to, t); }); \
			}

		namespace EaseIn
		{
			DEFINE_EASE_FUNCTION(Linear, EaseInLinear)
			DEFINE_EASE_FUNCTION(Sine, EaseInSine)
			DEFINE_EASE_FUNCTION(Quad, EaseInQuad)
			DEFINE_EASE_FUNCTION(Cubic, EaseInCubic)
			DEFINE_EASE_FUNCTION(Quart, EaseInQuart)
			DEFINE_EASE_FUNCTION(Quint, EaseInQuint)
			DEFINE_EASE_FUNCTION(Expo, EaseInExpo)
			DEFINE_EASE_FUNCTION(Circ, EaseInCirc)
			DEFINE_EASE_FUNCTION(Back, EaseInBack)
			DEFINE_EASE_FUNCTION(Elastic, EaseInElastic)
			DEFINE_EASE_FUNCTION(Bounce, EaseInBounce)
		}

		namespace EaseOut
		{
			DEFINE_EASE_FUNCTION(Linear, EaseOutLinear)
			DEFINE_EASE_FUNCTION(Sine, EaseOutSine)
			DEFINE_EASE_FUNCTION(Quad, EaseOutQuad)
			DEFINE_EASE_FUNCTION(Cubic, EaseOutCubic)
			DEFINE_EASE_FUNCTION(Quart, EaseOutQuart)
			DEFINE_EASE_FUNCTION(Quint, EaseOutQuint)
			DEFINE_EASE_FUNCTION(Expo, EaseOutExpo)
			DEFINE_EASE_FUNCTION(Circ, EaseOutCirc)
			DEFINE_EASE_FUNCTION(Back, EaseOutBack)
			DEFINE_EASE_FUNCTION(Elastic, EaseOutElastic)
			DEFINE_EASE_FUNCTION(Bounce, EaseOutBounce)
		}

		namespace EaseInOut
		{
			DEFINE_EASE_FUNCTION(Linear, EaseInOutLinear)
			DEFINE_EASE_FUNCTION(Sine, EaseInOutSine)
			DEFINE_EASE_FUNCTION(Quad, EaseInOutQuad)
			DEFINE_EASE_FUNCTION(Cubic, EaseInOutCubic)
			DEFINE_EASE_FUNCTION(Quart, EaseInOutQuart)
			DEFINE_EASE_FUNCTION(Quint, EaseInOutQuint)
			DEFINE_EASE_FUNCTION(Expo, EaseInOutExpo)
			DEFINE_EASE_FUNCTION(Circ, EaseInOutCirc)
			DEFINE_EASE_FUNCTION(Back, EaseInOutBack)
			DEFINE_EASE_FUNCTION(Elastic, EaseInOutElastic)
			DEFINE_EASE_FUNCTION(Bounce, EaseInOutBounce)
		}
		
		#undef DEFINE_EASE_FUNCTION
	}
}
