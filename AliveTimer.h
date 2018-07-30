/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 */

#ifndef __TZHTTPD_ALIVE_TIMER_H__
#define __TZHTTPD_ALIVE_TIMER_H__

#include <xtra_rhel6.h>

#include <set>
#include <boost/unordered_map.hpp> // C++11

#include "Log.h"

namespace tzhttpd {

// 其实支持使用了Operation Cancel机制之后，这里的AliveTimer显得有些多余了；
// 而且特殊情况：Operation Cancel在定时中还持有智能指针，但是AliveTimer这边已经将其注销了，然后在
//              连接真正析构的时候，再注册AliveTimer就会发现该连接找不到了

template<typename T>
class AliveItem {
public:
    AliveItem(time_t tm, std::shared_ptr<T> t_ptr):
        time_(tm), raw_ptr_(t_ptr.get()), weak_ptr_(t_ptr) {
    }

    time_t get_expire_time() {
        return time_;
    }

    void set_expire_time(time_t t) {
        time_ = t;
    }

    T* get_raw_ptr() {
        return raw_ptr_;
    }

    std::weak_ptr<T> get_weak_ptr() {
        return weak_ptr_;
    }

private:
    time_t time_;
    T*     raw_ptr_;
    std::weak_ptr<T> weak_ptr_;
};

template<typename T>
class AliveTimer {
public:
    typedef std::shared_ptr<AliveItem<T> >               active_item_ptr;     // internel use
    typedef std::map<time_t, std::set<active_item_ptr> > TimeContainer;       //
    typedef std::map<T*, active_item_ptr >               BucketContainer;     //
    typedef std::set<T* >                                DropContainer;       // 主动删除
    typedef std::function<int(std::shared_ptr<T>)>       ExpiredHandler;

public:

    explicit AliveTimer(std::string alive_name, time_t time_out = 10*60, time_t time_linger = 30):
        alive_name_(alive_name),
        lock_(), time_items_(), bucket_items_(), func_(),
        time_out_(time_out), time_linger_(time_linger) {
        initialized_ = false;
    }

     explicit AliveTimer(std::string alive_name, ExpiredHandler func, time_t time_out = 10*60, time_t time_linger = 30):
        alive_name_(alive_name),
        lock_(), time_items_(), bucket_items_(), func_(func),
        time_out_(time_out), time_linger_(time_linger) {

        initialized_ = true;
    }

    bool init(ExpiredHandler func) {
        if (initialized_) {
            tzhttpd_log_err("AliveTimer %s already initialized so we will skip, please check!", alive_name_.c_str());
            return true;
        }

        func_ = func;
        initialized_ = true;

        return true;
    }

    bool init(ExpiredHandler func, time_t time_out, time_t time_linger) {
        if (initialized_) {
            tzhttpd_log_err("AliveTimer %s already initialized so we will skip, please check!", alive_name_.c_str());
            return true;
        }

        func_ = func;
        time_out_ = time_out;
        time_linger_ = time_linger;
        initialized_ = true;

        return true;
    }

    ~AliveTimer(){}

    bool TOUCH(std::shared_ptr<T> ptr) {
        time_t tm = ::time(NULL) + time_out_;
        return TOUCH(ptr, tm);
    }

    bool TOUCH(std::shared_ptr<T> ptr, time_t tm) {

        std::lock_guard<std::mutex> lock(lock_);

        typename BucketContainer::iterator iter = bucket_items_.find(ptr.get());
        if (iter == bucket_items_.end()) {
            tzhttpd_log_err("bucket item %p not found!", ptr.get());
            SAFE_ASSERT(false);
            return false;
        }

        time_t before = iter->second->get_expire_time();
        if (tm - before < time_linger_){
            tzhttpd_log_debug("item %p linger optimize: %ld - %ld = %ld", ptr.get(), tm, before, tm - before);
            return true;
        }

        if (time_items_.find(before) == time_items_.end()){
            tzhttpd_log_err("time slot: %ld not found, critical error!", before);
            SAFE_ASSERT(false);
            return false;
        }

        if (time_items_.find(tm) == time_items_.end()) {
            time_items_[tm] = std::set<active_item_ptr>();  // create new time bucket
        }

        // 更新原object
        iter->second->set_expire_time(tm);
        time_items_[tm].insert(iter->second);
        time_items_[before].erase(iter->second);
        tzhttpd_log_debug("touched item %p, from %ld -> %ld", ptr.get(), before, tm);
        return true;
    }

