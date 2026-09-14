/*
 * Nova Toolbox — UDP bind 失败：errno → 文案 查表（纯 C++，不依赖 LVGL/IDF）
 *
 * 为什么单拎出来：
 *   1. 文案必须走查表（EADDRINUSE / EACCES / EADDRNOTAVAIL + 兜底），不是 if 链 ——
 *      加一种 errno 只加一行，不动逻辑；
 *   2. 同一张表有两个使用方：固件（tool_udp.cpp）+ 上位机自测
 *      （tools/udp_bind_errno_check.cpp，真实 syscall 造错，跑的是本文件，不是副本）。
 *      所以本文件禁止 include 任何 LVGL/IDF 头，只用 <cerrno>/<cstdio>/<cstddef>。
 *
 * 【坑】日志里必须同时打 errno 数值和符号名，不能只打数值：
 *   数值随 C 库变 —— 同一个"端口被占"，ESP32(newlib) 是 112，Linux(glibc) 是 98。
 *   跨平台可比的只有符号名（EADDRINUSE）。查表也用宏，绝不用字面量。
 *   （设备端来源：lwip err_to_errno(ERR_USE) → EADDRINUSE，取的就是 <errno.h> 的宏值。）
 */
#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdio>

namespace udp_bind_err {

struct Entry {
    int         err;     // errno 值（宏，不写字面量）
    const char* name;    // 符号名：日志与自测比对用（跨平台可比）
    const char* reason;  // 屏上：发生了什么
    const char* next;    // 屏上：下一步做什么（只说"失败"等于让人瞎猜）
};

// 表 —— 只加行，不加分支
inline const Entry* table(size_t* count = nullptr)
{
    static const Entry kTable[] = {
        {EADDRINUSE,    "EADDRINUSE",    "端口已被占用",     "点「本机端口」换一个再试"},
        {EACCES,        "EACCES",        "没有权限绑这个端口", "换 ≥1024 的端口再试"},
        {EADDRNOTAVAIL, "EADDRNOTAVAIL", "本机没有这个地址",  "先连上 WiFi/AP 再试"},
    };
    if (count != nullptr) {
        *count = sizeof(kTable) / sizeof(kTable[0]);
    }
    return kTable;
}

// 命中返回条目；未命中返回 nullptr —— 由调用方走兜底文案，不要在这里假装认识
inline const Entry* find(int err)
{
    size_t       n = 0;
    const Entry* t = table(&n);
    for (size_t i = 0; i < n; i++) {
        if (t[i].err == err) {
            return &t[i];
        }
    }
    return nullptr;
}

// 符号名（未命中 → "EUNKNOWN"，不猜、不编一个像样的名字）
inline const char* name_of(int err)
{
    const Entry* e = find(err);
    return (e != nullptr) ? e->name : "EUNKNOWN";
}

// ---- 启动链停在哪一步 --------------------------------------------------
// socket 建不出来 / bind 失败 / 收包任务起不来，是三件不同的事。
// 合成一个「没监听」布尔值，就会把前两者显示成「已停止」——用户以为没点着。
enum class Step { None = 0, Socket, Bind, Task };

// 状态行文案（纯函数，便于上位机自测：文案算错是回归，不是审美）。
// live 优先：真在监听就是「监听中」，别拿陈旧的失败态盖住它。
inline void status_line(char* out, size_t out_len, bool live, Step step, unsigned port)
{
    if (live) {
        snprintf(out, out_len, "监听中 :%u", port);
        return;
    }
    switch (step) {
        case Step::Bind:
            snprintf(out, out_len, "绑定失败 :%u", port);
            break;
        case Step::Socket:
        case Step::Task:
            snprintf(out, out_len, "启动失败 :%u", port);
            break;
        default:
            snprintf(out, out_len, "已停止");
            break;
    }
}

// 屏上一行文案：回显真实 addr:port（只说"绑定失败"等于没说），带下一步。
// 超长由 snprintf 截断 —— 宁可少几个字，也不许撑破报文区。
inline void format_line(char* out, size_t out_len, int err, const char* addr, unsigned port)
{
    const Entry* e = find(err);
    if (e != nullptr) {
        snprintf(out, out_len, "[bind 失败] %s:%u %s（%s）→ %s",
                 (addr != nullptr) ? addr : "?", port, e->reason, e->name, e->next);
    } else {
        snprintf(out, out_len, "[bind 失败] %s:%u 未识别错误（errno=%d）→ 换个端口再试一次",
                 (addr != nullptr) ? addr : "?", port, err);
    }
}

}  // namespace udp_bind_err
