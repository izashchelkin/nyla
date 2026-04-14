/*
 * Copyright (C) 2017-2022 Willy Tarreau <w@1wt.eu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "nyla/commons/entrypoint.h"

#if 1

auto main() -> int
{
    return nyla::LibMain(nyla::UserMain);
}

#else

#include "nyla/commons/macros.h"

int g_argc;
char **g_argv;

static void NORETURN _start_c()
{
    nyla::LibMain(nyla::UserMain);
    TRAP();
    UNREACHABLE();
}

void __attribute__((weak, noreturn, naked, no_stack_protector)) _start()
{
    __asm__ volatile("xor %%ebp, %%ebp\n"
                     "mov (%%rsp), %%rax\n" /* argc */
                     "mov %%eax, %0\n"
                     "lea 8(%%rsp), %%rax\n" /* argv */
                     "mov %%rax, %1\n"
                     "call _start_c\n"
                     "hlt\n"
                     : "=m"(g_argc), "=m"(g_argv)
                     :
                     : "rax", "memory");
}

#endif