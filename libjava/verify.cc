// defineclass.cc - defining a class from .class format.

/* Copyright (C) 2001  Free Software Foundation

   This file is part of libgcj.

This software is copyrighted work licensed under the terms of the
Libgcj License.  Please consult the file "LIBGCJ_LICENSE" for
details.  */

// Writte by Tom Tromey <tromey@redhat.com>

#include <config.h>

#include <jvm.h>
#include <gcj/cni.h>
#include <java-insns.h>
#include <java-interp.h>

#ifdef INTERPRETER

#include <java/lang/Class.h>
#include <java/lang/VerifyError.h>
#include <java/lang/Throwable.h>
#include <java/lang/reflect/Modifier.h>


// TO DO
// * read more about when classes must be loaded
// * there are bugs with boolean arrays?
// * class loader madness
// * Lots and lots of debugging and testing
// * type representation is still ugly.  look for the big switches
// * at least one GC problem :-(


// This is global because __attribute__ doesn't seem to work on static
// methods.
static void verify_fail (char *s) __attribute__ ((__noreturn__));

class _Jv_BytecodeVerifier
{
private:

  static const int FLAG_INSN_START = 1;
  static const int FLAG_BRANCH_TARGET = 2;
  static const int FLAG_JSR_TARGET = 4;

  struct state;
  struct type;
  struct subr_info;

  // The current PC.
  int PC;
  // The PC corresponding to the start of the current instruction.
  int start_PC;

  // The current state of the stack, locals, etc.
  state *current_state;

  // We store the state at branch targets, for merging.  This holds
  // such states.
  state **states;

  // We keep a linked list of all the PCs which we must reverify.
  // The link is done using the PC values.  This is the head of the
  // list.
  int next_verify_pc;

  // We keep some flags for each instruction.  The values are the
  // FLAG_* constants defined above.
  char *flags;

  // We need to keep track of which instructions can call a given
  // subroutine.  FIXME: this is inefficient.  We keep a linked list
  // of all calling `jsr's at at each jsr target.
  subr_info **jsr_ptrs;

  // The current top of the stack, in terms of slots.
  int stacktop;
  // The current depth of the stack.  This will be larger than
  // STACKTOP when wide types are on the stack.
  int stackdepth;

  // The bytecode itself.
  unsigned char *bytecode;
  // The exceptions.
  _Jv_InterpException *exception;

  // Defining class.
  jclass current_class;
  // This method.
  _Jv_InterpMethod *current_method;

  // This enum holds a list of tags for all the different types we
  // need to handle.  Reference types are treated specially by the
  // type class.
  enum type_val
  {
    void_type,

    // The values for primitive types are chosen to correspond to values
    // specified to newarray.
    boolean_type = 4,
    char_type = 5,
    float_type = 6,
    double_type = 7,
    byte_type = 8,
    short_type = 9,
    int_type = 10,
    long_type = 11,

    // Used when overwriting second word of a double or long in the
    // local variables.  Also used after merging local variable states
    // to indicate an unusable value.
    unsuitable_type,
    return_address_type,
    continuation_type,

    // Everything after `reference_type' must be a reference type.
    reference_type,
    null_type,
    unresolved_reference_type,
    uninitialized_reference_type,
    uninitialized_unresolved_reference_type
  };

  // Return the type_val corresponding to a primitive signature
  // character.  For instance `I' returns `int.class'.
  static type_val get_type_val_for_signature (jchar sig)
  {
    type_val rt;
    switch (sig)
      {
      case 'Z':
	rt = boolean_type;
	break;
      case 'C':
	rt = char_type;
	break;
      case 'S':
	rt = short_type;
	break;
      case 'I':
	rt = int_type;
	break;
      case 'J':
	rt = long_type;
	break;
      case 'F':
	rt = float_type;
	break;
      case 'D':
	rt = double_type;
	break;
      case 'V':
	rt = void_type;
	break;
      default:
	verify_fail ("invalid signature");
      }
    return rt;
  }

  // Return the type_val corresponding to a primitive class.
  static type_val get_type_val_for_signature (jclass k)
  {
    return get_type_val_for_signature ((jchar) k->method_count);
  }

  // This is used to keep track of which `jsr's correspond to a given
  // jsr target.
  struct subr_info
  {
    // PC of the instruction just after the jsr.
    int pc;
    // Link.
    subr_info *next;
  };

  // The `type' class is used to represent a single type in the
  // verifier.
  struct type
  {
    // The type.
    type_val key;
    // Some associated data.
    union
    {
      // For a resolved reference type, this is a pointer to the class.
      jclass klass;
      // For other reference types, this it the name of the class.
      _Jv_Utf8Const *name;
    } data;
    // This is used when constructing a new object.  It is the PC of the
    // `new' instruction which created the object.  We use the special
    // value -2 to mean that this is uninitialized, and the special
    // value -1 for the case where the current method is itself the
    // <init> method.
    int pc;

    static const int UNINIT = -2;
    static const int SELF = -1;

    // Basic constructor.
    type ()
    {
      key = unsuitable_type;
      data.klass = NULL;
      pc = UNINIT;
    }

    // Make a new instance given the type tag.  We assume a generic
    // `reference_type' means Object.
    type (type_val k)
    {
      key = k;
      data.klass = NULL;
      if (key == reference_type)
	data.klass = &java::lang::Object::class$;
      pc = UNINIT;
    }

    // Make a new instance given a class.
    type (jclass klass)
    {
      key = reference_type;
      data.klass = klass;
      pc = UNINIT;
    }

    // Make a new instance given the name of a class.
    type (_Jv_Utf8Const *n)
    {
      key = unresolved_reference_type;
      data.name = n;
      pc = UNINIT;
    }

    // Copy constructor.
    type (const type &t)
    {
      key = t.key;
      data = t.data;
      pc = t.pc;
    }

    // These operators are required because libgcj can't link in
    // -lstdc++.
    void *operator new[] (size_t bytes)
    {
      return _Jv_Malloc (bytes);
    }

    void operator delete[] (void *mem)
    {
      _Jv_Free (mem);
    }

    type& operator= (type_val k)
    {
      key = k;
      data.klass = NULL;
      pc = UNINIT;
      return *this;
    }

    type& operator= (const type& t)
    {
      key = t.key;
      data = t.data;
      pc = t.pc;
      return *this;
    }

    // Promote a numeric type.
    void promote ()
    {
      if (key == boolean_type || key == char_type
	  || key == byte_type || key == short_type)
	key = int_type;
    }

    // If *THIS is an unresolved reference type, resolve it.
    void resolve ()
    {
      if (key != unresolved_reference_type
	  && key != uninitialized_unresolved_reference_type)
	return;

      // FIXME: class loader
      using namespace java::lang;
      // We might see either kind of name.  Sigh.
      if (data.name->data[0] == 'L'
	  && data.name->data[data.name->length - 1] == ';')
	data.klass = _Jv_FindClassFromSignature (data.name->data, NULL);
      else
	data.klass = Class::forName (_Jv_NewStringUtf8Const (data.name),
				     false, NULL);
      key = (key == unresolved_reference_type
	     ? reference_type
	     : uninitialized_reference_type);
    }

    // Mark this type as the uninitialized result of `new'.
    void set_uninitialized (int pc)
    {
      if (key != reference_type && key != unresolved_reference_type)
	verify_fail ("internal error in type::uninitialized");
      key = (key == reference_type
	     ? uninitialized_reference_type
	     : uninitialized_unresolved_reference_type);
      pc = pc;
    }

    // Mark this type as now initialized.
    void set_initialized (int npc)
    {
      if (pc == npc)
	{
	  key = (key == uninitialized_reference_type
		 ? reference_type
		 : unresolved_reference_type);
	  pc = UNINIT;
	}
    }


