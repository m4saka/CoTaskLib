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
#include "Tween.hpp"

namespace cotasklib::Co
{
	namespace detail
	{
		static constexpr Vec2 SimpleDialogSize{ 600, 240 };
		static constexpr Vec2 FadeInScaleFrom{ 0.8, 0.8 };
		static constexpr Vec2 FadeInScaleTo{ 1.0, 1.0 };
		static constexpr Vec2 ButtonSize{ 120, 40 };
		static constexpr double ButtonMargin = 20;
		static constexpr double ButtonOffsetY = 50;
		static constexpr double FooterOffsetY = 60;
		static constexpr ColorF BackgroundColor{ 1.0 };
		static constexpr ColorF BackgroundColorFooter{ 0.8 };
		static constexpr ColorF FrameColor{ 0.67 };
		static constexpr ColorF ButtonMouseOverColor{ 0.9, 0.95, 1.0 };
		static constexpr ColorF ButtonPressedColor{ 0.8, 0.85, 0.9 };
		static constexpr double ButtonRoundSize = 4.8;
		static constexpr double ButtonFrameThickness = 1;
		static constexpr double ButtonFrameThicknessPressed = 2;
		static constexpr ColorF ButtonTextColor = Palette::Black;
		static constexpr double SimpleDialogRoundSize = 8;
		static constexpr ColorF TextColor = Palette::Black;
		static constexpr Duration FadeDuration = 0.25s;

		struct SimpleButton
		{
		private:
			String m_text;
			RectF m_rect;
			s3d::RoundRect m_roundRect;
			bool m_interactable;
			bool m_isPressed = false;
			bool m_isClicked = false;

		public:
			SimpleButton(StringView text, RectF rect, bool interactable = true)
				: m_text(text)
				, m_rect(rect)
				, m_roundRect(rect.rounded(ButtonRoundSize))
				, m_interactable(interactable)
			{
			}

			void update()
			{
				if (m_rect.mouseOver())
				{
					if (MouseL.down() && !m_isPressed)
					{
						// 領域内でクリック開始
						m_isPressed = true;
					}
				}
				else
				{
					if (MouseL.down())
					{
						// 領域外でクリックが開始
						m_isPressed = false;
					}
				}

				if (MouseL.up())
				{
					if (m_isPressed && m_rect.mouseOver())
					{
						// 領域内でクリック終了
						m_isClicked = true;
					}
					m_isPressed = false;
				}
				else
				{
					m_isClicked = false;
				}
			}

			void draw() const
			{
				ColorF color;
				if (m_interactable && m_rect.mouseOver())
				{
					if (m_isPressed)
					{
						color = ButtonPressedColor;
						Cursor::RequestStyle(CursorStyle::Hand);
					}
					else if (MouseL.pressed())
					{
						// 領域外でクリック開始された場合のマウスオーバーでは色を変えない
						color = BackgroundColor;
					}
					else
					{
						color = ButtonMouseOverColor;
						Cursor::RequestStyle(CursorStyle::Hand);
					}
				}
				else
				{
					color = BackgroundColor;
				}

				m_roundRect
					.draw(color)
					.drawFrame(m_isPressed ? ButtonFrameThicknessPressed : ButtonFrameThickness, 0, FrameColor);

				const auto& font = SimpleGUI::GetFont();
				font(m_text).drawAt(m_rect.center(), ButtonTextColor);
			}

			[[nodiscard]]
			const String& text() const
			{
				return m_text;
			}

			[[nodiscard]]
			bool isClicked() const
			{
				return m_isClicked;
			}

			[[nodiscard]]
			bool interactable() const
			{
				return m_interactable;
			}

			void setInteractable(bool interactable)
			{
				m_interactable = interactable;
			}
		};

		class SimpleDialogSequence : public UpdaterSequenceBase<String>
		{
		private:
			String m_text;
			Array<SimpleButton> m_buttons;
			Tweener m_tweener;
			int32 m_drawIndex;

			void update() override
			{
				for (auto& button : m_buttons)
				{
					button.update();
				}
				for (const auto& button : m_buttons)
				{
					if (button.isClicked())
					{
						requestFinish(button.text());
						break;
					}
				}
			}

