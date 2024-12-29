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
#include <Siv3D.hpp>

namespace cotasklib::Co::detail
{
	// 削除の実行効率を重視したflat_map
	template <typename KeyType, typename T, bool IsMonotonic = false>
	class FlatHiveMap
	{
	public:
		using pair_type = std::pair<KeyType, std::optional<T>>;
		using container_type = Array<pair_type>;

	private:
		container_type m_data;
		size_t m_nulloptCount = 0;

		template <bool>
		friend class FilteredIterator;

	public:
		template <bool IsConst>
		class FilteredIterator
		{
		public:
			using iterator_type = std::conditional_t<IsConst,
				typename container_type::const_iterator,
				typename container_type::iterator>;

			using ContainerPtr = std::conditional_t<IsConst,
				const FlatHiveMap<KeyType, T, IsMonotonic>*,
				FlatHiveMap<KeyType, T, IsMonotonic>*>;

		private:
			ContainerPtr m_containerPtr;
			size_t m_index;

			void skipNullopt()
			{
				while (m_index < m_containerPtr->m_data.size() && !m_containerPtr->m_data[m_index].second.has_value())
				{
					++m_index;
				}
			}

		public:
			using value_type = std::conditional_t<IsConst, const T, T>;
			using reference = std::conditional_t<IsConst, const T&, T&>;
			using pointer = std::conditional_t<IsConst, const T*, T*>;
			using difference_type = std::ptrdiff_t;
			using iterator_category = std::forward_iterator_tag; // 逆方向は実装を省略

			explicit FilteredIterator(ContainerPtr containerPtr, size_t index)
				: m_containerPtr(containerPtr)
				, m_index(index)
			{
				skipNullopt();
			}

			reference operator*() const
			{
				return *(m_containerPtr->m_data[m_index].second);
			}

			pointer operator->() const
			{
				return &*(m_containerPtr->m_data[m_index].second);
			}

			// 前置インクリメント
			FilteredIterator& operator++()
			{
				++m_index;
				skipNullopt();
				return *this;
			}

			// 後置インクリメント
			FilteredIterator operator++(int)
			{
				FilteredIterator tmp = *this;
				++(*this);
				return tmp;
			}

			bool operator==(const FilteredIterator& other) const
			{
				if (m_containerPtr != other.m_containerPtr)
				{
					throw Error{ U"FlatHiveMap: Cannot compare iterators of different containers" };
				}
				return m_index == other.m_index;
			}

			bool operator!=(const FilteredIterator& other) const
			{
				if (m_containerPtr != other.m_containerPtr)
				{
					throw Error{ U"FlatHiveMap: Cannot compare iterators of different containers" };
				}
				return m_index != other.m_index;
			}

			size_t index() const
			{
				return m_index;
			}

			const KeyType& key() const
			{
				return m_containerPtr->m_data[m_index].first;
			}
		};
		using iterator = FilteredIterator<false>;
		using const_iterator = FilteredIterator<true>;

		FlatHiveMap() = default;
		FlatHiveMap(const FlatHiveMap&) = default;
		FlatHiveMap& operator=(const FlatHiveMap&) = default;
		FlatHiveMap(FlatHiveMap&&) = default;
		FlatHiveMap& operator=(FlatHiveMap&&) = default;

		T& at(const KeyType& key)
		{
			const auto it = std::lower_bound(m_data.begin(), m_data.end(), key,
				[](const pair_type& p, const KeyType& key) { return p.first < key; });
			if (it != m_data.end() && it->first == key && it->second.has_value())
			{
				return *(it->second);
			}
			throw Error{ U"FlatHiveMap: Key not found" };
		}

		const T& at(const KeyType& key) const
		{
			const auto it = std::lower_bound(m_data.begin(), m_data.end(), key,
				[](const pair_type& p, const KeyType& key) { return p.first < key; });
			if (it != m_data.end() && it->first == key && it->second.has_value())
			{
				return *(it->second);
			}
			throw Error{ U"FlatHiveMap: Key not found" };
		}

