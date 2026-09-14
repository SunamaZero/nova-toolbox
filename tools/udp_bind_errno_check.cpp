/*
 * UDP bind 失败文案 —— 上位机自测（真 syscall 造错，跑的是固件同一张表）
 *
 * 为什么要有它：
 *   验收要求"文案 = errno → 可比对字符串"。屏上截图没法回归重跑，
 *   所以把同一份查表（app/apps/app_launcher/view/tool_udp_bind_err.h）
 *   在 PC 上用真实失败场景跑一遍：造出错的是内核，判对错的是本程序。
 *
 * 编译运行（零依赖，任意 g++）：
 *   g++ -std=c++17 -Wall -Wextra -o /tmp/udp_bind_errno_check tools/udp_bind_errno_check.cpp && /tmp/udp_bind_errno_check
 *
 * 退出码：0 = 全部符合预期；1 = 有不符合
 *
 * 覆盖：
 *   1) EADDRINUSE    —— 同一端口 bind 两次（第二次必失败）
 *   2) EACCES        —— bind <1024 特权端口（非 root 时）
 *   3) EADDRNOTAVAIL —— bind 本机没有的地址
 *   4) 兜底文案      —— 编一个不认识的 errno，必须走兜底而不是假装认识
 *   5) fd 不泄漏     —— 失败后 close 的路径 fd 计数回到基线；
 *                       配一组"故意不 close"的反向对照，证明探针真能发现泄漏
 *                       （探针抓不出反例 = 没测）
 *
 * 已知差异（不是 bug）：errno 数值随 C 库变（设备 newlib 112 / Linux glibc 98），
 *   所以判据只用**符号名**；数值打印出来只是给你看差异。
 */
#include "../app/apps/app_launcher/view/tool_udp_bind_err.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

static int g_fail = 0;

static void check(bool ok, const char* what)
{
    printf("  %s %s\n", ok ? "[ok]  " : "[FAIL]", what);
    if (!ok) {
        g_fail++;
    }
}

// 数一下本进程当前打开的 fd（Linux：/proc/self/fd）
static int fd_count()
{
    DIR* d = opendir("/proc/self/fd");
    if (d == nullptr) {
        return -1;
    }
    int n = 0;
    while (readdir(d) != nullptr) {
        n++;
    }
    closedir(d);
    return n;
}

// 与产品 setListening(true) 同一套动作：socket → SO_BROADCAST → bind addr:port
// 不走这个函数做 fd 探针（失败时 fd 不外露），探针在 main 里手写同一套动作。
static int udp_bind(const char* ip, uint16_t port, int* err_out)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        *err_out = errno;
        return -1;
    }
    int bcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons(port);
    inet_pton(AF_INET, (ip != nullptr) ? ip : "0.0.0.0", &a.sin_addr);
    if (bind(sock, (struct sockaddr*)&a, sizeof(a)) < 0) {
        *err_out = errno;   // 【坑】先抓 errno：close() 会把它改掉
        return -1;
    }
    *err_out = 0;
    return sock;
}

static void show(int err, const char* addr, unsigned port)
{
    char line[160];
    udp_bind_err::format_line(line, sizeof(line), err, addr, port);
    printf("        日志：errno=%d(%s)   屏上：%s\n", err, udp_bind_err::name_of(err), line);
}