			void draw() const override
			{
				const Transformer2D m_tr{ Mat3x2::Identity(), Mat3x2::Identity(), Transformer2D::Target::SetLocal };

				// メッセージボックスの背景
				Scene::Rect().draw(ColorF{ 0.0, m_tweener.alpha() * 0.5 });

				// メッセージボックス本体を上下に分けて描画
				const auto scopedTween = m_tweener.applyScoped();
				const auto boxPos = BoxPos();
				RectF{ boxPos, { SimpleDialogSize.x, SimpleDialogSize.y - FooterOffsetY } }.rounded(SimpleDialogRoundSize, SimpleDialogRoundSize, 0, 0).draw(BackgroundColor);
				RectF{ boxPos + Vec2{ 0, SimpleDialogSize.y - FooterOffsetY }, { SimpleDialogSize.x, FooterOffsetY } }.rounded(0, 0, SimpleDialogRoundSize, SimpleDialogRoundSize).draw(BackgroundColorFooter);

				// メッセージ
				const auto& font = SimpleGUI::GetFont();
				font(m_text).drawAt(boxPos + (SimpleDialogSize - Vec2{ 0, FooterOffsetY }) / 2, TextColor);

				// ボタン
				for (const auto& button : m_buttons)
				{
					button.draw();
				}
			}

			[[nodiscard]]
			Task<void> fadeIn() override
			{
				co_await All(
					m_tweener.fadeInAlpha(FadeDuration).play(),
					m_tweener.tweenScale(FadeDuration).fromTo(FadeInScaleFrom, FadeInScaleTo).play());
				setButtonInteractable(true);
			}

			[[nodiscard]]
			Task<void> fadeOut() override
			{
				setButtonInteractable(false);
				co_await All(
					m_tweener.fadeOutAlpha(FadeDuration).play(),
					m_tweener.tweenScale(FadeDuration).fromTo(FadeInScaleTo, FadeInScaleFrom).play());
			}

			void setButtonInteractable(bool interactable)
			{
				for (auto& button : m_buttons)
				{
					button.setInteractable(interactable);
				}
			}

			[[nodiscard]]
			static Vec2 BoxPos()
			{
				return Scene::Center() - SimpleDialogSize / 2;
			}

			[[nodiscard]]
			static RectF ButtonRect(std::size_t index, std::size_t numButtons)
			{
				const auto boxPos = BoxPos();
				const auto buttonPos = boxPos + Vec2{ SimpleDialogSize.x / 2, SimpleDialogSize.y - ButtonOffsetY } + Vec2{ (ButtonSize.x + ButtonMargin / 2) * (index - static_cast<double>(numButtons) / 2), 0 };
				return RectF{ buttonPos, ButtonSize };
			}

			[[nodiscard]]
			static Array<SimpleButton> CreateButtons(const Array<String>& buttonTexts)
			{
				const std::size_t numButtons = buttonTexts.size();
				Array<SimpleButton> buttons;
				buttons.reserve(numButtons);
				for (std::size_t i = 0; i < numButtons; ++i)
				{
					buttons.emplace_back(buttonTexts[i], ButtonRect(i, numButtons), false);
				}
				return buttons;
			}

		public:
			explicit SimpleDialogSequence(StringView text, const Array<String>& buttonTexts, Layer layer, int32 drawIndex)
				: UpdaterSequenceBase<String>(layer, drawIndex)
				, m_text(text)
				, m_buttons(CreateButtons(buttonTexts))
			{
			}
		};
	}

	[[nodiscard]]
	inline Task<String> SimpleDialog(StringView text, const Array<String>& buttonTexts, Layer layer = Layer::Modal, int32 drawIndex = DrawIndex::Default)
	{
		return Play<detail::SimpleDialogSequence>(text, buttonTexts, layer, drawIndex);
	}

	[[nodiscard]]
	inline Task<void> SimpleDialog(StringView text, Layer layer = Layer::Modal, int32 drawIndex = DrawIndex::Default)
	{
		return SimpleDialog(text, { U"OK" }, layer, drawIndex).discardResult();
	}
}

#ifndef NO_COTASKLIB_USING
using namespace cotasklib;
#endif
