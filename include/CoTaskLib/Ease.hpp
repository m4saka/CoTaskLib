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
				return static_cast<T>(std::lerp(a, b, t));
			}

			template <MemberFuncLerpable T>
			T GenericLerp(const T& a, const T& b, double t)
			{
				return a.lerp(b, t);
			}

			[[nodiscard]]
			inline Task<void> EaseTask(std::function<void(double)> callback, const Duration duration, double easeFunc(double))
			{
				if (duration.count() <= 0.0)
				{
					// durationが0の場合は1.0を設定するが、フレームを待たずに終了
					callback(1.0);
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

			[[nodiscard]]
			inline Task<void> TypewriterTask(std::function<void(const String&)> callback, const Duration totalDuration, const String text)
			{
				if (totalDuration.count() <= 0.0)
				{
					// totalDurationが0の場合はそのまま出力するが、フレームを待たずに終了
					callback(text);
					co_return;
				}

				Optional<std::size_t> prevLength = none;
				const Timer timer{ totalDuration, StartImmediately::Yes };
				while (!timer.reachedZero())
				{
					const double t = timer.progress0_1();
					const std::size_t length = std::min(static_cast<std::size_t>(1 + text.length() * t), text.length());
					if (length != prevLength)
					{
						callback(text.substr(0, length));
						prevLength = length;
					}
					co_await detail::Yield{};
				}

				// 最後は必ず全て表示
				if (prevLength != text.length())
				{
					callback(text);
					co_await detail::Yield{};
				}
			}
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

		public:
			explicit EaseTaskBuilder(std::function<void(T)> callback, Duration duration, T from, T to, double(*easeFunc)(double))
				: m_callback(std::move(callback))
				, m_duration(duration)
				, m_from(std::move(from))
				, m_to(std::move(to))
				, m_easeFunc(easeFunc)
			{
			}

			EaseTaskBuilder(const EaseTaskBuilder&) = default;
			EaseTaskBuilder(EaseTaskBuilder&&) = default;
			EaseTaskBuilder& operator=(const EaseTaskBuilder&) = default;
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

			template <typename... Args>
			EaseTaskBuilder& from(Args&&... args)
			{
				m_from = T{ std::forward<Args>(args)... };
				return *this;
			}

			EaseTaskBuilder& to(T to)
			{
				m_to = std::move(to);
				return *this;
			}

			template <typename... Args>
			EaseTaskBuilder& to(Args&&... args)
			{
				m_to = T{ std::forward<Args>(args)... };
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

			Task<void> play()
			{
				auto lerpedCallback = [from = m_from, to = m_to, callback = m_callback](double t)
					{
						callback(detail::GenericLerp(from, to, t));
					};
				return detail::EaseTask(std::move(lerpedCallback), m_duration, m_easeFunc);
			}
		};

		template <detail::Lerpable T>
		[[nodiscard]]
		EaseTaskBuilder<T> Ease(T* pValue, Duration duration = 0s, double easeFunc(double) = EaseOutQuad)
		{
			if constexpr (std::is_floating_point_v<T>)
			{
				// 浮動小数点数の場合は0.0から1.0までの補間をデフォルトにする
				return EaseTaskBuilder<T>([pValue](T value) { *pValue = value; }, duration, 0.0, 1.0, easeFunc);
			}
			else
			{
				return EaseTaskBuilder<T>([pValue](T value) { *pValue = value; }, duration, T{}, T{}, easeFunc);
			}
		}

		template <detail::Lerpable T>
		[[nodiscard]]
		EaseTaskBuilder<T> Ease(std::function<T> callback, Duration duration = 0s, double easeFunc(double) = EaseOutQuad)
		{
			if constexpr (std::is_floating_point_v<T>)
			{
				// 浮動小数点数の場合は0.0から1.0までの補間をデフォルトにする
				return EaseTaskBuilder<T>(std::move(callback), duration, 0.0, 1.0, easeFunc);
			}
			else
			{
				return EaseTaskBuilder<T>(std::move(callback), duration, T{}, T{}, easeFunc);
			}
		}

		template <detail::Lerpable T>
		[[nodiscard]]
		EaseTaskBuilder<T> LinearEase(T* pValue, Duration duration = 0s)
		{
			if constexpr (std::is_floating_point_v<T>)
			{
				// 浮動小数点数の場合は0.0から1.0までの補間をデフォルトにする
				return EaseTaskBuilder<T>([pValue](T value) { *pValue = value; }, duration, 0.0, 1.0, Easing::Linear);
			}
			else
			{
				return EaseTaskBuilder<T>([pValue](T value) { *pValue = value; }, duration, T{}, T{}, Easing::Linear);
			}
		}

		template <detail::Lerpable T>
		[[nodiscard]]
		EaseTaskBuilder<T> LinearEase(std::function<T> callback, Duration duration = 0s)
		{
			if constexpr (std::is_floating_point_v<T>)
			{
				// 浮動小数点数の場合は0.0から1.0までの補間をデフォルトにする
				return EaseTaskBuilder<T>(std::move(callback), duration, 0.0, 1.0, Easing::Linear);
			}
			else
			{
				return EaseTaskBuilder<T>(std::move(callback), duration, T{}, T{}, Easing::Linear);
			}
		}

		class [[nodiscard]] TypewriterTaskBuilder
		{
		private:
			std::function<void(const String&)> m_callback;
			Duration m_duration;
			bool m_isOneLetterDuration;
			String m_text;

			Duration calcTotalDuration() const
			{
				return m_isOneLetterDuration ? m_duration * m_text.length() : m_duration;
			}

		public:
			explicit TypewriterTaskBuilder(std::function<void(const String&)> callback, Duration oneLetterDuration, StringView text)
				: m_callback(std::move(callback))
				, m_duration(oneLetterDuration)
				, m_isOneLetterDuration(true)
				, m_text(text)
			{
			}

			TypewriterTaskBuilder(const TypewriterTaskBuilder&) = default;
			TypewriterTaskBuilder(TypewriterTaskBuilder&&) = default;
			TypewriterTaskBuilder& operator=(const TypewriterTaskBuilder&) = default;
			TypewriterTaskBuilder& operator=(TypewriterTaskBuilder&&) = default;

			TypewriterTaskBuilder& oneLetterDuration(Duration oneLetterDuration)
			{
				m_duration = oneLetterDuration;
				m_isOneLetterDuration = true;
				return *this;
			}

			TypewriterTaskBuilder& totalDuration(Duration duration)
			{
				m_duration = duration;
				m_isOneLetterDuration = false;
				return *this;
			}

			TypewriterTaskBuilder& text(StringView text)
			{
				m_text = text;
				return *this;
			}

			Task<void> play()
			{
				return detail::TypewriterTask(m_callback, calcTotalDuration(), m_text);
			}
		};

		[[nodiscard]]
		inline TypewriterTaskBuilder Typewriter(String* pText)
		{
			return TypewriterTaskBuilder([pText](const String& text) { *pText = text; }, 1s, U"");
		}

		[[nodiscard]]
		inline TypewriterTaskBuilder Typewriter(String* pText, Duration oneLetterDuration)
		{
			return TypewriterTaskBuilder([pText](const String& text) { *pText = text; }, oneLetterDuration, U"");
		}

		[[nodiscard]]
		inline TypewriterTaskBuilder Typewriter(String* pText, Duration oneLetterDuration, StringView text)
		{
			return TypewriterTaskBuilder([pText](const String& text) { *pText = text; }, oneLetterDuration, text);
		}

		[[nodiscard]]
		inline TypewriterTaskBuilder Typewriter(std::function<void(const String&)> callback)
		{
			return TypewriterTaskBuilder(std::move(callback), 1s, U"");
		}

		[[nodiscard]]
		inline TypewriterTaskBuilder Typewriter(std::function<void(const String&)> callback, Duration oneLetterDuration)
		{
			return TypewriterTaskBuilder(std::move(callback), oneLetterDuration, U"");
		}

		[[nodiscard]]
		inline TypewriterTaskBuilder Typewriter(std::function<void(const String&)> callback, Duration oneLetterDuration, StringView text)
		{
			return TypewriterTaskBuilder(std::move(callback), oneLetterDuration, text);
		}
	}
}
