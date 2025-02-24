//----------------------------------------------------------------------------------------
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
#include "Core.hpp"
#include "Ease.hpp"

namespace cotasklib::Co
{
	namespace detail
	{
		[[nodiscard]]
		inline Task<void> TypewriterTask(std::function<void(const String&)> callback, const Duration totalDuration, const String text, ISteadyClock* pSteadyClock)
		{
			Optional<std::size_t> prevLength = none; // textが空文字列の場合の初回コールバック呼び出しを考慮するためOptionalを使用
			detail::DeltaAggregateTimer timer{ totalDuration, pSteadyClock };
			while (true)
			{
				const double progress = timer.progress0_1();
				const std::size_t length = Min(static_cast<std::size_t>(1 + text.length() * progress), text.length());
				if (length != prevLength)
				{
					callback(text.substr(0, length));
					prevLength = length;
				}
				if (progress >= 1.0)
				{
					co_return;
				}
				co_await NextFrame();
				timer.update();
			}
		}
	}

	class [[nodiscard]] TypewriterTaskBuilder
	{
	private:
		std::function<void(const String&)> m_callback;
		Duration m_duration;
		bool m_isOneLetterDuration;
		String m_text;
		ISteadyClock* m_pSteadyClock;

		[[nodiscard]]
		Duration calcTotalDuration() const
		{
			return m_isOneLetterDuration ? m_duration * m_text.length() : m_duration;
		}

	public:
		explicit TypewriterTaskBuilder(std::function<void(const String&)> callback, Duration oneLetterDuration, StringView text, ISteadyClock* pSteadyClock)
			: m_callback(std::move(callback))
			, m_duration(oneLetterDuration)
			, m_isOneLetterDuration(true)
			, m_text(text)
			, m_pSteadyClock(pSteadyClock)
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

		TypewriterTaskBuilder& setClock(ISteadyClock* pSteadyClock)
		{
			m_pSteadyClock = pSteadyClock;
			return *this;
		}

		Task<void> play()
		{
			return detail::TypewriterTask(m_callback, calcTotalDuration(), m_text, m_pSteadyClock);
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

	[[nodiscard]]
	inline TypewriterTaskBuilder Typewriter(String* pText, Duration oneLetterDuration = 0s, StringView text = U"", ISteadyClock* pSteadyClock = nullptr)
	{
		return TypewriterTaskBuilder([pText](const String& text) { *pText = text; }, oneLetterDuration, text, pSteadyClock);
	}

	[[nodiscard]]
	inline TypewriterTaskBuilder Typewriter(std::function<void(const String&)> callback, Duration oneLetterDuration = 0s, StringView text = U"", ISteadyClock* pSteadyClock = nullptr)
	{
		return TypewriterTaskBuilder(std::move(callback), oneLetterDuration, text, pSteadyClock);
	}
}

#ifndef NO_COTASKLIB_USING
using namespace cotasklib;
#endif
