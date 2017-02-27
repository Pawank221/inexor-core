
#pragma once

#include "inexor/rpc/SharedOptions.hpp"
#include "inexor/rpc/SharedVar.hpp"
#include "inexor/rpc/SharedClass.hpp"


struct sharedclasstest : SharedClass
{
    SharedVar<int> test;

    void setonchangefunctions(int id)
    {
        //test.connect([id](int oldvalue, int newvalue)
        //{
        //    spdlog::get("global")->debug("init: ENet {}", id, value);
        //});
    }
    sharedclasstest() : test(15) {}

    // TODO we should fire on = operator ein: adde + remove event f�rs parent oder onchange f�r jeden member! (Achtung overhead)
    // vllt gibts schon iwie ne anleitung standardlisten zu modifizieren?
};

struct InternalSharedListMarker : SharedOption
{
    InternalSharedListMarker() {}
    const char *is_shared_list = "true";
};

template<typename T>
struct SharedList : SharedClass
{
    int len = 0;
    std::function<void(int, sharedclasstest &)> element_pushed_back_func;
    std::function<void(int)> element_removed_func;
    std::vector<T> container;

    SharedList() : SharedClass(InternalSharedListMarker()) {}

    void push_back(T s) // + &s + &&s hat beides
    {
        int id = ++len;
        container.push_back(std::move(s));
        element_pushed_back_func(id, container.back());
    }
    // fehlt: emplace emplace back assign, insert, swap, erase

    void erase(int i)
    {
        if(i >= container.size() || i < 0) return;
        container.erase(container.begin() + i);
        element_removed_func(i);
    }

    T& operator[](size_t n)
    {

    }
};