		void emplace(const KeyType& key, const T& value)
		{
			if constexpr (IsMonotonic)
			{
				if (!m_data.empty())
				{
					const KeyType& lastKey = m_data.back().first;
					if (lastKey >= key)
					{
						// キーが昇順で挿入されていない場合は例外を投げる
						throw Error{ U"FlatHiveMap: Keys must be inserted in increasing order" };
					}
				}
				m_data.emplace_back(pair_type{ key, std::make_optional(value) });
			}
			else
			{
				const auto it = std::lower_bound(m_data.begin(), m_data.end(), key,
					[](const pair_type& p, const KeyType& key) { return p.first < key; });
				if (it != m_data.end() && it->first == key)
				{
					if (!it->second.has_value())
					{
						--m_nulloptCount;
					}
					it->second = value;
				}
				else
				{
					m_data.emplace(it, pair_type{ key, std::make_optional(value) });
				}
			}
		}

		void emplace(const KeyType& key, T&& value)
		{
			if constexpr (IsMonotonic)
			{
				if (!m_data.empty())
				{
					const KeyType& lastKey = m_data.back().first;
					if (lastKey >= key)
					{
						// キーが昇順で挿入されていない場合は例外を投げる
						throw Error{ U"FlatHiveMap: Keys must be inserted in increasing order" };
					}
				}
				m_data.emplace_back(pair_type{ key, std::make_optional(std::move(value)) });
			}
			else
			{
				const auto it = std::lower_bound(m_data.begin(), m_data.end(), key,
					[](const pair_type& p, const KeyType& key) { return p.first < key; });
				if (it != m_data.end() && it->first == key)
				{
					if (!it->second.has_value())
					{
						--m_nulloptCount;
					}
					it->second = std::move(value);
				}
				else
				{
					m_data.emplace(it, pair_type{ key, std::make_optional(std::move(value)) });
				}
			}
		}

		void erase(const KeyType& key)
		{
			const auto it = std::lower_bound(m_data.begin(), m_data.end(), key,
				[](const pair_type& p, const KeyType& key) { return p.first < key; });
			if (it != m_data.end() && it->first == key)
			{
				if (it->second.has_value())
				{
					it->second = std::nullopt;
					++m_nulloptCount;
				}
			}
		}

		iterator erase(iterator it)
		{
			if (it == end())
			{
				return it;
			}
			if (m_data[it.index()].second.has_value())
			{
				m_data[it.index()].second = std::nullopt;
				++m_nulloptCount;
			}
			return ++it;
		}

		iterator find(const KeyType& key)
		{
			const auto it = std::lower_bound(m_data.begin(), m_data.end(), key,
				[](const pair_type& p, const KeyType& key) { return p.first < key; });
			if (it != m_data.end() && it->first == key && it->second.has_value())
			{
				return iterator{ this, static_cast<size_t>(std::distance(m_data.begin(), it)) };
			}
			return end();
		}

		const_iterator find(const KeyType& key) const
		{
			const auto it = std::lower_bound(m_data.begin(), m_data.end(), key,
				[](const pair_type& p, const KeyType& key) { return p.first < key; });
			if (it != m_data.end() && it->first == key && it->second.has_value())
			{
				return const_iterator{ this, static_cast<size_t>(std::distance(m_data.begin(), it)) };
			}
			return end();
		}

		void clear()
		{
			m_data.clear();
			m_nulloptCount = 0;
		}

		void compact()
		{
			m_data.erase(
				std::remove_if(m_data.begin(), m_data.end(),
					[](const pair_type& p) { return !p.second.has_value(); }),
					m_data.end());
			m_nulloptCount = 0;
		}

		void reserve(size_t newCapacity)
		{
			m_data.reserve(newCapacity);
		}

		iterator begin()
		{
			return iterator{ this, 0 };
		}

		iterator end()
		{
			return iterator{ this, m_data.size() };
		}

		const_iterator begin() const
		{
			return const_iterator{ this, 0 };
		}

		const_iterator end() const
		{
			return const_iterator{ this, m_data.size() };
		}

		const_iterator cbegin() const
		{
			return const_iterator{ this, 0 };
		}

		const_iterator cend() const
		{
			return const_iterator{ this, m_data.size() };
		}

		std::size_t size() const
		{
			return m_data.size() - m_nulloptCount;
		}

		bool empty() const
		{
			return size() == 0;
		}

		size_t nulloptCount() const
		{
			return m_nulloptCount;
		}
	};
}
