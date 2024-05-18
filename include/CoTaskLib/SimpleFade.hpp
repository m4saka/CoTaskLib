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
			class [[nodiscard]] FadeSequenceBase : public SequenceBase<void>
			{
			private:
				int32 m_drawIndex;
				Timer m_timer;
				double m_t = 0.0;

			public:
				explicit FadeSequenceBase(const Duration& duration, int32 drawIndex)
					: m_drawIndex(drawIndex)
					, m_timer(duration, StartImmediately::No)
				{
				}

				virtual ~FadeSequenceBase() = default;

				virtual Task<void> start() override final
				{
					if (m_timer.duration().count() <= 0.0)
					{
						// durationが0の場合は何もしない
						co_return;
					}

					m_timer.start();
					while (true)
					{
						m_t = m_timer.progress0_1();
						if (m_t >= 1.0)
						{
							break;
						}
						co_await Yield{};
					}

					// 最後に必ずt=1.0で描画されるように
					m_t = 1.0;
					co_await Yield{};
				}

				virtual void draw() const override final
				{
					drawFade(m_t);
				}

				virtual int32 drawIndex() const override final
				{
					return m_drawIndex;
				}

				// tには時間が0.0～1.0で渡される
				virtual void drawFade(double t) const = 0;
			};

			class [[nodiscard]] SimpleFadeInSequence : public FadeSequenceBase
			{
			private:
				ColorF m_color;

			public:
				explicit SimpleFadeInSequence(const Duration& duration, const ColorF& color, int32 drawIndex)
					: FadeSequenceBase(duration, drawIndex)
					, m_color(color)
				{
				}

				void drawFade(double t) const override
				{
					const Transformer2D transform{ Mat3x2::Identity(), Transformer2D::Target::SetLocal };

					Scene::Rect().draw(ColorF{ m_color, 1.0 - t });
				}
			};

			class [[nodiscard]] SimpleFadeOutSequence : public FadeSequenceBase
			{
			private:
				ColorF m_color;

			public:
				explicit SimpleFadeOutSequence(const Duration& duration, const ColorF& color, int32 drawIndex)
					: FadeSequenceBase(duration, drawIndex)
					, m_color(color)
				{
				}

				void drawFade(double t) const override
				{
					const Transformer2D transform{ Mat3x2::Identity(), Transformer2D::Target::SetLocal };

					Scene::Rect().draw(ColorF{ m_color, t });
				}
			};
		}

		constexpr int32 FadeInDrawIndex = 10000000;
		constexpr int32 FadeOutDrawIndex = 11000000;

		[[nodiscard]]
		inline Task<void> SimpleFadeIn(const Duration& duration, const ColorF& color = Palette::Black, int32 drawIndex = FadeInDrawIndex)
		{
			return AsTask<detail::SimpleFadeInSequence>(duration, color, drawIndex);
		}

		[[nodiscard]]
		inline Task<void> SimpleFadeOut(const Duration& duration, const ColorF& color = Palette::Black, int32 drawIndex = FadeOutDrawIndex)
		{
			return AsTask<detail::SimpleFadeOutSequence>(duration, color, drawIndex);
		}
	}
}
