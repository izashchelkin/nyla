#include "nyla/commons/macros.h"

namespace nyla
{

auto UserMain() -> int;
void NYLA_API NORETURN LibMain(int (*userMain)());

} // namespace nyla

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