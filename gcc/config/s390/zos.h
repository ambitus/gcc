#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()	\
  do					\
    {					\
      builtin_define ("__ZOS__");	\
    }					\
  while (0)


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

#undef STACK_POINTER_OFFSET
#define STACK_POINTER_OFFSET 152

#define DEFAULT_PCC_STRUCT_RETURN 1

#undef FUNCTION_VALUE_REGNO_P
#define FUNCTION_VALUE_REGNO_P(N) ((N) == 15)

#undef  TARGET_ASM_FUNCTION_END_PROLOGUE
#define TARGET_ASM_FUNCTION_END_PROLOGUE s390_asm_function_end_prologue

#undef STACK_GROWS_DOWNWARD
#define STACK_GROWS_DOWNWARD 0

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

#define FIXED_REGISTERS				\
{ 0, 1, 0, 0, 					\
  0, 0, 0, 0, 					\
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
  0, 0, 0, 0, 					\
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

#define CALL_REALLY_USED_REGISTERS		\
{ 1, 0, 0, 0, 	/* r0 - r15 */			\
  0, 0, 0, 0, 					\
  0, 0, 0, 0, 					\
  0, 0, 0, 0,					\
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
  {  1, 2, 3, 4, 5, 0, 6, 7, 8, 9, 10, 11, 14, 15,			\
     16, 17, 18, 19, 20, 21, 22, 23,					\
     24, 25, 26, 27, 28, 29, 30, 31,					\
     38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 	\
     15, 32, 33, 34, 35, 36, 37 }

#undef ELIMINABLE_REGS
#define ELIMINABLE_REGS           \
{{ FRAME_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM }, \
 { BASE_REGNUM, BASE_REGNUM }, \
 { RETURN_ADDRESS_POINTER_REGNUM, STACK_POINTER_REGNUM }, \
 { RETURN_ADDRESS_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM },  \
 { STACK_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM }}
