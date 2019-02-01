/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_KVVEC_H__
#define __TZHTTPD_KVVEC_H__

// 不同于map的key value，使用vector保持推入的顺序

#include <vector>
#include <memory>
#include <mutex>

#include <utility>
#include <functional>

namespace tzhttpd {

template<typename K, typename V>
class KVVec {
public:
    typedef std::pair<K, V> Entry;
    typedef std::vector<Entry> Container;
    typedef typename Container::iterator iterator;
    typedef typename Container::const_iterator const_iterator;

public:
    KVVec():
        lock_(new std::mutex()), items_() {
    }

    ~KVVec() {
    }

public:

    // so called iterator delegation...
    iterator BEGIN() {
        return items_.begin();
    }

    iterator END() {
        return items_.end();
    }

    void PUSH_BACK(const K& k, const V& v) {
        std::lock_guard<std::mutex> lock(*lock_);
        items_.push_back({k, v}); // make_pair can not use - const ref
    }

    void PUSH_BACK(const Entry& entry) {
        std::lock_guard<std::mutex> lock(*lock_);
        items_.push_back(entry);
    }

    bool EXIST(const K& k) const {
        std::lock_guard<std::mutex> lock(*lock_);
        for (size_t idx = 0; idx < items_.size(); ++idx) {
            if (items_[idx].first == k) {
                return true;
            }
        }
        return false;
    }

    bool FIND(const K& k, V& v) const {
        std::lock_guard<std::mutex> lock(*lock_);
        for (size_t idx = 0; idx < items_.size(); ++idx) {
            if (items_[idx].first == k) {
                v = items_[idx].second;
                return true;
            }
        }
        return false;
    }

    bool FIND(const K& k, Entry& entry) const {
        std::lock_guard<std::mutex> lock(*lock_);
        for (size_t idx = 0; idx < items_.size(); ++idx) {
            if (items_[idx].first == k) {
                entry = items_[idx];
                return true;
            }
        }
        return false;
    }

    V VALUE(const K& k) const {
        std::lock_guard<std::mutex> lock(*lock_);

        V v {}; // default value
        for (size_t idx = 0; idx < items_.size(); ++idx) {
            if (items_[idx].first == k) {
                return items_[idx].second;
            }
        }
        return v;
    }

    size_t SIZE() const {
        std::lock_guard<std::mutex> lock(*lock_);
        return items_.size();
    }

    bool EMPTY() const {
        std::lock_guard<std::mutex> lock(*lock_);
        return items_.empty();
    }

    void CLEAR() {
        std::lock_guard<std::mutex> lock(*lock_);
        items_.clear();
    }

    std::mutex& LOCK() {
        return *lock_;
    }

    // 简单的json序列化，暂时不引入json库
    std::string SERIALIZE() const {
        std::lock_guard<std::mutex> lock(*lock_);

        std::stringstream ss;
        ss << "{";
        for (size_t idx = 0; idx < items_.size(); ++idx) {
            if (idx != 0) {
                ss << ",";
            }
            ss << "\"" << items_[idx].first << "\"" << ":";
            ss << "\"" << items_[idx].second << "\"";
        }
        ss << "}";

        return ss.str();
    }

private:
    std::unique_ptr<std::mutex>  lock_;
    std::vector<std::pair<K, V> > items_;
};

} // end namespace tzhttpd

#endif // __TZHTTPD_KVVEC_H__