    bool INSERT(std::shared_ptr<T> ptr) {
        time_t tm = ::time(NULL) + time_out_;
        return INSERT(ptr, tm);
    }

    bool INSERT(std::shared_ptr<T> ptr, time_t tm ) {
        std::lock_guard<std::mutex> lock(lock_);
        typename BucketContainer::iterator iter = bucket_items_.find(ptr.get());
        if (iter != bucket_items_.end()) {
            tzhttpd_log_err("insert item %p already exists at time slot %ld",
                    iter->second->get_raw_ptr(), iter->second->get_expire_time());
            return false;
        }

        active_item_ptr alive_item = std::make_shared<AliveItem<T> >(tm, ptr);
        if (!alive_item){
            tzhttpd_log_err("create AliveItem object failed!");
            return false;
        }

        bucket_items_[ptr.get()] = alive_item;

        if (time_items_.find(tm) == time_items_.end()) {
            time_items_[tm] = std::set<active_item_ptr>();
        }
        time_items_[tm].insert(alive_item);

        tzhttpd_log_debug("inserted item %p, time slot %ld", ptr.get(), tm);
        return true;
    }

    bool DROP(std::shared_ptr<T> ptr) { // 此处肯定是shared_ptr的
        std::lock_guard<std::mutex> drop_lock(drop_lock_);

        auto result = drop_items_.insert(ptr.get());  // store raw_ptr
        return result.second;
    }

    bool DROP(T* ptr) {
        std::lock_guard<std::mutex> drop_lock(drop_lock_);

        auto result = drop_items_.insert(ptr);  // store raw_ptr
        return result.second;
    }

    // 被管理池绑定使用，一般是定时周期执行
    bool clean_up() {

        if (!initialized_) {
            tzhttpd_log_err("AliveTimer not initialized, please check ...");
            return false;
        }


        struct timeval checked_start;
        ::gettimeofday(&checked_start, NULL);

        active_remove_conns();

        struct timeval checked_active_now;
        ::gettimeofday(&checked_active_now, NULL);
        int64_t active_elapse_ms = ( 1000000 * ( checked_active_now.tv_sec - checked_start.tv_sec ) +
                                                 checked_active_now.tv_usec - checked_start.tv_usec ) / 1000;
        if (active_elapse_ms > 5) {
            tzhttpd_log_err("check active works too long elapse time: %ld ms, break now", active_elapse_ms);
            return true;
        }


        std::lock_guard<std::mutex> lock(lock_);

        ::gettimeofday(&checked_start, NULL);
        int checked_count = 0;
        time_t current_sec = ::time(NULL);

        typename TimeContainer::iterator iter = time_items_.begin();
        typename TimeContainer::iterator remove_iter = time_items_.end();
        for ( ; iter != time_items_.end(); ){
            if (iter->first < current_sec) {
                typename std::set<active_item_ptr>::iterator it = iter->second.begin();
                for (; it != iter->second.end(); ++it) {
                    T* p = (*it)->get_raw_ptr();
                    if (bucket_items_.find(p) == bucket_items_.end()) {
                        tzhttpd_log_err("bucket item %p not found, critical error!", p);
                        SAFE_ASSERT(false);
                    }

                    tzhttpd_log_debug("bucket item remove: %p, time slot %ld", p, (*it)->get_expire_time());
                    bucket_items_.erase(p);
                    std::weak_ptr<T> weak_item = (*it)->get_weak_ptr();
                    if (std::shared_ptr<T> ptr = weak_item.lock()) {
                        if (func_) {
                           func_(ptr);
                        }
                    } else {
                        tzhttpd_log_debug("item %p may already release before ...", p);
                    }
                }

                // (Old style) References and iterators to the erased elements are invalidated.
                // Other references and iterators are not affected.

                tzhttpd_log_debug("expire entry remove: %ld, now:%ld, diff:%ld, count:%ld",
                                  iter->first, current_sec, current_sec - iter->first, iter->second.size());
                remove_iter = iter ++;
                time_items_.erase(remove_iter);
            }
            else {
                       // time_t 是已经排序了的
                break; // all expired clean
            }

            ++ checked_count;
            if ((checked_count % 10) == 0) {  // 不能卡顿太长时间，否则正常的请求会被卡死
                struct timeval checked_now;
                ::gettimeofday(&checked_now, NULL);
                int64_t elapse_ms = ( 1000000 * ( checked_now.tv_sec - checked_start.tv_sec ) +
                                                  checked_now.tv_usec - checked_start.tv_usec ) / 1000;
                if (elapse_ms > 5) {
                    tzhttpd_log_err("check works too long elapse time: %ld ms, break now", elapse_ms);
                    break;
                }
            }
        }

        size_t total_count = 0;
        for (iter = time_items_.begin() ; iter != time_items_.end(); ++ iter) {
            total_count += iter->second.size();
        }

        if (total_count != 0 || bucket_items_.size() != 0) {
            tzhttpd_log_debug("current alived hashed count:%ld, timed_count: %ld, need check timed_bucket: %d",
                            bucket_items_.size(), total_count, static_cast<int>(time_items_.size()));
        }

        if (bucket_items_.size() != total_count) {
            tzhttpd_log_err("mismatch item count, bug count:%ld, timed_count: %ld", bucket_items_.size(), total_count);
            SAFE_ASSERT(false);
        }

        return true;
    }

private:

