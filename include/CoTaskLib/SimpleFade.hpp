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
#include "Sequence.hpp"
#include "Ease.hpp"

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
			class [[nodiscard]] SimpleFadeSequence : public SequenceBase<void>
			{
			private:
				Duration m_duration;
				ColorF m_color;
				ColorF m_toColor;
				double(*m_easeFunc)(double);
				int32 m_drawIndex;

			public:
				explicit SimpleFadeSequence(Duration duration, const ColorF& fromColor, const ColorF& toColor, double easeFunc(double), int32 drawIndex)
					: m_duration(duration)
					, m_color(fromColor)
					, m_toColor(toColor)
					, m_easeFunc(easeFunc)
					, m_drawIndex(drawIndex)
				{
				}

				Task<void> start() override
				{
					co_await Ease(m_duration, m_color, m_toColor, m_easeFunc).assignTo(&m_color).asTask();
				}

				void draw() const override
				{
					const Transformer2D transform{ Mat3x2::Identity(), Transformer2D::Target::SetLocal };

					Scene::Rect().draw(m_color);
				}
			};
		}

		constexpr int32 FadeInDrawIndex = 10000000;
		constexpr int32 FadeOutDrawIndex = 11000000;

		[[nodiscard]]
		inline Task<void> SimpleFadeIn(const Duration& duration, const ColorF& color = Palette::Black, double easeFunc(double) = Easing::Linear, int32 drawIndex = FadeInDrawIndex)
		{
			return AsTask<detail::SimpleFadeSequence>(duration, color, color.withA(0.0), easeFunc, drawIndex);
		}

		[[nodiscard]]
		inline Task<void> SimpleFadeOut(const Duration& duration, const ColorF& color = Palette::Black, double easeFunc(double) = Easing::Linear, int32 drawIndex = FadeOutDrawIndex)
		{
			return AsTask<detail::SimpleFadeSequence>(duration, color.withA(0.0), color, easeFunc, drawIndex);
		}
	}
}
