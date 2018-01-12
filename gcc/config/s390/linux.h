/* Definitions for Linux for S/390.
   Copyright (C) 1999-2018 Free Software Foundation, Inc.
   Contributed by Hartmut Penner (hpenner@de.ibm.com) and
                  Ulrich Weigand (uweigand@de.ibm.com).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef _LINUX_H
#define _LINUX_H

/* Target specific type definitions.  */

/* For 31 bit our size type differs from most other targets (where it
   is "unsigned int").  The difference tends to cause trouble e.g.:
   Glibc BZ #16712, GCC BZ #79358 but cannot be changed due to ABI
   issues.  */
#undef  SIZE_TYPE
#define SIZE_TYPE "long unsigned int"
#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE (TARGET_64BIT ? "long int" : "int")

#undef  WCHAR_TYPE
#define WCHAR_TYPE "int"
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32


/* Target specific preprocessor settings.  */

#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      GNU_USER_TARGET_OS_CPP_BUILTINS();	\
    }						\
  while (0)


/* Target specific assembler settings.  */
/* Rewrite -march=arch* options to the original CPU name in order to
   make it work with older binutils.  */
#undef  ASM_SPEC
#define ASM_SPEC					\
  "%{m31&m64}%{mesa&mzarch}%{march=z*}"			\
  "%{march=arch3:-march=g5}"				\
  "%{march=arch5:-march=z900}"				\
  "%{march=arch6:-march=z990}"				\
  "%{march=arch7:-march=z9-ec}"				\
  "%{march=arch8:-march=z10}"				\
  "%{march=arch9:-march=z196}"				\
  "%{march=arch10:-march=zEC12}"			\
  "%{march=arch11:-march=z13}"


/* Target specific linker settings.  */

#ifdef DEFAULT_TARGET_64BIT
#define MULTILIB_DEFAULTS { "m64" }
#else
#define MULTILIB_DEFAULTS { "m31" }
#endif

#define GLIBC_DYNAMIC_LINKER32 "/lib/ld.so.1"
#define GLIBC_DYNAMIC_LINKER64 "/lib/ld64.so.1"

#undef  LINK_SPEC
#define LINK_SPEC \
  "%{m31:-m elf_s390}%{m64:-m elf64_s390} \
   %{shared:-shared} \
   %{!shared: \
      %{static:-static} \
      %{!static:%{!static-pie: \
	%{rdynamic:-export-dynamic} \
	%{m31:-dynamic-linker " GNU_USER_DYNAMIC_LINKER32 "} \
	%{m64:-dynamic-linker " GNU_USER_DYNAMIC_LINKER64 "}}}} \
   %{static-pie:-static -pie --no-dynamic-linker -z text}"

#define CPP_SPEC "%{posix:-D_POSIX_SOURCE} %{pthread:-D_REENTRANT}"

#define TARGET_ASM_FILE_END file_end_indicate_exec_stack

#ifdef TARGET_LIBC_PROVIDES_SSP
/* s390 glibc provides __stack_chk_guard in 0x14(tp),
   s390x glibc provides it at 0x28(tp).  */
#define TARGET_THREAD_SSP_OFFSET        (TARGET_64BIT ? 0x28 : 0x14)
#endif

/* Define if long doubles should be mangled as 'g'.  */
#define TARGET_ALTERNATE_LONG_DOUBLE_MANGLING

#undef TARGET_LIBC_HAS_FUNCTION
#define TARGET_LIBC_HAS_FUNCTION gnu_libc_has_function

/* Uninitialized common symbols in non-PIE executables, even with
   strong definitions in dependent shared libraries, will resolve
   to COPY relocated symbol in the executable.  See PR65780.  */
#undef TARGET_BINDS_LOCAL_P
#define TARGET_BINDS_LOCAL_P default_binds_local_p_2

/* MICHAEL: Migrating non-common constants */
#undef STACK_POINTER_REGNUM
#define STACK_POINTER_REGNUM 15
#undef HARD_FRAME_POINTER_REGNUM
#define HARD_FRAME_POINTER_REGNUM 11
/* Only gpr 2, fpr 0, and v24 are ever used as return registers.  */
#undef FUNCTION_VALUE_REGNO_P
#define FUNCTION_VALUE_REGNO_P(N)          \
    ((N) == 2 || (N) == 16 || (TARGET_VX && (N) == FIRST_VEC_ARG_REGNO))


/* Set up fixed registers and calling convention:

   GPRs 0-5 are always call-clobbered,
   GPRs 6-15 are always call-saved.
   GPR 12 is fixed if used as GOT pointer.
   GPR 13 is always fixed (as literal pool pointer).
   GPR 14 is always fixed on S/390 machines (as return address).
   GPR 15 is always fixed (as stack pointer).
   The 'fake' hard registers are call-clobbered and fixed.
   The access registers are call-saved and fixed.

   On 31-bit, FPRs 18-19 are call-clobbered;
   on 64-bit, FPRs 24-31 are call-clobbered.
   The remaining FPRs are call-saved.

   All non-FP vector registers are call-clobbered v16-v31.  */

#define FIXED_REGISTERS       \
{ 0, 0, 0, 0,           \
  0, 0, 0, 0,           \
  0, 0, 0, 0,           \
  0, 1, 1, 1,         \
  0, 0, 0, 0,           \
  0, 0, 0, 0,           \
  0, 0, 0, 0,           \
  0, 0, 0, 0,           \
  1, 1, 1, 1,         \
  1, 1,           \
  0, 0, 0, 0,           \
  0, 0, 0, 0,           \
  0, 0, 0, 0,           \
  0, 0, 0, 0 }

#define CALL_USED_REGISTERS     \
{ 1, 1, 1, 1,           \
  1, 1, 0, 0,           \
  0, 0, 0, 0,           \
  0, 1, 1, 1,         \
  1, 1, 1, 1,           \
  1, 1, 1, 1,           \
  1, 1, 1, 1,           \
  1, 1, 1, 1,           \
  1, 1, 1, 1,         \
  1, 1,                 \
  1, 1, 1, 1,           \
  1, 1, 1, 1,         \
  1, 1, 1, 1,           \
  1, 1, 1, 1 }

#define CALL_REALLY_USED_REGISTERS    \
{ 1, 1, 1, 1,   /* r0 - r15 */      \
  1, 1, 0, 0,           \
  0, 0, 0, 0,           \
  0, 0, 0, 0,         \
  1, 1, 1, 1,   /* f0 (16) - f15 (31) */  \
  1, 1, 1, 1,           \
  1, 1, 1, 1,           \
  1, 1, 1, 1,           \
  1, 1, 1, 1, /* arg, cc, fp, ret addr */ \
  0, 0,   /* a0 (36), a1 (37) */          \
  1, 1, 1, 1,   /* v16 (38) - v23 (45) */ \
  1, 1, 1, 1,         \
  1, 1, 1, 1,   /* v24 (46) - v31 (53) */ \
  1, 1, 1, 1 }

/* Preferred register allocation order.  */
#define REG_ALLOC_ORDER             \
  {  1, 2, 3, 4, 5, 0, 12, 11, 10, 9, 8, 7, 6, 14, 13,      \
     16, 17, 18, 19, 20, 21, 22, 23,          \
     24, 25, 26, 27, 28, 29, 30, 31,          \
     38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,  \
     15, 32, 33, 34, 35, 36, 37 }

#endif