    // 这里的删除和HttpServer的删除是异步的，所以如果遇到还存在的连接，
    // 将其忽略，由超时机制再去处理
    void active_remove_item(T* p) {

        active_item_ptr active_item;

        do {
            auto bucket_iter = bucket_items_.find(p);
            if (bucket_iter == bucket_items_.end()) {
                tzhttpd_log_notice("bucket item %p not found, may already try released before!", p);
                return;
            }

            active_item = bucket_iter->second;
            if (std::shared_ptr<T> ptr = active_item->get_weak_ptr().lock()) { // bad!!!
                tzhttpd_log_notice("bucket item %p still alive, leave alone with it", p);
                return;
            }

            auto time_iter = time_items_.find(active_item->get_expire_time());
            if (time_iter == time_items_.end()) {
                tzhttpd_log_err("time slot %ld not found, critical error!", active_item->get_expire_time());
                SAFE_ASSERT(false);
                bucket_items_.erase(bucket_iter);  // remove it anyway
                break;
            }

            std::set<active_item_ptr>& time_set = time_iter->second;
            auto time_item_iter = time_set.find(active_item);
            if (time_item_iter == time_set.end()) {
                tzhttpd_log_err("time item not found, critical error!");
                SAFE_ASSERT(false);
                bucket_items_.erase(bucket_iter);
                break;
            }

            tzhttpd_log_debug("bucket item remove: %p, time slot %ld", p, active_item->get_expire_time());
            bucket_items_.erase(bucket_iter);
            time_set.erase(time_item_iter);

        } while (0);

        auto weak_real = active_item->get_weak_ptr();
        if (std::shared_ptr<T> ptr = weak_real.lock()) { // bad!!!
            tzhttpd_log_err("active remove item should not shared, bug...");
            SAFE_ASSERT(false);
            func_(ptr);
        }
    }

    // 主动处理已经废弃的连接
    int active_remove_conns() {

        std::lock_guard<std::mutex> drop_lock(drop_lock_);

        if (drop_items_.empty()) {
            return 0;
        }

        std::lock_guard<std::mutex> lock(lock_);

        // 在高并发的情况下会占用大量的时间，导致实时交易延迟而不能退出
        // 后续优化之
        std::for_each(drop_items_.begin(), drop_items_.end(),
                      std::bind(&AliveTimer::active_remove_item, this, std::placeholders::_1));

        int count = static_cast<int>(drop_items_.size());
        drop_items_.clear();

        tzhttpd_log_debug("total active remove %d items!", count);
        return count;
    }

private:
    bool initialized_;
    std::string     alive_name_;
    mutable std::mutex lock_;
    TimeContainer   time_items_;
    BucketContainer bucket_items_;
    ExpiredHandler  func_;

    mutable std::mutex drop_lock_;
    DropContainer   drop_items_;

    time_t time_out_;
    time_t time_linger_;
};

} // end namespace tzhttpd

#endif // __TZHTTPD_ALIVE_TIMER_H__