    // Return true if an object of type K can be assigned to a variable
    // of type *THIS.  Handle various special cases too.  Might modify
    // *THIS or K.  Note however that this does not perform numeric
    // promotion.
    bool compatible (type &k)
    {
      // Any type is compatible with the unsuitable type.
      if (key == unsuitable_type)
	return true;

      if (key < reference_type || k.key < reference_type)
	return key == k.key;

      // The `null' type is convertible to any reference type.
      // FIXME: is this correct for THIS?
      if (key == null_type || k.key == null_type)
	return true;

      // Any reference type is convertible to Object.  This is a special
      // case so we don't need to unnecessarily resolve a class.
      if (key == reference_type
	  && data.klass == &java::lang::Object::class$)
	return true;

      // An initialized type and an uninitialized type are not
      // compatible.
      if (isinitialized () != k.isinitialized ())
	return false;

      // Two uninitialized objects are compatible if either:
      // * The PCs are identical, or
      // * One PC is UNINIT.
      if (! isinitialized ())
	{
	  if (pc != k.pc && pc != UNINIT && k.pc != UNINIT)
	    return false;
	}

      // Two unresolved types are equal if their names are the same.
      if (! isresolved ()
	  && ! k.isresolved ()
	  && _Jv_equalUtf8Consts (data.name, k.data.name))
	return true;

      // We must resolve both types and check assignability.
      resolve ();
      k.resolve ();
      // Use _Jv_IsAssignableFrom to avoid premature class
      // initialization.
      return _Jv_IsAssignableFrom (data.klass, k.data.klass);
    }

    bool isvoid () const
    {
      return key == void_type;
    }

    bool iswide () const
    {
      return key == long_type || key == double_type;
    }

    // Return number of stack or local variable slots taken by this
    // type.
    int depth () const
    {
      return iswide () ? 2 : 1;
    }

    bool isarray () const
    {
      // We treat null_type as not an array.  This is ok based on the
      // current uses of this method.
      if (key == reference_type)
	return data.klass->isArray ();
      else if (key == unresolved_reference_type)
	return data.name->data[0] == '[';
      return false;
    }

    bool isinterface ()
    {
      resolve ();
      if (key != reference_type)
	return false;
      return data.klass->isInterface ();
    }

    bool isabstract ()
    {
      resolve ();
      if (key != reference_type)
	return false;
      using namespace java::lang::reflect;
      return Modifier::isAbstract (data.klass->getModifiers ());
    }

    // Return the element type of an array.
    type element_type ()
    {
      // FIXME: maybe should do string manipulation here.
      resolve ();
      if (key != reference_type)
	verify_fail ("programmer error in type::element_type()");

      jclass k = data.klass->getComponentType ();
      if (k->isPrimitive ())
	return type (get_type_val_for_signature (k));
      return type (k);
    }

    bool isreference () const
    {
      return key >= reference_type;
    }

    int get_pc () const
    {
      return pc;
    }

    bool isinitialized () const
    {
      return (key == reference_type
	      || key == null_type
	      || key == unresolved_reference_type);
    }

    bool isresolved () const
    {
      return (key == reference_type
	      || key == null_type
	      || key == uninitialized_reference_type);
    }

    void verify_dimensions (int ndims)
    {
      // The way this is written, we don't need to check isarray().
      if (key == reference_type)
	{
	  jclass k = data.klass;
	  while (k->isArray () && ndims > 0)
	    {
	      k = k->getComponentType ();
	      --ndims;
	    }
	}
      else
	{
	  // We know KEY == unresolved_reference_type.
	  char *p = data.name->data;
	  while (*p++ == '[' && ndims-- > 0)
	    ;
	}

      if (ndims > 0)
	verify_fail ("array type has fewer dimensions than required");
    }

    // Merge OLD_TYPE into this.  On error throw exception.
    bool merge (type& old_type, bool local_semantics = false)
    {
      bool changed = false;
      bool refo = old_type.isreference ();
      bool refn = isreference ();
      if (refo && refn)
	{
	  if (old_type.key == null_type)
	    ;
	  else if (key == null_type)
	    {
	      *this = old_type;
	      changed = true;
	    }
	  else if (isinitialized () != old_type.isinitialized ())
	    verify_fail ("merging initialized and uninitialized types");
	  else
	    {
	      if (! isinitialized ())
		{
		  if (pc == UNINIT)
		    pc = old_type.pc;
		  else if (old_type.pc == UNINIT)
		    ;
		  else if (pc != old_type.pc)
		    verify_fail ("merging different uninitialized types");
		}

	      if (! isresolved ()
		  && ! old_type.isresolved ()
		  && _Jv_equalUtf8Consts (data.name, old_type.data.name))
		{
		  // Types are identical.
		}
	      else
		{
		  resolve ();
		  old_type.resolve ();

		  jclass k = data.klass;
		  jclass oldk = old_type.data.klass;

		  int arraycount = 0;
		  while (k->isArray () && oldk->isArray ())
		    {
		      ++arraycount;
		      k = k->getComponentType ();
		      oldk = oldk->getComponentType ();
		    }

		  // This loop will end when we hit Object.
		  while (true)
		    {
		      // Use _Jv_IsAssignableFrom to avoid premature
		      // class initialization.
		      if (_Jv_IsAssignableFrom (k, oldk))
			break;
		      k = k->getSuperclass ();
		      changed = true;
		    }

		  if (changed)
		    {
		      while (arraycount > 0)
			{
			  // FIXME: Class loader.
			  k = _Jv_GetArrayClass (k, NULL);
			  --arraycount;
			}
		      data.klass = k;
		    }
		}
	    }
	}
      else if (refo || refn || key != old_type.key)
	{
	  if (local_semantics)
	    {
	      key = unsuitable_type;
	      changed = true;
	    }
	  else
	    verify_fail ("unmergeable type");
	}
      return changed;
    }
  };

  // This class holds all the state information we need for a given
  // location.
  struct state
  {
    // Current top of stack.
    int stacktop;
    // Current stack depth.  This is like the top of stack but it
    // includes wide variable information.
    int stackdepth;
    // The stack.
    type *stack;
    // The local variables.
    type *locals;
    // This is used in subroutines to keep track of which local
    // variables have been accessed.
    bool *local_changed;
    // If not 0, then we are in a subroutine.  The value is the PC of
    // the subroutine's entry point.  We can use 0 as an exceptional
    // value because PC=0 can never be a subroutine.
    int subroutine;
    // This is used to keep a linked list of all the states which
    // require re-verification.  We use the PC to keep track.
    int next;

    // INVALID marks a state which is not on the linked list of states
    // requiring reverification.
    static const int INVALID = -1;
    // NO_NEXT marks the state at the end of the reverification list.
    static const int NO_NEXT = -2;

    state ()
    {
      stack = NULL;
      locals = NULL;
      local_changed = NULL;
    }

    state (int max_stack, int max_locals)
    {
      stacktop = 0;
      stackdepth = 0;
      stack = new type[max_stack];
      for (int i = 0; i < max_stack; ++i)
	stack[i] = unsuitable_type;
      locals = new type[max_locals];
      local_changed = (bool *) _Jv_Malloc (sizeof (bool) * max_locals);
      for (int i = 0; i < max_locals; ++i)
	{
	  locals[i] = unsuitable_type;
	  local_changed[i] = false;
	}
      next = INVALID;
      subroutine = 0;
    }

    state (const state *copy, int max_stack, int max_locals)
    {
      stack = new type[max_stack];
      locals = new type[max_locals];
      local_changed = (bool *) _Jv_Malloc (sizeof (bool) * max_locals);
      *this = *copy;
      next = INVALID;
    }

    ~state ()
    {
      if (stack)
	delete[] stack;
      if (locals)
	delete[] locals;
      if (local_changed)
	_Jv_Free (local_changed);
    }

    void *operator new[] (size_t bytes)
    {
      return _Jv_Malloc (bytes);
    }

    void operator delete[] (void *mem)
    {
      _Jv_Free (mem);
    }

    void *operator new (size_t bytes)
    {
      return _Jv_Malloc (bytes);
    }

    void operator delete (void *mem)
    {
      _Jv_Free (mem);
    }

    void copy (const state *copy, int max_stack, int max_locals)
    {
      stacktop = copy->stacktop;
      stackdepth = copy->stackdepth;
      subroutine = copy->subroutine;
      for (int i = 0; i < max_stack; ++i)
	stack[i] = copy->stack[i];
      for (int i = 0; i < max_locals; ++i)
	{
	  locals[i] = copy->locals[i];
	  local_changed[i] = copy->local_changed[i];
	}
      // Don't modify `next'.
    }

