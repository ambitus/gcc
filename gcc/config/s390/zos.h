/* Definitions for Linux for S/390.
   Copyright (C) 2019 Free Software Foundation, Inc.
   Contributed by Giancarlo Frix (gfrix@rocketsoftware.com) and
                  Michael Colavita (mcolavita@rocketsoftware.com).

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

#undef  SIZE_TYPE
#define SIZE_TYPE "long unsigned int"
#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE (TARGET_64BIT ? "long int" : "int")

#undef  WCHAR_TYPE
#define WCHAR_TYPE "int"
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

#define TARGET_OS_CPP_BUILTINS()			\
  do							\
    {							\
      builtin_define ("__ZOS__");			\
      builtin_define_std ("unix");			\
      builtin_assert ("system=unix");			\
      builtin_assert ("system=posix");			\
    }							\
  while (0)

#undef GNU_USER_DYNAMIC_LINKER
#define GNU_USER_DYNAMIC_LINKER "/lib/ld64.so.1"

#undef  ASM_SPEC
#define ASM_SPEC				\
  "%{m31&m64}%{mesa&mzarch}%{march=z*}"	\
  "%{march=arch3:-march=g5}"			\
  "%{march=arch5:-march=z900}"			\
  "%{march=arch6:-march=z990}"			\
  "%{march=arch7:-march=z9-ec}"		\
  "%{march=arch8:-march=z10}"			\
  "%{march=arch9:-march=z196}"			\
  "%{march=arch10:-march=zEC12}"		\
  "%{march=arch11:-march=z13}"

#ifdef DEFAULT_TARGET_64BIT
#define MULTILIB_DEFAULTS { "m64" }
#else
#define MULTILIB_DEFAULTS { "m31" }
#endif

/* z/OS TODO: Handle static PIEs.  */
#undef  LINK_SPEC
#define LINK_SPEC \
  "%{m64:-m po64_s390 -b po64-s390} \
   %{shared:-shared} \
   %{!shared: \
      %{static:-static} \
      %{!static: \
	%{rdynamic:-export-dynamic} \
	-dynamic-linker " GNU_USER_DYNAMIC_LINKER "}}"

#define CPP_SPEC "%{posix:-D_POSIX_SOURCE} %{pthread:-D_REENTRANT}"

/* Define if long doubles should be mangled as 'g'.  */
#define TARGET_ALTERNATE_LONG_DOUBLE_MANGLING

#undef TARGET_LIBC_HAS_FUNCTION
#define TARGET_LIBC_HAS_FUNCTION gnu_libc_has_function

/* Uninitialized common symbols in non-PIE executables, even with
   strong definitions in dependent shared libraries, will resolve
   to COPY relocated symbol in the executable.  See PR65780.  */
#undef TARGET_BINDS_LOCAL_P
#define TARGET_BINDS_LOCAL_P default_binds_local_p_2

/* DWARF is throwing a fit about F4SA... TODO */
/*
#undef DWARF2_DEBUGGING_INFO
#undef DWARF2_UNWIND_INFO
*/

#undef OBJECT_FORMAT_GOFF
#define OBJECT_FORMAT_GOFF

#undef TARGET_ZOS
#define TARGET_ZOS 1

#undef ASM_APP_ON
#define ASM_APP_ON "#APP\n"
#undef ASM_APP_OFF
#define ASM_APP_OFF "#NO_APP\n"


/* Stack configurations */
#define TARGET_ZOS_STACK_F4SA (TARGET_ZOS && s390_zos_stack_format == F4SA)
#define TARGET_ZOS_STACK_XPLINK (TARGET_ZOS && s390_zos_stack_format == XPLINK)

/* MICHAEL: Migrating non-common constants */
#undef STACK_POINTER_REGNUM
#define STACK_POINTER_REGNUM 11
#undef HARD_FRAME_POINTER_REGNUM
#define HARD_FRAME_POINTER_REGNUM 13
#undef ARG_POINTER_REGNUM
#define ARG_POINTER_REGNUM 1
#undef BASE_REGNUM
#define BASE_REGNUM 9

/* Offset from stack-pointer to first location of outgoing args.  */
#define STACK_POINTER_OFFSET (-crtl->outgoing_args_size.to_constant ())

/* Offset from the stack pointer register to an item dynamically
   allocated on the stack, e.g., by `alloca'.  */
#define STACK_DYNAMIC_OFFSET(FUNDECL) STACK_POINTER_OFFSET

/* Defining this macro makes __builtin_frame_address(0) and
   __builtin_return_address(0) work with -fomit-frame-pointer.  */
#define INITIAL_FRAME_ADDRESS_RTX (hard_frame_pointer_rtx)

/* The return address of the current frame is retrieved
   from the initial value of register RETURN_REGNUM.
   For frames farther back, we use the stack slot where
   the corresponding RETURN_REGNUM register was saved.  */
#define DYNAMIC_CHAIN_ADDRESS(FRAME) (plus_constant (Pmode, (FRAME), 128))

/* z/OS TODO: check if this is right.  */
#define FRAME_ADDR_RTX(FRAME) (hard_frame_pointer_rtx)

