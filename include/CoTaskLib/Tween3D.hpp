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
		struct ScopedTween3D
		{
			Optional<Transformer3D> transformer = none;
		};

		class Tweener3D
		{
		private:
			Vec3 m_pivot = Vec3::Zero();
			Vec3 m_position = Vec3::Zero();
			double(*m_easeFuncPosition)(double);
			Vec3 m_scale = Vec3::One();
			double(*m_easeFuncScale)(double);
			Vec3 m_rollPitchYaw = Vec3::Zero(); // オイラー角で持つ
			double(*m_easeFuncRotation)(double);

		public:
			explicit Tweener3D(Vec3 pivot = Vec3::Zero(), double defaultEaseFunc(double) = EaseOutQuad)
				: m_pivot(pivot)
				, m_easeFuncPosition(defaultEaseFunc)
				, m_easeFuncScale(defaultEaseFunc)
				, m_easeFuncRotation(defaultEaseFunc)
			{
			}

			~Tweener3D() = default;

			Tweener3D(const Tweener3D&) = default;
			Tweener3D& operator=(const Tweener3D&) = default;
			Tweener3D(Tweener3D&&) = default;
			Tweener3D& operator=(Tweener3D&&) = default;

			const Vec3& pivot() const
			{
				return m_pivot;
			}

			void setPivot(const Vec3& pivot)
			{
				m_pivot = pivot;
			}

			EaseTaskBuilder<Vec3> tweenPosition(Duration duration)
			{
				return Ease(&m_position, duration, m_easeFuncPosition).fromTo(m_position, m_position);
			}

			const Vec3& position() const
			{
				return m_position;
			}

			void setPosition(const Vec3& position)
			{
				m_position = position;
			}

			void setPositionEase(double easeFunc(double))
			{
				m_easeFuncPosition = easeFunc;
			}

			EaseTaskBuilder<Vec3> tweenScale(Duration duration)
			{
				return Ease(&m_scale, duration, m_easeFuncScale).fromTo(m_scale, m_scale);
			}

			const Vec3& scale() const
			{
				return m_scale;
			}

			void setScale(const Vec3& scale)
			{
				m_scale = scale;
			}

			void setScaleEase(double easeFunc(double))
			{
				m_easeFuncScale = easeFunc;
			}

			EaseTaskBuilder<Vec3> tweenRollPitchYaw(Duration duration)
			{
				return Ease(&m_rollPitchYaw, duration, m_easeFuncRotation).fromTo(m_rollPitchYaw, m_rollPitchYaw);
			}

			EaseTaskBuilder<double> tweenRoll(Duration duration)
			{
				return Ease(&m_rollPitchYaw.x, duration, m_easeFuncRotation).fromTo(m_rollPitchYaw.x, m_rollPitchYaw.x);
			}

			EaseTaskBuilder<double> tweenPitch(Duration duration)
			{
				return Ease(&m_rollPitchYaw.y, duration, m_easeFuncRotation).fromTo(m_rollPitchYaw.y, m_rollPitchYaw.y);
			}

			EaseTaskBuilder<double> tweenYaw(Duration duration)
			{
				return Ease(&m_rollPitchYaw.z, duration, m_easeFuncRotation).fromTo(m_rollPitchYaw.z, m_rollPitchYaw.z);
			}

			const Vec3& rollPitchYaw() const
			{
				return m_rollPitchYaw;
			}

			double roll() const
			{
				return m_rollPitchYaw.x;
			}

			double pitch() const
			{
				return m_rollPitchYaw.y;
			}

			double yaw() const
			{
				return m_rollPitchYaw.z;
			}

			void setRoll(double roll)
			{
				m_rollPitchYaw.x = roll;
			}

			void setPitch(double pitch)
			{
				m_rollPitchYaw.y = pitch;
			}

			void setYaw(double yaw)
			{
				m_rollPitchYaw.z = yaw;
			}

			void setRollPitchYaw(double roll, double pitch, double yaw)
			{
				m_rollPitchYaw = Vec3{ roll, pitch, yaw };
			}

			void setRollPitchYaw(const Vec3& rollPitchYaw)
			{
				m_rollPitchYaw = rollPitchYaw;
			}

			void setRotationEase(double easeFunc(double))
			{
				m_easeFuncRotation = easeFunc;
			}

			ScopedTween applyScoped() const
			{
				ScopedTween scopedTween;

				bool hasTransform = false;
				Mat4x4 mat = Mat4x4::Identity();

				// Easeの最終フレームは必ず目標値がそのまま代入されており誤差は出ないため、ここでは計算機イプシロンを考慮した比較はしない

				if (m_rollPitchYaw != Vec3::Zero())
				{
					hasTransform = true;

					if (m_pivot != Vec3::Zero())
					{
						mat *= Mat4x4::Translate(-m_pivot);
					}

					// 引数の順番が異なる(pitch, yaw, roll)点に注意
					mat *= Mat4x4::RollPitchYaw(m_rollPitchYaw.y, m_rollPitchYaw.z, m_rollPitchYaw.x);

					if (m_pivot != Vec3::Zero())
					{
						mat *= Mat4x4::Translate(m_pivot);
					}
				}

				if (m_scale != Vec3::One())
				{
					hasTransform = true;
					mat *= Mat4x4::Scale(m_scale, m_pivot);
				}

				if (m_position != Vec3::Zero())
				{
					hasTransform = true;
					mat *= Mat4x4::Translate(m_position);
				}

				return scopedTween;
			}
		};
	}
}