    // Modify this state to reflect entry to an exception handler.
    void set_exception (type t, int max_stack)
    {
      stackdepth = 1;
      stacktop = 1;
      stack[0] = t;
      for (int i = stacktop; i < max_stack; ++i)
	stack[i] = unsuitable_type;

      // FIXME: subroutine handling?
    }

    // Merge STATE into this state.  Destructively modifies this state.
    // Returns true if the new state was in fact changed.  Will throw an
    // exception if the states are not mergeable.
    bool merge (state *state_old, bool ret_semantics,
		int max_locals)
    {
      bool changed = false;

      // Merge subroutine states.  *THIS and *STATE_OLD must be in the
      // same subroutine.  Also, recursive subroutine calls must be
      // avoided.
      if (subroutine == state_old->subroutine)
	{
	  // Nothing.
	}
      else if (subroutine == 0)
	{
	  subroutine = state_old->subroutine;
	  changed = true;
	}
      else
	verify_fail ("subroutines merged");

      // Merge stacks.
      if (state_old->stacktop != stacktop)
	verify_fail ("stack sizes differ");
      for (int i = 0; i < state_old->stacktop; ++i)
	{
	  if (stack[i].merge (state_old->stack[i]))
	    changed = true;
	}

      // Merge local variables.
      for (int i = 0; i < max_locals; ++i)
	{
	  if (! ret_semantics || local_changed[i])
	    {
	      if (locals[i].merge (state_old->locals[i], true))
		{
		  changed = true;
		  note_variable (i);
		}
	    }

	  // If we're in a subroutine, we must compute the union of
	  // all the changed local variables.
	  if (state_old->local_changed[i])
	    note_variable (i);
	}

      return changed;
    }

    // Throw an exception if there is an uninitialized object on the
    // stack or in a local variable.  EXCEPTION_SEMANTICS controls
    // whether we're using backwards-branch or exception-handing
    // semantics.
    void check_no_uninitialized_objects (int max_locals,
					 bool exception_semantics = false)
    {
      if (! exception_semantics)
	{
	  for (int i = 0; i < stacktop; ++i)
	    if (stack[i].isreference () && ! stack[i].isinitialized ())
	      verify_fail ("uninitialized object on stack");
	}

      for (int i = 0; i < max_locals; ++i)
	if (locals[i].isreference () && ! locals[i].isinitialized ())
	  verify_fail ("uninitialized object in local variable");
    }

    // Note that a local variable was accessed or modified.
    void note_variable (int index)
    {
      if (subroutine > 0)
	local_changed[index] = true;
    }

    // Mark each `new'd object we know of that was allocated at PC as
    // initialized.
    void set_initialized (int pc, int max_locals)
    {
      for (int i = 0; i < stacktop; ++i)
	stack[i].set_initialized (pc);
      for (int i = 0; i < max_locals; ++i)
	locals[i].set_initialized (pc);
    }
  };

  type pop_raw ()
  {
    if (current_state->stacktop <= 0)
      verify_fail ("stack empty");
    type r = current_state->stack[--current_state->stacktop];
    current_state->stackdepth -= r.depth ();
    if (current_state->stackdepth < 0)
      verify_fail ("stack empty");
    return r;
  }

  type pop32 ()
  {
    type r = pop_raw ();
    if (r.iswide ())
      verify_fail ("narrow pop of wide type");
    return r;
  }

  type pop64 ()
  {
    type r = pop_raw ();
    if (! r.iswide ())
      verify_fail ("wide pop of narrow type");
    return r;
  }

  type pop_type (type match)
  {
    type t = pop_raw ();
    if (! match.compatible (t))
      verify_fail ("incompatible type on stack");
    return t;
  }

  void push_type (type t)
  {
    // If T is a numeric type like short, promote it to int.
    t.promote ();

    int depth = t.depth ();
    if (current_state->stackdepth + depth > current_method->max_stack)
      verify_fail ("stack overflow");
    current_state->stack[current_state->stacktop++] = t;
    current_state->stackdepth += depth;
  }

  void set_variable (int index, type t)
  {
    // If T is a numeric type like short, promote it to int.
    t.promote ();

    int depth = t.depth ();
    if (index > current_method->max_locals - depth)
      verify_fail ("invalid local variable");
    current_state->locals[index] = t;
    current_state->note_variable (index);

    if (depth == 2)
      {
	current_state->locals[index + 1] = continuation_type;
	current_state->note_variable (index + 1);
      }
    if (index > 0 && current_state->locals[index - 1].iswide ())
      {
	current_state->locals[index - 1] = unsuitable_type;
	// There's no need to call note_variable here.
      }
  }

  type get_variable (int index, type t)
  {
    int depth = t.depth ();
    if (index > current_method->max_locals - depth)
      verify_fail ("invalid local variable");
    if (! t.compatible (current_state->locals[index]))
      verify_fail ("incompatible type in local variable");
    if (depth == 2)
      {
	type t (continuation_type);
	if (! current_state->locals[index + 1].compatible (t))
	  verify_fail ("invalid local variable");
      }
    current_state->note_variable (index);
    return current_state->locals[index];
  }

  // Make sure ARRAY is an array type and that its elements are
  // compatible with type ELEMENT.  Returns the actual element type.
  type require_array_type (type array, type element)
  {
    if (! array.isarray ())
      verify_fail ("array required");

    type t = array.element_type ();
    if (! element.compatible (t))
      verify_fail ("incompatible array element type");

    // Return T and not ELEMENT, because T might be specialized.
    return t;
  }

  jint get_byte ()
  {
    if (PC >= current_method->code_length)
      verify_fail ("premature end of bytecode");
    return (jint) bytecode[PC++] & 0xff;
  }

  jint get_ushort ()
  {
    jbyte b1 = get_byte ();
    jbyte b2 = get_byte ();
    return (jint) ((b1 << 8) | b2) & 0xffff;
  }

  jint get_short ()
  {
    jbyte b1 = get_byte ();
    jbyte b2 = get_byte ();
    jshort s = (b1 << 8) | b2;
    return (jint) s;
  }

  jint get_int ()
  {
    jbyte b1 = get_byte ();
    jbyte b2 = get_byte ();
    jbyte b3 = get_byte ();
    jbyte b4 = get_byte ();
    return (b1 << 24) | (b2 << 16) | (b3 << 8) | b4;
  }

  int compute_jump (int offset)
  {
    int npc = start_PC + offset;
    if (npc < 0 || npc >= current_method->code_length)
      verify_fail ("branch out of range");
    return npc;
  }

  // Merge the indicated state into a new state and schedule a new PC if
  // there is a change.  If RET_SEMANTICS is true, then we are merging
  // from a `ret' instruction into the instruction after a `jsr'.  This
  // is a special case with its own modified semantics.
  void push_jump_merge (int npc, state *nstate, bool ret_semantics = false)
  {
    bool changed = true;
    if (states[npc] == NULL)
      {
	// FIXME: what if we reach this code from a `ret'?
	
	states[npc] = new state (nstate, current_method->max_stack,
				 current_method->max_locals);
      }
    else
      changed = nstate->merge (states[npc], ret_semantics,
			       current_method->max_stack);

    if (changed && states[npc]->next == state::INVALID)
      {
	// The merge changed the state, and the new PC isn't yet on our
	// list of PCs to re-verify.
	states[npc]->next = next_verify_pc;
	next_verify_pc = npc;
      }
  }

  void push_jump (int offset)
  {
    int npc = compute_jump (offset);
    if (npc < PC)
      current_state->check_no_uninitialized_objects (current_method->max_stack);
    push_jump_merge (npc, current_state);
  }

  void push_exception_jump (type t, int pc)
  {
    current_state->check_no_uninitialized_objects (current_method->max_stack,
						  true);
    state s (current_state, current_method->max_stack,
	     current_method->max_locals);
    s.set_exception (t, current_method->max_stack);
    push_jump_merge (pc, &s);
  }

  int pop_jump ()
  {
    int npc = next_verify_pc;
    if (npc != state::NO_NEXT)
      {
	next_verify_pc = states[npc]->next;
	states[npc]->next = state::INVALID;
      }
    return npc;
  }

