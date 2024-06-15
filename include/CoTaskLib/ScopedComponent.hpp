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

namespace cotasklib
{
	namespace Co
	{
		class ScopedComponentBase
		{
		protected:
			virtual void update()
			{
			}

			virtual void updateInput()
			{
			}

			virtual void draw() const
			{
			}

			virtual int32 drawIndex() const
			{
				return DrawIndex::Default;
			}

		public:
			ScopedComponentBase() = default;

			virtual ~ScopedComponentBase() = 0;

			ScopedComponentBase(const ScopedComponentBase&) = delete;

			ScopedComponentBase& operator =(const ScopedComponentBase&) = delete;

			ScopedComponentBase(ScopedComponentBase&&) = default;

			ScopedComponentBase& operator =(ScopedComponentBase&&) = delete;
		};

		inline ScopedComponentBase::~ScopedComponentBase() = default;

		template <class TComponent>
		[[nodiscard]]
		Task<void> ShowComponent(TComponent&& component) requires std::is_base_of_v<ScopedComponentBase, TComponent>&& std::is_rvalue_reference_v<TComponent>
		{
			TComponent componentLocal = std::move(component);
			const ScopedUpdateInputCaller scopedUpdateInputCaller{ [&componentLocal] { componentLocal.updateInput(); }, [&componentLocal] { return -componentLocal.drawIndex(); } };
			const ScopedDrawer scopedDrawer{ [&componentLocal] { componentLocal.draw(); }, [&componentLocal] { return componentLocal.drawIndex(); } };
			co_await UpdaterTask([&componentLocal] { componentLocal.update(); });
		}

		template <class TComponent, class... Args>
		[[nodiscard]]
		Task<void> ShowComponent(Args&&... args)
		{
			return ShowComponent(TComponent{ std::forward<Args>(args)... });
		}

		class [[nodiscard]] ScopedScreenFill : public ScopedComponentBase
		{
		private:
			ColorF m_color;
			int32 m_drawIndex;

			void draw() const override
			{
				const Transformer2D transform{ Mat3x2::Identity(), Transformer2D::Target::SetLocal };
				Scene::Rect().draw(m_color);
			}

			int32 drawIndex() const override
			{
				return m_drawIndex;
			}

		public:
			explicit ScopedScreenFill(const ColorF& color, int32 drawIndex = DrawIndex::Modal)
				: m_color(color)
				, m_drawIndex(drawIndex)
			{
			}
		};
	}
}

#ifndef NO_COTASKLIB_USING
using namespace cotasklib;
#endif
