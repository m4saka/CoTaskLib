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
#include "Core.hpp"
#include "Sequence.hpp"
#include "Ease.hpp"

namespace cotasklib::Co
{
	namespace detail
	{
		inline void ScreenFill(const ColorF& color)
		{
			const Transformer2D transform{ Mat3x2::Identity(), Transformer2D::Target::SetLocal };

			Scene::Rect().draw(color);
		}

		class [[nodiscard]] ScreenFadeSequence : public SequenceBase<void>
		{
		private:
			Duration m_duration;
			ColorF m_color;
			ColorF m_toColor;
			double(*m_easeFunc)(double);
			ISteadyClock* m_pSteadyClock;

		public:
			explicit ScreenFadeSequence(Duration duration, const ColorF& fromColor, const ColorF& toColor, double easeFunc(double), Layer layer, int32 drawIndex, ISteadyClock* pSteadyClock)
				: SequenceBase<void>(layer, drawIndex)
				, m_duration(duration)
				, m_color(fromColor)
				, m_toColor(toColor)
				, m_easeFunc(easeFunc)
				, m_pSteadyClock(pSteadyClock)
			{
			}

			[[nodiscard]]
			Task<void> start() override
			{
				return Ease(&m_color, m_duration, m_easeFunc, m_pSteadyClock).fromTo(m_color, m_toColor).play();
			}

			void draw() const override
			{
				ScreenFill(m_color);
			}
		};
	}

	[[nodiscard]]
	inline Task<void> ScreenFadeIn(const Duration& duration, const ColorF& color = Palette::Black, double easeFunc(double) = Easing::Linear, Layer layer = Layer::Transition_FadeIn, int32 drawIndex = DrawIndex::Default, ISteadyClock* pSteadyClock = nullptr)
	{
		return Play<detail::ScreenFadeSequence>(duration, color, ColorF{ color, 0.0 }, easeFunc, layer, drawIndex, pSteadyClock);
	}

	[[nodiscard]]
	inline Task<void> ScreenFadeOut(const Duration& duration, const ColorF& color = Palette::Black, double easeFunc(double) = Easing::Linear, Layer layer = Layer::Transition_FadeOut, int32 drawIndex = DrawIndex::Default, ISteadyClock* pSteadyClock = nullptr)
	{
		return Play<detail::ScreenFadeSequence>(duration, ColorF{ color, 0.0 }, color, easeFunc, layer, drawIndex, pSteadyClock);
	}
}

#ifndef NO_COTASKLIB_USING
using namespace cotasklib;
#endif