  void invalidate_pc ()
  {
    PC = state::NO_NEXT;
  }

  void note_branch_target (int pc, bool is_jsr_target = false)
  {
    if (pc <= PC && ! (flags[pc] & FLAG_INSN_START))
      verify_fail ("branch not to instruction start");
    flags[pc] |= FLAG_BRANCH_TARGET;
    if (is_jsr_target)
      {
	// Record the jsr which called this instruction.
	subr_info *info = (subr_info *) _Jv_Malloc (sizeof (subr_info));
	info->pc = PC;
	info->next = jsr_ptrs[pc];
	jsr_ptrs[pc] = info;
	flags[pc] |= FLAG_JSR_TARGET;
      }
  }

  void skip_padding ()
  {
    while ((PC % 4) > 0)
      if (get_byte () != 0)
	verify_fail ("found nonzero padding byte");
  }

  // Return the subroutine to which the instruction at PC belongs.
  int get_subroutine (int pc)
  {
    if (states[pc] == NULL)
      return 0;
    return states[pc]->subroutine;
  }

  // Do the work for a `ret' instruction.  INDEX is the index into the
  // local variables.
  void handle_ret_insn (int index)
  {
    get_variable (index, return_address_type);

    int csub = current_state->subroutine;
    if (csub == 0)
      verify_fail ("no subroutine");

    for (subr_info *subr = jsr_ptrs[csub]; subr != NULL; subr = subr->next)
      {
	// Temporarily modify the current state so it looks like we're
	// in the enclosing context.
	current_state->subroutine = get_subroutine (subr->pc);
	if (subr->pc < PC)
	  current_state->check_no_uninitialized_objects (current_method->max_stack);
	push_jump_merge (subr->pc, current_state, true);
      }

    current_state->subroutine = csub;
    invalidate_pc ();
  }

  // We're in the subroutine SUB, calling a subroutine at DEST.  Make
  // sure this subroutine isn't already on the stack.
  void check_nonrecursive_call (int sub, int dest)
  {
    if (sub == 0)
      return;
    if (sub == dest)
      verify_fail ("recursive subroutine call");
    for (subr_info *info = jsr_ptrs[sub]; info != NULL; info = info->next)
      check_nonrecursive_call (get_subroutine (info->pc), dest);
  }

  void handle_jsr_insn (int offset)
  {
    int npc = compute_jump (offset);

    if (npc < PC)
      current_state->check_no_uninitialized_objects (current_method->max_stack);
    check_nonrecursive_call (current_state->subroutine, npc);

    // Temporarily modify the current state so that it looks like we are
    // in the subroutine.
    push_type (return_address_type);
    int save = current_state->subroutine;
    current_state->subroutine = npc;

    // Merge into the subroutine.
    push_jump_merge (npc, current_state);

    // Undo our modifications.
    current_state->subroutine = save;
    pop_type (return_address_type);
  }

  jclass construct_primitive_array_type (type_val prim)
  {
    jclass k = NULL;
    switch (prim)
      {
      case boolean_type:
	k = JvPrimClass (boolean);
	break;
      case char_type:
	k = JvPrimClass (char);
	break;
      case float_type:
	k = JvPrimClass (float);
	break;
      case double_type:
	k = JvPrimClass (double);
	break;
      case byte_type:
	k = JvPrimClass (byte);
	break;
      case short_type:
	k = JvPrimClass (short);
	break;
      case int_type:
	k = JvPrimClass (int);
	break;
      case long_type:
	k = JvPrimClass (long);
	break;
      default:
	verify_fail ("unknown type in construct_primitive_array_type");
      }
    k = _Jv_GetArrayClass (k, NULL);
    return k;
  }

  // This pass computes the location of branch targets and also
  // instruction starts.
  void branch_prepass ()
  {
    flags = (char *) _Jv_Malloc (current_method->code_length);
    jsr_ptrs = (subr_info **) _Jv_Malloc (sizeof (subr_info *)
					  * current_method->code_length);

    for (int i = 0; i < current_method->code_length; ++i)
      {
	flags[i] = 0;
	jsr_ptrs[i] = NULL;
      }

    bool last_was_jsr = false;

    PC = 0;
    while (PC < current_method->code_length)
      {
	flags[PC] |= FLAG_INSN_START;

	// If the previous instruction was a jsr, then the next
	// instruction is a branch target -- the branch being the
	// corresponding `ret'.
	if (last_was_jsr)
	  note_branch_target (PC);
	last_was_jsr = false;

	start_PC = PC;
	unsigned char opcode = bytecode[PC++];
	switch (opcode)
	  {
	  case op_nop:
	  case op_aconst_null:
	  case op_iconst_m1:
	  case op_iconst_0:
	  case op_iconst_1:
	  case op_iconst_2:
	  case op_iconst_3:
	  case op_iconst_4:
	  case op_iconst_5:
	  case op_lconst_0:
	  case op_lconst_1:
	  case op_fconst_0:
	  case op_fconst_1:
	  case op_fconst_2:
	  case op_dconst_0:
	  case op_dconst_1:
	  case op_iload_0:
	  case op_iload_1:
	  case op_iload_2:
	  case op_iload_3:
	  case op_lload_0:
	  case op_lload_1:
	  case op_lload_2:
	  case op_lload_3:
	  case op_fload_0:
	  case op_fload_1:
	  case op_fload_2:
	  case op_fload_3:
	  case op_dload_0:
	  case op_dload_1:
	  case op_dload_2:
	  case op_dload_3:
	  case op_aload_0:
	  case op_aload_1:
	  case op_aload_2:
	  case op_aload_3:
	  case op_iaload:
	  case op_laload:
	  case op_faload:
	  case op_daload:
	  case op_aaload:
	  case op_baload:
	  case op_caload:
	  case op_saload:
	  case op_istore_0:
	  case op_istore_1:
	  case op_istore_2:
	  case op_istore_3:
	  case op_lstore_0:
	  case op_lstore_1:
	  case op_lstore_2:
	  case op_lstore_3:
	  case op_fstore_0:
	  case op_fstore_1:
	  case op_fstore_2:
	  case op_fstore_3:
	  case op_dstore_0:
	  case op_dstore_1:
	  case op_dstore_2:
	  case op_dstore_3:
	  case op_astore_0:
	  case op_astore_1:
	  case op_astore_2:
	  case op_astore_3:
	  case op_iastore:
	  case op_lastore:
	  case op_fastore:
	  case op_dastore:
	  case op_aastore:
	  case op_bastore:
	  case op_castore:
	  case op_sastore:
	  case op_pop:
	  case op_pop2:
	  case op_dup:
	  case op_dup_x1:
	  case op_dup_x2:
	  case op_dup2:
	  case op_dup2_x1:
	  case op_dup2_x2:
	  case op_swap:
	  case op_iadd:
	  case op_isub:
	  case op_imul:
	  case op_idiv:
	  case op_irem:
	  case op_ishl:
	  case op_ishr:
	  case op_iushr:
	  case op_iand:
	  case op_ior:
	  case op_ixor:
	  case op_ladd:
	  case op_lsub:
	  case op_lmul:
	  case op_ldiv:
	  case op_lrem:
	  case op_lshl:
	  case op_lshr:
	  case op_lushr:
	  case op_land:
	  case op_lor:
	  case op_lxor:
	  case op_fadd:
	  case op_fsub:
	  case op_fmul:
	  case op_fdiv:
	  case op_frem:
	  case op_dadd:
	  case op_dsub:
	  case op_dmul:
	  case op_ddiv:
	  case op_drem:
	  case op_ineg:
	  case op_i2b:
	  case op_i2c:
	  case op_i2s:
	  case op_lneg:
	  case op_fneg:
	  case op_dneg:
	  case op_iinc:
	  case op_i2l:
	  case op_i2f:
	  case op_i2d:
	  case op_l2i:
	  case op_l2f:
	  case op_l2d:
	  case op_f2i:
	  case op_f2l:
	  case op_f2d:
	  case op_d2i:
	  case op_d2l:
	  case op_d2f:
	  case op_lcmp:
	  case op_fcmpl:
	  case op_fcmpg:
	  case op_dcmpl:
	  case op_dcmpg:
	  case op_monitorenter:
	  case op_monitorexit:
	  case op_ireturn:
	  case op_lreturn:
	  case op_freturn:
	  case op_dreturn:
	  case op_areturn:
	  case op_return:
	  case op_athrow:
	    break;

	  case op_bipush:
	  case op_sipush:
	  case op_ldc:
	  case op_iload:
	  case op_lload:
	  case op_fload:
	  case op_dload:
	  case op_aload:
	  case op_istore:
	  case op_lstore:
	  case op_fstore:
	  case op_dstore:
	  case op_astore:
	  case op_arraylength:
	  case op_ret:
	    get_byte ();
	    break;

	  case op_ldc_w:
	  case op_ldc2_w:
	  case op_getstatic:
	  case op_getfield:
	  case op_putfield:
	  case op_putstatic:
	  case op_new:
	  case op_newarray:
	  case op_anewarray:
	  case op_instanceof:
	  case op_checkcast:
	  case op_invokespecial:
	  case op_invokestatic:
	  case op_invokevirtual:
	    get_short ();
	    break;

	  case op_multianewarray:
	    get_short ();
	    get_byte ();
	    break;

	  case op_jsr:
	    last_was_jsr = true;
	    // Fall through.
	  case op_ifeq:
	  case op_ifne:
	  case op_iflt:
	  case op_ifge:
	  case op_ifgt:
	  case op_ifle:
	  case op_if_icmpeq:
	  case op_if_icmpne:
	  case op_if_icmplt:
	  case op_if_icmpge:
	  case op_if_icmpgt:
	  case op_if_icmple:
	  case op_if_acmpeq:
	  case op_if_acmpne:
	  case op_ifnull:
	  case op_ifnonnull:
	  case op_goto:
	    note_branch_target (compute_jump (get_short ()), last_was_jsr);
	    break;

	  case op_tableswitch:
	    {
	      skip_padding ();
	      note_branch_target (compute_jump (get_int ()));
	      jint low = get_int ();
	      jint hi = get_int ();
	      if (low > hi)
		verify_fail ("invalid tableswitch");
	      for (int i = low; i <= hi; ++i)
		note_branch_target (compute_jump (get_int ()));
	    }
	    break;

	  case op_lookupswitch:
	    {
	      skip_padding ();
	      note_branch_target (compute_jump (get_int ()));
	      int npairs = get_int ();
	      if (npairs < 0)
		verify_fail ("too few pairs in lookupswitch");
	      while (npairs-- > 0)
		{
		  get_int ();
		  note_branch_target (compute_jump (get_int ()));
		}
	    }
	    break;

	  case op_invokeinterface:
	    get_short ();
	    get_byte ();
	    get_byte ();
	    break;

	  case op_wide:
	    {
	      opcode = get_byte ();
	      get_short ();
	      if (opcode == (unsigned char) op_iinc)
		get_short ();
	    }
	    break;

	  case op_jsr_w:
	    last_was_jsr = true;
	    // Fall through.
	  case op_goto_w:
	    note_branch_target (compute_jump (get_int ()), last_was_jsr);
	    break;

	  default:
	    verify_fail ("unrecognized instruction in branch_prepass");
	  }

	// See if any previous branch tried to branch to the middle of
	// this instruction.
	for (int pc = start_PC + 1; pc < PC; ++pc)
	  {
	    if ((flags[pc] & FLAG_BRANCH_TARGET))
	      verify_fail ("branch not to instruction start");
	  }
      }

    // Verify exception handlers.
    for (int i = 0; i < current_method->exc_count; ++i)
      {
	if (! (flags[exception[i].handler_pc] & FLAG_INSN_START))
	  verify_fail ("exception handler not at instruction start");
	if (exception[i].start_pc > exception[i].end_pc)
	  verify_fail ("exception range inverted");
	if (! (flags[exception[i].start_pc] & FLAG_INSN_START)
	    || ! (flags[exception[i].start_pc] & FLAG_INSN_START))
	  verify_fail ("exception endpoint not at instruction start");

	flags[exception[i].handler_pc] |= FLAG_BRANCH_TARGET;
      }
  }

