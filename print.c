/* Copyright (c) 2004 Russ Cox.  See COPYRIGHT. */

#include "taskimpl.h"
#include <stdio.h> /* for strerror! */

/*
 * Stripped down print library.  Plan 9 interface, new code.
 */

enum {
    FlagLong = 1 << 0,
    FlagLongLong = 1 << 1,
    FlagUnsigned = 1 << 2,
};

/**
 * @brief 根据指定的宽度和对齐方式, 将一个字符串格式化后写入到目标缓冲区中, 并返回下一个可写入的位置
 *
 * @param dst 目标缓冲区的指针, 字符串将被写入到这里
 * @param edst 目标缓冲区的结束位置(即缓冲区的上限), 用于防止写越界
 * @param s 要处理的源字符串
 * @param size 指定输出字符串的最小宽度. 负数表示左对齐, 正数表示右对齐
 * @return char* 返回指向目标缓冲区中下一个可写入位置的指针
 */
static char *printstr(char *dst, char *edst, char *s, int size)
{
    int l, n, sign;

    sign = 1;
    if (size < 0) {
        size = -size;
        sign = -1;
    }

    if (dst >= edst) {
        return dst;
    }

    l = strlen(s);

    /* n 是目标宽度, 如果指定的宽度更大, 则使用指定的宽度(这意味着需要补空白) */
    n = l;
    if (n < size) {
        n = size;
    }

    /* 如果要求比较过分, 超出了缓冲区, 则最多处理到缓冲区结尾位置, 更多的位丢掉 */
    if (n >= edst - dst) {
        n = (edst - dst) - 1;
    }

    /* 这里也是, 丢 s 里面的数据, 以防缓冲区越界 */
    if (l > n) {
        l = n;
    }

    if (sign < 0) {
        /* 左对齐, 在后面补 0 */
        memmove(dst, s, l);
        if (n - l) {
            memset(dst + l, ' ', n - l);
        }
    } else {
        /* 右对齐, 在前面补 0 */
        if (n - l) {
            memset(dst, ' ', n - l);
        }
        memmove(dst + n - l, s, l);
    }

    return dst + n;
}

/**
 * @brief 按照指定的格式化字符串填充目标缓冲区
 *
 * @param dst 目标缓冲区
 * @param edst 目标缓冲区结束位置, 防越界
 * @param fmt 格式化字符串
 * @param arg 被格式化的参数
 * @return char*
 */
char *vseprint(char *dst, char *edst, char *fmt, va_list arg)
{
    int fl, size, sign, base;
    char *p, *w;
    char cbuf[2];

    w = dst;

    /* 扫描格式化字符串, 根据格式化要求处理 */
    for (p = fmt; *p && w < edst - 1; p++) {
        switch (*p) {
        default:
            /* 默认情况(也即不需要做特别格式化处理)下正常拷贝 */
            *w++ = *p;
            break;
        case '%':
            /* 百分号是需要格式化处理的前导标志 */
            fl = 0;
            size = 0;
            sign = 1;
            for (p++; *p; p++) {
                switch (*p) {
                case '-':
                    /* `-` 号, 表示需要左对齐 */
                    sign = -1;
                    break;
                case '0':
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                    /* 数字指示对齐宽度 */
                    size = size * 10 + *p - '0';
                    break;
                case 'l':
                    if (fl & FlagLong) {
                        fl |= FlagLongLong;
                    } else {
                        fl |= FlagLong;
                    }
                    break;
                case 'u':
                    fl |= FlagUnsigned;
                    break;
                case 'd':
                    base = 10;
                    goto num;
                case 'o':
                    base = 8;
                    goto num;
                case 'p':
                case 'x':
                    base = 16;
                    goto num;
                num: {
                    static char digits[] = "0123456789abcdef";
                    char buf[30], *p;
                    int neg, zero;
                    uvlong luv;

                    /* 根据标记情况, 取出来要格式化的参数 */
                    if (fl & FlagLongLong) {
                        if (fl & FlagUnsigned)
                            luv = va_arg(arg, uvlong);
                        else
                            luv = va_arg(arg, vlong);
                    } else {
                        if (fl & FlagLong) {
                            if (fl & FlagUnsigned)
                                luv = va_arg(arg, ulong);
                            else
                                luv = va_arg(arg, long);
                        } else {
                            if (fl & FlagUnsigned)
                                luv = va_arg(arg, uint);
                            else
                                luv = va_arg(arg, int);
                        }
                    }

                    /* 先把指针指向结束位置, 这样就可以直接从个位倒着开始输出了 */
                    p = buf + sizeof buf; /* 这个 p 是局部变量, 和外面那个没有关系*/
                    zero = 0;

                    /* TODO-DONE: 负号在哪里填充的?
                     * 答: 这里对负数的处理有 bug */
                    if (!(fl & FlagUnsigned) && (vlong)luv < 0) {
                        neg = 1;
                        luv = -luv;
                    }

                    if (luv == 0) {
                        zero = 1;
                    }

                    *--p = 0;
                    while (luv) {
                        *--p = digits[luv % base];
                        luv /= base;
                    }
                    if (base == 16) {
                        *--p = 'x';
                        *--p = '0';
                    }
                    if (base == 8 || zero)
                        *--p = '0';

                    /* 上面提到的对负号处理 fix */
                    if (neg) {
                        *--p = '-';
                    }

                    w = printstr(w, edst, p, size * sign);
                    goto break2;
                }
                case 'c':
                    cbuf[0] = va_arg(arg, int);
                    cbuf[1] = 0;
                    w = printstr(w, edst, cbuf, size * sign);
                    goto break2;
                case 's':
                    w = printstr(w, edst, va_arg(arg, char *), size * sign);
                    goto break2;
                case 'r':
                    w = printstr(w, edst, strerror(errno), size * sign);
                    goto break2;
                default:
                    p = "X*verb*";
                    goto break2;
                }
            }
        break2:
            break;
        }
    }

    assert(w < edst);
    *w = 0;
    return dst;
}

