#pragma once
#include <vector>

namespace Util {

// 预分配容器
template<typename T>
class PreallocVector {
    std::vector<T> m_data;
public:
    PreallocVector(size_t cap = 1024) { m_data.reserve(cap); }
    void Clear() { m_data.clear(); }
    void ShrinkIfNeeded() {
        // 优化：更保守的缩容策略，避免频繁缩容-扩容震荡
        if (m_data.capacity() > 512 && m_data.size() < m_data.capacity() / 8) {
            m_data.shrink_to_fit();
            m_data.reserve(std::max<size_t>(256, m_data.size() * 2));
        }
    }
    size_t Size() const { return m_data.size(); }
    bool Empty() const { return m_data.empty(); }
    T& operator[](size_t i) { return m_data[i]; }
    const T& operator[](size_t i) const { return m_data[i]; }
    auto begin() { return m_data.begin(); }
    auto end() { return m_data.end(); }
    auto begin() const { return m_data.begin(); }
    auto end() const { return m_data.end(); }
    std::vector<T>& GetVector() { return m_data; }
    const std::vector<T>& GetVector() const { return m_data; }
};

} // namespace Util
