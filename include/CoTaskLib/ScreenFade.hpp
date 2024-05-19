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
				int32 m_drawIndex;

			public:
				explicit ScreenFadeSequence(Duration duration, const ColorF& fromColor, const ColorF& toColor, double easeFunc(double), int32 drawIndex)
					: m_duration(duration)
					, m_color(fromColor)
					, m_toColor(toColor)
					, m_easeFunc(easeFunc)
					, m_drawIndex(drawIndex)
				{
				}

				Task<void> start() override
				{
					return Ease(m_duration, m_color, m_toColor, m_easeFunc).assigning(&m_color);
				}

				void draw() const override
				{
					ScreenFill(m_color);
				}
			};

			class [[nodiscard]] ScreenFillSequence : public SequenceBase<void>
			{
			private:
				Duration m_duration;
				ColorF m_color;
				int32 m_drawIndex;

			public:
				explicit ScreenFillSequence(Duration duration, const ColorF& color, int32 drawIndex)
					: m_duration(duration)
					, m_color(color)
					, m_drawIndex(drawIndex)
				{
				}

				Task<void> start() override
				{
					return Delay(m_duration);
				}

				void draw() const override
				{
					ScreenFill(m_color);
				}
			};

			class [[nodiscard]] EndlessScreenFillSequence : public SequenceBase<void>
			{
			private:
				ColorF m_color;
				int32 m_drawIndex;

			public:
				explicit EndlessScreenFillSequence(const ColorF& color, int32 drawIndex)
					: m_color(color)
					, m_drawIndex(drawIndex)
				{
				}

				Task<void> start() override
				{
					return WaitForever();
				}

				void draw() const override
				{
					ScreenFill(m_color);
				}
			};
		}

		constexpr int32 ScreenFillDrawIndex = 0; // 通常の描画と同じ
		constexpr int32 ScreenFadeInDrawIndex = 10500000;
		constexpr int32 ScreenFadeOutDrawIndex = 10600000;

		[[nodiscard]]
		inline Task<void> ScreenFadeIn(const Duration& duration, const ColorF& color = Palette::Black, double easeFunc(double) = Easing::Linear, int32 drawIndex = ScreenFadeInDrawIndex)
		{
			return AsTask<detail::ScreenFadeSequence>(duration, color, color.withA(0.0), easeFunc, drawIndex);
		}

		[[nodiscard]]
		inline Task<void> ScreenFadeOut(const Duration& duration, const ColorF& color = Palette::Black, double easeFunc(double) = Easing::Linear, int32 drawIndex = ScreenFadeOutDrawIndex)
		{
			return AsTask<detail::ScreenFadeSequence>(duration, color.withA(0.0), color, easeFunc, drawIndex);
		}

		[[nodiscard]]
		inline Task<void> ScreenFill(const ColorF& color, int32 drawIndex = ScreenFillDrawIndex)
		{
			return AsTask<detail::EndlessScreenFillSequence>(color, drawIndex);
		}

		[[nodiscard]]
		inline Task<void> ScreenFill(const Duration& duration, const ColorF& color, int32 drawIndex = ScreenFillDrawIndex)
		{
			return AsTask<detail::ScreenFillSequence>(duration, color, drawIndex);
		}
	}
}
