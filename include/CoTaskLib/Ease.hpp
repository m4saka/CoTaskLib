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
			return static_cast<T>(std::lerp(a, b, t));
		}

		template <MemberFuncLerpable T>
		T GenericLerp(const T& a, const T& b, double t)
		{
			return a.lerp(b, t);
		}

		[[nodiscard]]
		inline Task<void> EaseTask(std::function<void(double)> callback, const Duration duration, double easeFunc(double), ISteadyClock* pSteadyClock)
		{
			detail::DeltaAggregateTimer timer{ duration, pSteadyClock };
			while (true)
			{
				const double progress = timer.progress0_1();
				callback(easeFunc(progress));
				if (progress >= 1.0)
				{
					co_return;
				}
				co_await NextFrame();
				timer.update();
			}
		}

		template<typename T>
		struct IsVector2D : std::false_type {};

		template<typename T>
		struct IsVector2D<Vector2D<T>> : std::true_type {};

		template<typename T>
		struct IsVector3D : std::false_type {};

		template<typename T>
		struct IsVector3D<Vector3D<T>> : std::true_type {};

		template <typename T, typename U>
		concept Vector2DConvertibleFrom = detail::IsVector2D<T>::value && std::is_convertible_v<U, typename T::value_type>;

		template <typename T, typename U>
		concept Vector3DConvertibleFrom = detail::IsVector2D<T>::value && std::is_convertible_v<U, typename T::value_type>;

		template <typename T, typename U>
		concept VectorConvertibleFrom = (detail::IsVector2D<T>::value || detail::IsVector3D<T>::value) && std::is_convertible_v<U, typename T::value_type>;
	}
		
	template <typename T>
	class [[nodiscard]] EaseTaskBuilder
	{
	private:
		std::function<void(T)> m_callback;
		Duration m_duration;
		T m_from;
		T m_to;
		double(*m_easeFunc)(double);
		ISteadyClock* m_pSteadyClock;

	public:
		explicit EaseTaskBuilder(std::function<void(T)> callback, Duration duration, T from, T to, double(*easeFunc)(double), ISteadyClock* pSteadyClock)
			: m_callback(std::move(callback))
			, m_duration(duration)
			, m_from(std::move(from))
			, m_to(std::move(to))
			, m_easeFunc(easeFunc)
			, m_pSteadyClock(pSteadyClock)
		{
		}

		EaseTaskBuilder(const EaseTaskBuilder&) = default;
		EaseTaskBuilder& operator=(const EaseTaskBuilder&) = default;
		EaseTaskBuilder(EaseTaskBuilder&&) = default;
		EaseTaskBuilder& operator=(EaseTaskBuilder&&) = default;

		EaseTaskBuilder& duration(Duration duration)
		{
			m_duration = duration;
			return *this;
		}

		EaseTaskBuilder& from(T from)
		{
			m_from = std::move(from);
			return *this;
		}

		template <typename U>
		EaseTaskBuilder& from(U from) requires detail::VectorConvertibleFrom<T, U>
		{
			m_from = T::All(from);
			return *this;
		}

		template <typename U>
		EaseTaskBuilder& from(U x, U y) requires detail::Vector2DConvertibleFrom<T, U>
		{
			m_from = T{ x, y };
			return *this;
		}

		template <typename U>
		EaseTaskBuilder& from(U x, U y, U z) requires detail::Vector3DConvertibleFrom<T, U>
		{
			m_from = T{ x, y, z };
			return *this;
		}

		EaseTaskBuilder& to(T to)
		{
			m_to = std::move(to);
			return *this;
		}

		template <typename U>
		EaseTaskBuilder& to(U to) requires detail::VectorConvertibleFrom<T, U>
		{
			m_to = T::All(to);
			return *this;
		}

		template <typename U>
		EaseTaskBuilder& to(U x, U y) requires detail::Vector2DConvertibleFrom<T, U>
		{
			m_to = T{ x, y };
			return *this;
		}

		template <typename U>
		EaseTaskBuilder& to(U x, U y, U z) requires detail::Vector3DConvertibleFrom<T, U>
		{
			m_to = T{ x, y, z };
			return *this;
		}

		EaseTaskBuilder& fromTo(T from, T to)
		{
			m_from = std::move(from);
			m_to = std::move(to);
			return *this;
		}

		EaseTaskBuilder& setEase(double(*easeFunc)(double))
		{
			m_easeFunc = easeFunc;
			return *this;
		}

		EaseTaskBuilder& setClock(ISteadyClock* pSteadyClock)
		{
			m_pSteadyClock = pSteadyClock;
			return *this;
		}

		Task<void> play()
		{
			auto lerpedCallback = [from = m_from, to = m_to, callback = m_callback](double t)
				{
					callback(detail::GenericLerp(from, to, t));
				};
			return detail::EaseTask(std::move(lerpedCallback), m_duration, m_easeFunc, m_pSteadyClock);
		}

		ScopedTaskRunner playScoped()
		{
			return play().runScoped();
		}

		void playAddTo(MultiRunner& mr)
		{
			play().runAddTo(mr);
		}
	};

	template <detail::Lerpable T>
	[[nodiscard]]
	EaseTaskBuilder<T> Ease(T* pValue, Duration duration = 0s, double easeFunc(double) = EaseOutQuad, ISteadyClock* pSteadyClock = nullptr)
	{
		if constexpr (std::is_floating_point_v<T>)
		{
			// 浮動小数点数の場合は0.0から1.0までの補間をデフォルトにする
			return EaseTaskBuilder<T>([pValue](T value) { *pValue = value; }, duration, 0.0, 1.0, easeFunc, pSteadyClock);
		}
		else
		{
			return EaseTaskBuilder<T>([pValue](T value) { *pValue = value; }, duration, T{}, T{}, easeFunc, pSteadyClock);
		}
	}

	template <detail::Lerpable T>
	[[nodiscard]]
	EaseTaskBuilder<T> Ease(std::function<T> callback, Duration duration = 0s, double easeFunc(double) = EaseOutQuad, ISteadyClock* pSteadyClock = nullptr)
	{
		if constexpr (std::is_floating_point_v<T>)
		{
			// 浮動小数点数の場合は0.0から1.0までの補間をデフォルトにする
			return EaseTaskBuilder<T>(std::move(callback), duration, 0.0, 1.0, easeFunc, pSteadyClock);
		}
		else
		{
			return EaseTaskBuilder<T>(std::move(callback), duration, T{}, T{}, easeFunc, pSteadyClock);
		}
	}

	template <detail::Lerpable T>
	[[nodiscard]]
	EaseTaskBuilder<T> LinearEase(T* pValue, Duration duration = 0s, ISteadyClock* pSteadyClock = nullptr)
	{
		if constexpr (std::is_floating_point_v<T>)
		{
			// 浮動小数点数の場合は0.0から1.0までの補間をデフォルトにする
			return EaseTaskBuilder<T>([pValue](T value) { *pValue = value; }, duration, 0.0, 1.0, Easing::Linear, pSteadyClock);
		}
		else
		{
			return EaseTaskBuilder<T>([pValue](T value) { *pValue = value; }, duration, T{}, T{}, Easing::Linear, pSteadyClock);
		}
	}

	template <detail::Lerpable T>
	[[nodiscard]]
	EaseTaskBuilder<T> LinearEase(std::function<T> callback, Duration duration = 0s, ISteadyClock* pSteadyClock = nullptr)
	{
		if constexpr (std::is_floating_point_v<T>)
		{
			// 浮動小数点数の場合は0.0から1.0までの補間をデフォルトにする
			return EaseTaskBuilder<T>(std::move(callback), duration, 0.0, 1.0, Easing::Linear, pSteadyClock);
		}
		else
		{
			return EaseTaskBuilder<T>(std::move(callback), duration, T{}, T{}, Easing::Linear, pSteadyClock);
		}
	}
}

#ifndef NO_COTASKLIB_USING
using namespace cotasklib;
#endif