  void check_pool_index (int index)
  {
    if (index < 0 || index >= current_class->constants.size)
      verify_fail ("constant pool index out of range");
  }

  type check_class_constant (int index)
  {
    check_pool_index (index);
    _Jv_Constants *pool = &current_class->constants;
    if (pool->tags[index] == JV_CONSTANT_ResolvedClass)
      return type (pool->data[index].clazz);
    else if (pool->tags[index] == JV_CONSTANT_Class)
      return type (pool->data[index].utf8);
    verify_fail ("expected class constant");
  }

  type check_constant (int index)
  {
    check_pool_index (index);
    _Jv_Constants *pool = &current_class->constants;
    if (pool->tags[index] == JV_CONSTANT_ResolvedString
	|| pool->tags[index] == JV_CONSTANT_String)
      return type (&java::lang::String::class$);
    else if (pool->tags[index] == JV_CONSTANT_Integer)
      return type (int_type);
    else if (pool->tags[index] == JV_CONSTANT_Float)
      return type (float_type);
    verify_fail ("String, int, or float constant expected");
  }

  // Helper for both field and method.  These are laid out the same in
  // the constant pool.
  type handle_field_or_method (int index, int expected,
			       _Jv_Utf8Const **name,
			       _Jv_Utf8Const **fmtype)
  {
    check_pool_index (index);
    _Jv_Constants *pool = &current_class->constants;
    if (pool->tags[index] != expected)
      verify_fail ("didn't see expected constant");
    // Once we know we have a Fieldref or Methodref we assume that it
    // is correctly laid out in the constant pool.  I think the code
    // in defineclass.cc guarantees this.
    _Jv_ushort class_index, name_and_type_index;
    _Jv_loadIndexes (&pool->data[index],
		     class_index,
		     name_and_type_index);
    _Jv_ushort name_index, desc_index;
    _Jv_loadIndexes (&pool->data[name_and_type_index],
		     name_index, desc_index);

    *name = pool->data[name_index].utf8;
    *fmtype = pool->data[desc_index].utf8;

    return check_class_constant (class_index);
  }

  // Return field's type, compute class' type if requested.
  type check_field_constant (int index, type *class_type = NULL)
  {
    _Jv_Utf8Const *name, *field_type;
    type ct = handle_field_or_method (index,
				      JV_CONSTANT_Fieldref,
				      &name, &field_type);
    if (class_type)
      *class_type = ct;
    return type (field_type);
  }

  type check_method_constant (int index, bool is_interface,
			      _Jv_Utf8Const **method_name,
			      _Jv_Utf8Const **method_signature)
  {
    return handle_field_or_method (index,
				   (is_interface
				    ? JV_CONSTANT_InterfaceMethodref
				    : JV_CONSTANT_Methodref),
				   method_name, method_signature);
  }

  type get_one_type (char *&p)
  {
    char *start = p;

    int arraycount = 0;
    while (*p == '[')
      {
	++arraycount;
	++p;
      }

    char v = *p++;

    if (v == 'L')
      {
	while (*p != ';')
	  ++p;
	++p;
	// FIXME!  This will get collected!
	_Jv_Utf8Const *name = _Jv_makeUtf8Const (start, p - start);
	return type (name);
      }

    // Casting to jchar here is ok since we are looking at an ASCII
    // character.
    type_val rt = get_type_val_for_signature (jchar (v));

    if (arraycount == 0)
      return type (rt);

    jclass k = construct_primitive_array_type (rt);
    while (--arraycount > 0)
      k = _Jv_GetArrayClass (k, NULL);
    return type (k);
  }

  void compute_argument_types (_Jv_Utf8Const *signature,
			       type *types)
  {
    char *p = signature->data;
    // Skip `('.
    ++p;

    int i = 0;
    while (*p != ')')
      types[i++] = get_one_type (p);
  }

  type compute_return_type (_Jv_Utf8Const *signature)
  {
    char *p = signature->data;
    while (*p != ')')
      ++p;
    ++p;
    return get_one_type (p);
  }

  void check_return_type (type expected)
  {
    type rt = compute_return_type (current_method->self->signature);
    if (! expected.compatible (rt))
      verify_fail ("incompatible return type");
  }

