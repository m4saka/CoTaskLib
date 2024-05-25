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
#include "Ease.hpp"

#ifdef NO_COTASKLIB_USING
namespace cotasklib
#else
inline namespace cotasklib
#endif
{
	namespace Co
	{
		struct ScopedTween
		{
			Optional<Transformer2D> transformer = none;
			Optional<ScopedColorMul2D> color = none;
			Optional<ScopedColorAdd2D> colorAdd = none;
		};

		class Tweener
		{
		private:
			Vec2 m_pivot = Vec2::Zero();
			Vec2 m_position = Vec2::Zero();
			double(*m_easeFuncPosition)(double);
			Vec2 m_scale = Vec2::One();
			double(*m_easeFuncScale)(double);
			double m_rotation = 0.0;
			double(*m_easeFuncRotation)(double);
			ColorF m_color = Palette::White;
			double(*m_easeFuncColor)(double);
			ColorF m_colorAdd = Palette::Black;
			double(*m_easeFuncColorAdd)(double);
			double m_alpha = 1.0;
			double(*m_easeFuncAlpha)(double);

		public:
			explicit Tweener(Vec2 pivot = Scene::CenterF(), double defaultEaseFunc(double) = EaseOutQuad)
				: m_pivot(pivot)
				, m_easeFuncPosition(defaultEaseFunc)
				, m_easeFuncScale(defaultEaseFunc)
				, m_easeFuncRotation(defaultEaseFunc)
				, m_easeFuncColor(defaultEaseFunc)
				, m_easeFuncColorAdd(defaultEaseFunc)
				, m_easeFuncAlpha(defaultEaseFunc)
			{
			}

			~Tweener() = default;

			Tweener(const Tweener&) = default;
			Tweener& operator=(const Tweener&) = default;
			Tweener(Tweener&&) = default;
			Tweener& operator=(Tweener&&) = default;

			void setPivot(const Vec2& pivot)
			{
				m_pivot = pivot;
			}

			EaseTaskBuilder<Vec2> tweenPosition(Duration duration)
			{
				return Ease(&m_position, duration, m_easeFuncPosition).fromTo(m_position, m_position);
			}

			void setPosition(const Vec2& position)
			{
				m_position = position;
			}

			void setPositionEase(double easeFunc(double))
			{
				m_easeFuncPosition = easeFunc;
			}

			EaseTaskBuilder<Vec2> tweenScale(Duration duration)
			{
				return Ease(&m_scale, duration, m_easeFuncScale).fromTo(m_scale, m_scale);
			}

			void setScale(const Vec2& scale)
			{
				m_scale = scale;
			}

			void setScale(double scale)
			{
				m_scale = Vec2::All(scale);
			}

			void setScaleEase(double easeFunc(double))
			{
				m_easeFuncScale = easeFunc;
			}

			EaseTaskBuilder<double> tweenRotation(Duration duration)
			{
				return Ease(&m_rotation, duration, m_easeFuncRotation).fromTo(m_rotation, m_rotation);
			}

			void setRotation(double rotation)
			{
				m_rotation = rotation;
			}

			void setRotationEase(double easeFunc(double))
			{
				m_easeFuncRotation = easeFunc;
			}

			EaseTaskBuilder<ColorF> tweenColor(Duration duration)
			{
				return Ease(&m_color, duration, m_easeFuncColor).fromTo(m_color, m_color);
			}

			void setColor(const ColorF& color)
			{
				m_color = color;
			}

			void setColorEase(double easeFunc(double))
			{
				m_easeFuncColor = easeFunc;
			}

			EaseTaskBuilder<ColorF> tweenColorAdd(Duration duration)
			{
				return Ease(&m_colorAdd, duration, m_easeFuncColorAdd).fromTo(m_colorAdd, m_colorAdd);
			}

			void setColorAdd(const ColorF& colorAdd)
			{
				m_colorAdd = colorAdd;
			}

			void setColorAddEase(double easeFunc(double))
			{
				m_easeFuncColorAdd = easeFunc;
			}

			EaseTaskBuilder<double> tweenAlpha(Duration duration)
			{
				return Ease(&m_alpha, duration, m_easeFuncAlpha).fromTo(m_alpha, m_alpha);
			}

			void setAlpha(double alpha)
			{
				m_alpha = alpha;
			}

			void setAlphaEase(double easeFunc(double))
			{
				m_easeFuncAlpha = easeFunc;
			}

			ScopedTween applyScoped() const
			{
				ScopedTween scopedTween;

				bool hasTransform = false;
				Mat3x2 mat = Mat3x2::Identity();

				// Easeの最終フレームは必ず目標値がそのまま代入されており誤差は出ないため、ここでは計算機イプシロンを考慮した比較はしない

				if (m_rotation != 0.0)
				{
					hasTransform = true;
					mat = mat.rotated(m_rotation, m_pivot);
				}

				if (m_scale != Vec2::One())
				{
					hasTransform = true;
					mat = mat.scaled(m_scale, m_pivot);
				}

				if (m_position != Vec2::Zero())
				{
					hasTransform = true;
					mat = mat.translated(m_position);
				}

				if (hasTransform && mat != Mat3x2::Identity())
				{
					scopedTween.transformer.emplace(mat);
				}

				if (m_alpha != 1.0 || m_color != ColorF{ Palette::White })
				{
					scopedTween.color.emplace(m_color.withA(m_color.a * m_alpha));
				}

				if (m_colorAdd != ColorF{ Palette::Black })
				{
					scopedTween.colorAdd.emplace(m_colorAdd);
				}

				return scopedTween;
			}
		};
	}
}