int main()
{
    printf("== 1. 查表内容（固件与自测共用同一份头文件）==\n");
    size_t                     n = 0;
    const udp_bind_err::Entry* t = udp_bind_err::table(&n);
    for (size_t i = 0; i < n; i++) {
        printf("  %-14s = %-3d  屏上：%s → %s\n", t[i].name, t[i].err, t[i].reason, t[i].next);
    }
    check(n == 3, "表里正好三条（EADDRINUSE / EACCES / EADDRNOTAVAIL），兜底不在表内");

    printf("\n== 2. EADDRINUSE：同一端口 bind 两次 ==\n");
    {
        const uint16_t port = 45888;
        int            e1 = 0, e2 = 0;
        int            s1 = udp_bind("0.0.0.0", port, &e1);
        check(s1 >= 0 && e1 == 0, "第一次 bind 成功（端口空闲）");
        int s2 = udp_bind("0.0.0.0", port, &e2);
        check(s2 < 0 && e2 == EADDRINUSE, "第二次 bind 失败且 errno == EADDRINUSE");
        check(udp_bind_err::find(e2) != nullptr, "查表命中（不是走兜底）");
        show(e2, "0.0.0.0", port);
        if (s1 >= 0) {
            close(s1);
        }
    }

    printf("\n== 3. EACCES：bind <1024 特权端口 ==\n");
    {
        int e = 0;
        int s = udp_bind("0.0.0.0", 80, &e);
        if (geteuid() == 0) {
            printf("  [skip] 当前是 root（root 下不会 EACCES），跳过\n");
        } else {
            check(s < 0 && e == EACCES, "非 root bind :80 失败且 errno == EACCES");
            check(udp_bind_err::find(e) != nullptr, "查表命中（不是走兜底）");
            show(e, "0.0.0.0", 80);
        }
        if (s >= 0) {
            close(s);
        }
    }

    printf("\n== 4. EADDRNOTAVAIL：bind 本机没有的地址 ==\n");
    {
        int e = 0;
        int s = udp_bind("192.0.2.1", 45889, &e);   // TEST-NET-1，本机必然没有
        check(s < 0 && e == EADDRNOTAVAIL, "bind 192.0.2.1 失败且 errno == EADDRNOTAVAIL");
        check(udp_bind_err::find(e) != nullptr, "查表命中（不是走兜底）");
        show(e, "192.0.2.1", 45889);
        if (s >= 0) {
            close(s);
        }
    }

    printf("\n== 5. 兜底：不认识的 errno 不许假装认识 ==\n");
    {
        const int fake = 999;
        check(udp_bind_err::find(fake) == nullptr, "查表未命中（find() 返回 nullptr）");
        show(fake, "0.0.0.0", 45890);
    }

    printf("\n== 6. fd 不泄漏：失败路径必须关 fd（含反向对照）==\n");
    {
        const uint16_t port = 45888;
        int            e    = 0;
        int            holder = udp_bind("0.0.0.0", port, &e);   // 先占住端口，制造必失败
        check(holder >= 0, "占位 socket 绑定成功（后续 bind 必失败）");

        // 产品路径（tool_udp.cpp 的写法）：socket → bind 失败 → close(fd) → 状态机回滚
        const int before_ok = fd_count();
        int       fail_cnt  = 0;
        for (int i = 0; i < 32; i++) {
            int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            struct sockaddr_in a;
            memset(&a, 0, sizeof(a));
            a.sin_family      = AF_INET;
            a.sin_port        = htons(port);
            a.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(s, (struct sockaddr*)&a, sizeof(a)) < 0) {
                const int err = errno;
                if (err == EADDRINUSE) {
                    fail_cnt++;
                }
                close(s);                      // ← 就是这一句（漏了就是 fd 泄漏）
                continue;
            }
            close(s);                          // 不该发生
        }
        const int after_ok = fd_count();
        printf("  产品路径 32 轮（失败后 close）：fd %d → %d，EADDRINUSE %d 次\n",
               before_ok, after_ok, fail_cnt);
        check(fail_cnt == 32, "32 轮全部失败于 EADDRINUSE");
        check(after_ok == before_ok, "fd 计数回到基线（无泄漏）");

        // 反向对照：故意不关 fd，计数必须涨 —— 证明上面那条不是假通过
        const int before_leak = fd_count();
        for (int i = 0; i < 16; i++) {
            int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            struct sockaddr_in a;
            memset(&a, 0, sizeof(a));
            a.sin_family      = AF_INET;
            a.sin_port        = htons(port);
            a.sin_addr.s_addr = htonl(INADDR_ANY);
            if (bind(s, (struct sockaddr*)&a, sizeof(a)) < 0) {
                continue;                      // 故意漏掉 close(s)：对照组的缺陷版本
            }
            close(s);
        }
        const int after_leak = fd_count();
        printf("  反向对照 16 轮（故意不 close）：fd %d → %d\n", before_leak, after_leak);
        check(after_leak >= before_leak + 16, "不 close 时计数确实涨 ≥16（探针有效）");

        if (holder >= 0) {
            close(holder);
        }
    }

    printf("\n== 结果：%s ==\n", g_fail == 0 ? "全部符合预期" : "有不符合项，见上面 [FAIL]");
    return g_fail == 0 ? 0 : 1;
}
