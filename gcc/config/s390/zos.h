#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()	\
  do					\
    {					\
      builtin_define ("__ZOS__");	\
    }					\
  while (0)

#undef OBJECT_FORMAT_GOFF
#define OBJECT_FORMAT_GOFF

#undef TARGET_ZOS
#define TARGET_ZOS 1
#undef ASM_APP_ON
#define ASM_APP_ON "#APP\n"
#undef ASM_APP_OFF
#define ASM_APP_OFF "#NO_APP\n"

/* Before implementing switching... */
#undef TARGET_64BIT_METAL_ABI
#define TARGET_64BIT_METAL_ABI 1

/* TODO: Alignment, see:
 * https://gcc.gnu.org/onlinedocs/gccint/Storage-Layout.html#Storage-Layout */