  void verify_instructions_0 ()
  {
    current_state = new state (current_method->max_stack,
			       current_method->max_locals);

    PC = 0;

    {
      int var = 0;

      using namespace java::lang::reflect;
      if (! Modifier::isStatic (current_method->self->accflags))
	{
	  type kurr (current_class);
	  if (_Jv_equalUtf8Consts (current_method->self->name, gcj::init_name))
	    kurr.set_uninitialized (type::SELF);
	  set_variable (0, kurr);
	  ++var;
	}

      if (var + _Jv_count_arguments (current_method->self->signature)
	  > current_method->max_locals)
	verify_fail ("too many arguments");
      compute_argument_types (current_method->self->signature,
			      &current_state->locals[var]);
    }

    states = (state **) _Jv_Malloc (sizeof (state *)
				    * current_method->code_length);
    for (int i = 0; i < current_method->code_length; ++i)
      states[i] = NULL;

    next_verify_pc = state::NO_NEXT;

    while (true)
      {
	// If the PC was invalidated, get a new one from the work list.
	if (PC == state::NO_NEXT)
	  {
	    PC = pop_jump ();
	    if (PC == state::INVALID)
	      verify_fail ("saw state::INVALID");
	    if (PC == state::NO_NEXT)
	      break;
	    // Set up the current state.
	    *current_state = *states[PC];
	  }

	// Control can't fall off the end of the bytecode.
	if (PC >= current_method->code_length)
	  verify_fail ("fell off end");

	if (states[PC] != NULL)
	  {
	    // We've already visited this instruction.  So merge the
	    // states together.  If this yields no change then we don't
	    // have to re-verify.
	    if (! current_state->merge (states[PC], false,
					current_method->max_stack))
	      {
		invalidate_pc ();
		continue;
	      }
	    // Save a copy of it for later.
	    states[PC]->copy (current_state, current_method->max_stack,
			      current_method->max_locals);
	  }
	else if ((flags[PC] & FLAG_BRANCH_TARGET))
	  {
	    // We only have to keep saved state at branch targets.
	    states[PC] = new state (current_state, current_method->max_stack,
				    current_method->max_locals);
	  }

	// Update states for all active exception handlers.  Ordinarily
	// there are not many exception handlers.  So we simply run
	// through them all.
	for (int i = 0; i < current_method->exc_count; ++i)
	  {
	    if (PC >= exception[i].start_pc && PC < exception[i].end_pc)
	      {
		type handler = reference_type;
		if (exception[i].handler_type != 0)
		  handler = check_class_constant (exception[i].handler_type);
		push_exception_jump (handler, exception[i].handler_pc);
	      }
	  }

	start_PC = PC;
	unsigned char opcode = bytecode[PC++];
	switch (opcode)
	  {
	  case op_nop:
	    break;

	  case op_aconst_null:
	    push_type (null_type);
	    break;

	  case op_iconst_m1:
	  case op_iconst_0:
	  case op_iconst_1:
	  case op_iconst_2:
	  case op_iconst_3:
	  case op_iconst_4:
	  case op_iconst_5:
	    push_type (int_type);
	    break;

	  case op_lconst_0:
	  case op_lconst_1:
	    push_type (long_type);
	    break;

	  case op_fconst_0:
	  case op_fconst_1:
	  case op_fconst_2:
	    push_type (float_type);
	    break;

	  case op_dconst_0:
	  case op_dconst_1:
	    push_type (double_type);
	    break;

	  case op_bipush:
	    get_byte ();
	    push_type (int_type);
	    break;

	  case op_sipush:
	    get_short ();
	    push_type (int_type);
	    break;

	  case op_ldc:
	    push_type (check_constant (get_byte ()));
	    break;
	  case op_ldc_w:
	    push_type (check_constant (get_ushort ()));
	    break;
	  case op_ldc2_w:
	    push_type (check_constant (get_ushort ()));
	    break;

	  case op_iload:
	    push_type (get_variable (get_byte (), int_type));
	    break;
	  case op_lload:
	    push_type (get_variable (get_byte (), long_type));
	    break;
	  case op_fload:
	    push_type (get_variable (get_byte (), float_type));
	    break;
	  case op_dload:
	    push_type (get_variable (get_byte (), double_type));
	    break;
	  case op_aload:
	    push_type (get_variable (get_byte (), reference_type));
	    break;

	  case op_iload_0:
	  case op_iload_1:
	  case op_iload_2:
	  case op_iload_3:
	    push_type (get_variable (opcode - op_iload_0, int_type));
	    break;
	  case op_lload_0:
	  case op_lload_1:
	  case op_lload_2:
	  case op_lload_3:
	    push_type (get_variable (opcode - op_lload_0, long_type));
	    break;
	  case op_fload_0:
	  case op_fload_1:
	  case op_fload_2:
	  case op_fload_3:
	    push_type (get_variable (opcode - op_fload_0, float_type));
	    break;
	  case op_dload_0:
	  case op_dload_1:
	  case op_dload_2:
	  case op_dload_3:
	    push_type (get_variable (opcode - op_dload_0, double_type));
	    break;
	  case op_aload_0:
	  case op_aload_1:
	  case op_aload_2:
	  case op_aload_3:
	    push_type (get_variable (opcode - op_aload_0, reference_type));
	    break;
	  case op_iaload:
	    pop_type (int_type);
	    push_type (require_array_type (pop_type (reference_type),
					   int_type));
	    break;
	  case op_laload:
	    pop_type (int_type);
	    push_type (require_array_type (pop_type (reference_type),
					   long_type));
	    break;
	  case op_faload:
	    pop_type (int_type);
	    push_type (require_array_type (pop_type (reference_type),
					   float_type));
	    break;
	  case op_daload:
	    pop_type (int_type);
	    push_type (require_array_type (pop_type (reference_type),
					   double_type));
	    break;
	  case op_aaload:
	    pop_type (int_type);
	    push_type (require_array_type (pop_type (reference_type),
					   reference_type));
	    break;
	  case op_baload:
	    pop_type (int_type);
	    require_array_type (pop_type (reference_type), byte_type);
	    push_type (int_type);
	    break;
	  case op_caload:
	    pop_type (int_type);
	    require_array_type (pop_type (reference_type), char_type);
	    push_type (int_type);
	    break;
	  case op_saload:
	    pop_type (int_type);
	    require_array_type (pop_type (reference_type), short_type);
	    push_type (int_type);
	    break;
	  case op_istore:
	    set_variable (get_byte (), pop_type (int_type));
	    break;
	  case op_lstore:
	    set_variable (get_byte (), pop_type (long_type));
	    break;
	  case op_fstore:
	    set_variable (get_byte (), pop_type (float_type));
	    break;
	  case op_dstore:
	    set_variable (get_byte (), pop_type (double_type));
	    break;
	  case op_astore:
	    set_variable (get_byte (), pop_type (reference_type));
	    break;
	  case op_istore_0:
	  case op_istore_1:
	  case op_istore_2:
	  case op_istore_3:
	    set_variable (opcode - op_istore_0, pop_type (int_type));
	    break;
	  case op_lstore_0:
	  case op_lstore_1:
	  case op_lstore_2:
	  case op_lstore_3:
	    set_variable (opcode - op_lstore_0, pop_type (long_type));
	    break;
	  case op_fstore_0:
	  case op_fstore_1:
	  case op_fstore_2:
	  case op_fstore_3:
	    set_variable (opcode - op_fstore_0, pop_type (float_type));
	    break;
	  case op_dstore_0:
	  case op_dstore_1:
	  case op_dstore_2:
	  case op_dstore_3:
	    set_variable (opcode - op_dstore_0, pop_type (double_type));
	    break;
	  case op_astore_0:
	  case op_astore_1:
	  case op_astore_2:
	  case op_astore_3:
	    set_variable (opcode - op_astore_0, pop_type (reference_type));
	    break;
	  case op_iastore:
	    pop_type (int_type);
	    pop_type (int_type);
	    require_array_type (pop_type (reference_type), int_type);
	    break;
	  case op_lastore:
	    pop_type (long_type);
	    pop_type (int_type);
	    require_array_type (pop_type (reference_type), long_type);
	    break;
	  case op_fastore:
	    pop_type (float_type);
	    pop_type (int_type);
	    require_array_type (pop_type (reference_type), float_type);
	    break;
	  case op_dastore:
	    pop_type (double_type);
	    pop_type (int_type);
	    require_array_type (pop_type (reference_type), double_type);
	    break;
	  case op_aastore:
	    pop_type (reference_type);
	    pop_type (int_type);
	    require_array_type (pop_type (reference_type), reference_type);
	    break;
	  case op_bastore:
	    pop_type (int_type);
	    pop_type (int_type);
	    require_array_type (pop_type (reference_type), byte_type);
	    break;
	  case op_castore:
	    pop_type (int_type);
	    pop_type (int_type);
	    require_array_type (pop_type (reference_type), char_type);
	    break;
	  case op_sastore:
	    pop_type (int_type);
	    pop_type (int_type);
	    require_array_type (pop_type (reference_type), short_type);
	    break;
	  case op_pop:
	    pop32 ();
	    break;
	  case op_pop2:
	    pop64 ();
	    break;
	  case op_dup:
	    {
	      type t = pop32 ();
	      push_type (t);
	      push_type (t);
	    }
	    break;
	  case op_dup_x1:
	    {
	      type t1 = pop32 ();
	      type t2 = pop32 ();
	      push_type (t1);
	      push_type (t2);
	      push_type (t1);
	    }
	    break;
	  case op_dup_x2:
	    {
	      type t1 = pop32 ();
	      type t2 = pop_raw ();
	      if (! t2.iswide ())
		{
		  type t3 = pop32 ();
		  push_type (t1);
		  push_type (t3);
		}
	      else
		push_type (t1);
	      push_type (t2);
	      push_type (t1);
	    }
	    break;
	  case op_dup2:
	    {
	      type t = pop_raw ();
	      if (! t.iswide ())
		{
		  type t2 = pop32 ();
		  push_type (t2);
		  push_type (t);
		  push_type (t2);
		}
	      push_type (t);
	    }
	    break;
	  case op_dup2_x1:
	    {
	      type t1 = pop_raw ();
	      type t2 = pop32 ();
	      if (! t1.iswide ())
		{
		  type t3 = pop32 ();
		  push_type (t2);
		  push_type (t1);
		  push_type (t3);
		}
	      else
		push_type (t1);
	      push_type (t2);
	      push_type (t1);
	    }
	    break;
	  case op_dup2_x2:
	    {
	      // FIXME
	      type t1 = pop_raw ();
	      if (t1.iswide ())
		{
		  type t2 = pop_raw ();
		  if (t2.iswide ())
		    {
		      push_type (t1);
		      push_type (t2);
		    }
		  else
		    {
		      type t3 = pop32 ();
		      push_type (t1);
		      push_type (t3);
		      push_type (t2);
		    }
		  push_type (t1);
		}
	      else
		{
		  type t2 = pop32 ();
		  type t3 = pop_raw ();
		  if (t3.iswide ())
		    {
		      push_type (t2);
		      push_type (t1);
		    }
		  else
		    {
		      type t4 = pop32 ();
		      push_type (t2);
		      push_type (t1);
		      push_type (t4);
		    }
		  push_type (t3);
		  push_type (t2);
		  push_type (t1);
		}
	    }
	    break;
	  case op_swap:
	    {
	      type t1 = pop32 ();
	      type t2 = pop32 ();
	      push_type (t1);
	      push_type (t2);
	    }
	    break;
	  case op_iadd:
	  case op_isub:
	  case op_imul:
	  case op_idiv:
	  case op_irem:
	  case op_ishl:
	  case op_ishr:
	  case op_iushr:
	  case op_iand:
	  case op_ior:
	  case op_ixor:
	    pop_type (int_type);
	    push_type (pop_type (int_type));
	    break;
	  case op_ladd:
	  case op_lsub:
	  case op_lmul:
	  case op_ldiv:
	  case op_lrem:
	  case op_lshl:
	  case op_lshr:
	  case op_lushr:
	  case op_land:
	  case op_lor:
	  case op_lxor:
	    pop_type (long_type);
	    push_type (pop_type (long_type));
	    break;
	  case op_fadd:
	  case op_fsub:
	  case op_fmul:
	  case op_fdiv:
	  case op_frem:
	    pop_type (float_type);
	    push_type (pop_type (float_type));
	    break;
	  case op_dadd:
	  case op_dsub:
	  case op_dmul:
	  case op_ddiv:
	  case op_drem:
	    pop_type (double_type);
	    push_type (pop_type (double_type));
	    break;
	  case op_ineg:
	  case op_i2b:
	  case op_i2c:
	  case op_i2s:
	    push_type (pop_type (int_type));
	    break;
	  case op_lneg:
	    push_type (pop_type (long_type));
	    break;
	  case op_fneg:
	    push_type (pop_type (float_type));
	    break;
	  case op_dneg:
	    push_type (pop_type (double_type));
	    break;
	  case op_iinc:
	    get_variable (get_byte (), int_type);
	    get_byte ();
	    break;
	  case op_i2l:
	    pop_type (int_type);
	    push_type (long_type);
	    break;
	  case op_i2f:
	    pop_type (int_type);
	    push_type (float_type);
	    break;
	  case op_i2d:
	    pop_type (int_type);
	    push_type (double_type);
	    break;
	  case op_l2i:
	    pop_type (long_type);
	    push_type (int_type);
	    break;
	  case op_l2f:
	    pop_type (long_type);
	    push_type (float_type);
	    break;
	  case op_l2d:
	    pop_type (long_type);
	    push_type (double_type);
	    break;
	  case op_f2i:
	    pop_type (float_type);
	    push_type (int_type);
	    break;
	  case op_f2l:
	    pop_type (float_type);
	    push_type (long_type);
	    break;
	  case op_f2d:
	    pop_type (float_type);
	    push_type (double_type);
	    break;
	  case op_d2i:
	    pop_type (double_type);
	    push_type (int_type);
	    break;
	  case op_d2l:
	    pop_type (double_type);
	    push_type (long_type);
	    break;
	  case op_d2f:
	    pop_type (double_type);
	    push_type (float_type);
	    break;
	  case op_lcmp:
	    pop_type (long_type);
	    pop_type (long_type);
	    push_type (int_type);
	    break;
	  case op_fcmpl:
	  case op_fcmpg:
	    pop_type (float_type);
	    pop_type (float_type);
	    push_type (int_type);
	    break;
	  case op_dcmpl:
	  case op_dcmpg:
	    pop_type (double_type);
	    pop_type (double_type);
	    push_type (int_type);
	    break;
	  case op_ifeq:
	  case op_ifne:
	  case op_iflt:
	  case op_ifge:
	  case op_ifgt:
	  case op_ifle:
	    pop_type (int_type);
	    push_jump (get_short ());
	    break;
	  case op_if_icmpeq:
	  case op_if_icmpne:
	  case op_if_icmplt:
	  case op_if_icmpge:
	  case op_if_icmpgt:
	  case op_if_icmple:
	    pop_type (int_type);
	    pop_type (int_type);
	    push_jump (get_short ());
	    break;
	  case op_if_acmpeq:
	  case op_if_acmpne:
	    pop_type (reference_type);
	    pop_type (reference_type);
	    push_jump (get_short ());
	    break;
	  case op_goto:
	    push_jump (get_short ());
	    invalidate_pc ();
	    break;
	  case op_jsr:
	    handle_jsr_insn (get_short ());
	    break;
	  case op_ret:
	    handle_ret_insn (get_byte ());
	    break;
	  case op_tableswitch:
	    {
	      pop_type (int_type);
	      skip_padding ();
	      push_jump (get_int ());
	      jint low = get_int ();
	      jint high = get_int ();
	      // Already checked LOW -vs- HIGH.
	      for (int i = low; i <= high; ++i)
		push_jump (get_int ());
	      invalidate_pc ();
	    }
	    break;

	  case op_lookupswitch:
	    {
	      pop_type (int_type);
	      skip_padding ();
	      push_jump (get_int ());
	      jint npairs = get_int ();
	      // Already checked NPAIRS >= 0.
	      jint lastkey = 0;
	      for (int i = 0; i < npairs; ++i)
		{
		  jint key = get_int ();
		  if (i > 0 && key <= lastkey)
		    verify_fail ("lookupswitch pairs unsorted");
		  lastkey = key;
		  push_jump (get_int ());
		}
	      invalidate_pc ();
	    }
	    break;
	  case op_ireturn:
	    check_return_type (pop_type (int_type));
	    invalidate_pc ();
	    break;
	  case op_lreturn:
	    check_return_type (pop_type (long_type));
	    invalidate_pc ();
	    break;
	  case op_freturn:
	    check_return_type (pop_type (float_type));
	    invalidate_pc ();
	    break;
	  case op_dreturn:
	    check_return_type (pop_type (double_type));
	    invalidate_pc ();
	    break;
	  case op_areturn:
	    check_return_type (pop_type (reference_type));
	    invalidate_pc ();
	    break;
	  case op_return:
	    check_return_type (void_type);
	    invalidate_pc ();
	    break;
	  case op_getstatic:
	    push_type (check_field_constant (get_ushort ()));
	    break;
	  case op_putstatic:
	    pop_type (check_field_constant (get_ushort ()));
	    break;
	  case op_getfield:
	    {
	      type klass;
	      type field = check_field_constant (get_ushort (), &klass);
	      pop_type (klass);
	      push_type (field);
	    }
	    break;
	  case op_putfield:
	    {
	      type klass;
	      type field = check_field_constant (get_ushort (), &klass);
	      pop_type (field);
	      pop_type (klass);
	    }
	    break;

	  case op_invokevirtual:
	  case op_invokespecial:
	  case op_invokestatic:
	  case op_invokeinterface:
	    {
	      _Jv_Utf8Const *method_name, *method_signature;
	      type class_type
		= check_method_constant (get_ushort (),
					 opcode == (unsigned char) op_invokeinterface,
					 &method_name,
					 &method_signature);
	      int arg_count = _Jv_count_arguments (method_signature);
	      if (opcode == (unsigned char) op_invokeinterface)
		{
		  int nargs = get_byte ();
		  if (nargs == 0)
		    verify_fail ("too few arguments to invokeinterface");
		  if (get_byte () != 0)
		    verify_fail ("invokeinterface dummy byte is wrong");
		  if (nargs - 1 != arg_count)
		    verify_fail ("wrong argument count for invokeinterface");
		}

	      bool is_init = false;
	      if (_Jv_equalUtf8Consts (method_name, gcj::init_name))
		{
		  is_init = true;
		  if (opcode != (unsigned char) op_invokespecial)
		    verify_fail ("can't invoke <init>");
		}
	      else if (method_name->data[0] == '<')
		verify_fail ("can't invoke method starting with `<'");

	      // Pop arguments and check types.
	      type arg_types[arg_count];
	      compute_argument_types (method_signature, arg_types);
	      for (int i = arg_count - 1; i >= 0; --i)
		pop_type (arg_types[i]);

	      if (opcode != (unsigned char) op_invokestatic)
		{
		  type t = class_type;
		  if (is_init)
		    {
		      // In this case the PC doesn't matter.
		      t.set_uninitialized (type::UNINIT);
		    }
		  t = pop_type (t);
		  if (is_init)
		    current_state->set_initialized (t.get_pc (),
						    current_method->max_locals);
		}

	      type rt = compute_return_type (method_signature);
	      if (! rt.isvoid ())
		push_type (rt);
	    }
	    break;

	  case op_new:
	    {
	      type t = check_class_constant (get_ushort ());
	      if (t.isarray () || t.isinterface () || t.isabstract ())
		verify_fail ("type is array, interface, or abstract");
	      t.set_uninitialized (start_PC);
	      push_type (t);
	    }
	    break;

	  case op_newarray:
	    {
	      int atype = get_byte ();
	      // We intentionally have chosen constants to make this
	      // valid.
	      if (atype < boolean_type || atype > long_type)
		verify_fail ("type not primitive");
	      pop_type (int_type);
	      push_type (construct_primitive_array_type (type_val (atype)));
	    }
	    break;
	  case op_anewarray:
	    pop_type (int_type);
	    push_type (check_class_constant (get_ushort ()));
	    break;
	  case op_arraylength:
	    {
	      type t = pop_type (reference_type);
	      if (! t.isarray ())
		verify_fail ("array type expected");
	      push_type (int_type);
	    }
	    break;
	  case op_athrow:
	    pop_type (type (&java::lang::Throwable::class$));
	    invalidate_pc ();
	    break;
	  case op_checkcast:
	    pop_type (reference_type);
	    push_type (check_class_constant (get_ushort ()));
	    break;
	  case op_instanceof:
	    pop_type (reference_type);
	    check_class_constant (get_ushort ());
	    push_type (int_type);
	    break;
	  case op_monitorenter:
	    pop_type (reference_type);
	    break;
	  case op_monitorexit:
	    pop_type (reference_type);
	    break;
	  case op_wide:
	    {
	      switch (get_byte ())
		{
		case op_iload:
		  push_type (get_variable (get_ushort (), int_type));
		  break;
		case op_lload:
		  push_type (get_variable (get_ushort (), long_type));
		  break;
		case op_fload:
		  push_type (get_variable (get_ushort (), float_type));
		  break;
		case op_dload:
		  push_type (get_variable (get_ushort (), double_type));
		  break;
		case op_aload:
		  push_type (get_variable (get_ushort (), reference_type));
		  break;
		case op_istore:
		  set_variable (get_ushort (), pop_type (int_type));
		  break;
		case op_lstore:
		  set_variable (get_ushort (), pop_type (long_type));
		  break;
		case op_fstore:
		  set_variable (get_ushort (), pop_type (float_type));
		  break;
		case op_dstore:
		  set_variable (get_ushort (), pop_type (double_type));
		  break;
		case op_astore:
		  set_variable (get_ushort (), pop_type (reference_type));
		  break;
		case op_ret:
		  handle_ret_insn (get_short ());
		  break;
		case op_iinc:
		  get_variable (get_ushort (), int_type);
		  get_short ();
		  break;
		default:
		  verify_fail ("unrecognized wide instruction");
		}
	    }
	    break;
	  case op_multianewarray:
	    {
	      type atype = check_class_constant (get_ushort ());
	      int dim = get_byte ();
	      if (dim < 1)
		verify_fail ("too few dimensions to multianewarray");
	      atype.verify_dimensions (dim);
	      for (int i = 0; i < dim; ++i)
		pop_type (int_type);
	      push_type (atype);
	    }
	    break;
	  case op_ifnull:
	  case op_ifnonnull:
	    pop_type (reference_type);
	    push_jump (get_short ());
	    break;
	  case op_goto_w:
	    push_jump (get_int ());
	    invalidate_pc ();
	    break;
	  case op_jsr_w:
	    handle_jsr_insn (get_int ());
	    break;

	  default:
	    // Unrecognized opcode.
	    verify_fail ("unrecognized instruction in verify_instructions_0");
	  }
      }
  }

public:

  void verify_instructions ()
  {
    branch_prepass ();
    verify_instructions_0 ();
  }

  _Jv_BytecodeVerifier (_Jv_InterpMethod *m)
  {
    current_method = m;
    bytecode = m->bytecode ();
    exception = m->exceptions ();
    current_class = m->defining_class;

    states = NULL;
    flags = NULL;
    jsr_ptrs = NULL;
  }

  ~_Jv_BytecodeVerifier ()
  {
    if (states)
      _Jv_Free (states);
    if (flags)
      _Jv_Free (flags);
    if (jsr_ptrs)
      _Jv_Free (jsr_ptrs);
  }
};

void
_Jv_VerifyMethod (_Jv_InterpMethod *meth)
{
  _Jv_BytecodeVerifier v (meth);
  v.verify_instructions ();
}

// FIXME: add more info, like PC, when required.
static void
verify_fail (char *s)
{
  char buf[1024];
  strcpy (buf, "verification failed: ");
  strcat (buf, s);
  throw new java::lang::VerifyError (JvNewStringLatin1 (buf));
}

#endif	/* INTERPRETER */