#define INCOMING_FRAME_SP_OFFSET 0

#define DEFAULT_PCC_STRUCT_RETURN 1

#undef FUNCTION_VALUE_REGNO_P
#define FUNCTION_VALUE_REGNO_P(N) ((N) == 15)

/* Our stack grows from lower to higher addresses, local variables
   are accessed by negative offsets from the virtual frame pointer,
   and function arguments are stored at increasing addresses.  */
#define STACK_GROWS_DOWNWARD 0

/* z/OS TODO: The current BIGGEST_ALIGN and STRICT_ALIGNMENT in s390.h
   seem suspect in the face of the 16-byte atomic instructions, some of
   which require 16-byte alignment or else emit a hardware exception. I
   haven't seen the linux port generate incorrect atomic code yet, but at
   some point we should go through and check the atomic codegen.  */

/* Our CFA must be the bottom of the frame, because we cannot know where
   the top of the frame is when we are returning.  */
#undef ARG_POINTER_CFA_OFFSET
#undef FRAME_POINTER_CFA_OFFSET
#define FRAME_POINTER_CFA_OFFSET(FNDECL) \
  ((void) (FNDECL), -get_frame_size ().coeffs[0] - 152)

/* Set up fixed registers and calling convention:

   GPRs 0-10 are scratch, call-saved.
   GPR 11 is always fixed as stack pointer.
   GPR 12 is always fixed as CAA pointer.
   GPR 13 is always fixed (as literal pool pointer).
   GPR 14 is always fixed on S/390 machines (as return address).
   GPR 15 is always fixed (as return value).
   The 'fake' hard registers are call-clobbered and fixed.
   The access registers are call-saved and fixed.

   On 31-bit, FPRs 18-19 are call-clobbered;
   on 64-bit, FPRs 24-31 are call-clobbered.
   The remaining FPRs are call-saved.

   All non-FP vector registers are call-clobbered v16-v31.  */

/* z/OS TODO: We should try allowing r1 to be used for other purposes.
   Also we should DEFINITELY make r14 available, since it will usually
   be saved anyway and should be available for the rest of the
   function.
   z/OS TODO: We needed to make r4 fixed as a stopgap fix for an issue
   where the compiler is trying to load incoming args from r1 after r1
   has been clobbered by a call. Find some more permanant solution.  */
#define FIXED_REGISTERS				\
{ 0, 1, 0, 0, 					\
  1, 0, 0, 0, 					\
  0, 0, 0, 1, 					\
  1, 1, 1, 0,					\
  0, 0, 0, 0, 					\
  0, 0, 0, 0, 					\
  0, 0, 0, 0, 					\
  0, 0, 0, 0, 					\
  1, 1, 1, 1,					\
  1, 1,						\
  0, 0, 0, 0, 					\
  0, 0, 0, 0, 					\
  0, 0, 0, 0, 					\
  0, 0, 0, 0 }

#define CALL_USED_REGISTERS			\
{ 1, 1, 0, 0, 					\
  1, 0, 0, 0, 					\
  0, 0, 0, 1, 					\
  1, 1, 1, 1,					\
  1, 1, 1, 1, 					\
  1, 1, 1, 1, 					\
  1, 1, 1, 1, 					\
  1, 1, 1, 1, 					\
  1, 1, 1, 1,					\
  1, 1,					        \
  1, 1, 1, 1, 					\
  1, 1, 1, 1,					\
  1, 1, 1, 1, 					\
  1, 1, 1, 1 }

/* z/OS TODO: Should this not include r15 (and maybe r1?).  */
#define CALL_REALLY_USED_REGISTERS		\
{ 1, 0, 0, 0, 	/* r0 - r15 */			\
  0, 0, 0, 0, 					\
  0, 0, 0, 0, 					\
  0, 0, 0, 1,					\
  1, 1, 1, 1, 	/* f0 (16) - f15 (31) */	\
  1, 1, 1, 1, 					\
  1, 1, 1, 1, 					\
  1, 1, 1, 1, 					\
  1, 1, 1, 1,	/* arg, cc, fp, ret addr */	\
  0, 0,		/* a0 (36), a1 (37) */	        \
  1, 1, 1, 1, 	/* v16 (38) - v23 (45) */	\
  1, 1, 1, 1,					\
  1, 1, 1, 1, 	/* v24 (46) - v31 (53) */	\
  1, 1, 1, 1 }

/* Preferred register allocation order.  */
#define REG_ALLOC_ORDER							\
  {  15, 0, 14,  /* Prefer the call-clobbered regs and r14.  */	\
     1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,				\
     16, 17, 18, 19, 20, 21, 22, 23,					\
     24, 25, 26, 27, 28, 29, 30, 31,					\
     38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 	\
     15, 32, 33, 34, 35, 36, 37 }

/* Elimination rules.  */

#undef ELIMINABLE_REGS
#define ELIMINABLE_REGS							\
{{ RETURN_ADDRESS_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM },		\
 { FRAME_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM },			\
 { STACK_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM },			\
 { BASE_REGNUM, BASE_REGNUM }}