/**
 * @brief 按照 fmt 指定的格式打印字符串到缓冲区
 *
 * @param dst 缓冲区
 * @param n 缓冲区长度
 * @param fmt 格式化字符串
 * @param arg 格式化参数(va_list 格式)
 * @return char* 打印结果
 */
char *vsnprint(char *dst, uint n, char *fmt, va_list arg)
{
    return vseprint(dst, dst + n, fmt, arg);
}

/**
 * @brief 按照 fmt 指定的格式打印字符串到缓冲区
 *
 * @param dst 缓冲区
 * @param n 缓冲区长度
 * @param fmt 格式化字符串
 * @param ... 格式化参数
 * @return char* 打印结果
 */
char *snprint(char *dst, uint n, char *fmt, ...)
{
    va_list arg;

    va_start(arg, fmt);
    vsnprint(dst, n, fmt, arg);
    va_end(arg);
    return dst;
}

/**
 * @brief 按照 fmt 指定的格式打印字符串
 *
 * @param dst 缓冲区
 * @param edst 缓冲区尾指针(防溢出)
 * @param fmt 格式化字符串
 * @param ... 格式化参数
 * @return char* 打印结果
 */
char *seprint(char *dst, char *edst, char *fmt, ...)
{
    va_list arg;

    va_start(arg, fmt);
    vseprint(dst, edst, fmt, arg);
    va_end(arg);
    return dst;
}

/**
 * @brief 按照 fmt 指定的格式打印字符串到文件描述符
 *
 * @param fd 输出文件描述符
 * @param fmt 格式化字符串
 * @param arg 格式化参数(va_list)
 * @return int 输出的字节数目
 */
int vfprint(int fd, char *fmt, va_list arg)
{
    char buf[256];

    vseprint(buf, buf + sizeof buf, fmt, arg);
    return write(fd, buf, strlen(buf));
}

/**
 * @brief 按照 fmt 指定的格式打印字符串到标准输出
 *
 * @param fmt 格式化字符串
 * @param arg 格式化参数(va_list)
 * @return int 输出的字节数目
 */
int vprint(char *fmt, va_list arg)
{
    return vfprint(1, fmt, arg);
}

/**
 * @brief 按照 fmt 指定的格式打印字符串到文件描述符
 *
 * @param fd 输出文件描述符
 * @param fmt 格式化字符串
 * @param ... 格式化参数
 * @return int 输出的字节数目
 */
int fprint(int fd, char *fmt, ...)
{
    int n;
    va_list arg;

    va_start(arg, fmt);
    n = vfprint(fd, fmt, arg);
    va_end(arg);
    return n;
}

/**
 * @brief 按照 fmt 指定的格式打印字符串到标准输出
 *
 * @param fmt 格式化字符串
 * @param ... 格式化参数
 * @return int 输出的字节数目
 */
int print(char *fmt, ...)
{
    int n;
    va_list arg;

    va_start(arg, fmt);
    n = vprint(fmt, arg);
    va_end(arg);
    return n;
}

/**
 * @brief 从 src 拷贝字符串到 dst
 *
 * @param dst 目标缓冲区
 * @param edst 目标缓冲区尾指针(防溢出)
 * @param src 源缓冲区
 * @return char* 返回拷贝结果
 */
char *strecpy(char *dst, char *edst, char *src)
{
    *printstr(dst, edst, src, 0) = 0;
    return dst;
}
