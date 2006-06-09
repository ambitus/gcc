/* gnu_java_awt_FreetypeGlyphVector.c
   Copyright (C) 2006  Free Software Foundation, Inc.

This file is part of GNU Classpath.

GNU Classpath is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU Classpath is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Classpath; see the file COPYING.  If not, write to the
Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301 USA.

Linking this library statically or dynamically with other modules is
making a combined work based on this library.  Thus, the terms and
conditions of the GNU General Public License cover the whole
combination.

As a special exception, the copyright holders of this library give you
permission to link this library with independent modules to produce an
executable, regardless of the license terms of these independent
modules, and to copy and distribute the resulting executable under
terms of your choice, provided that you also meet, for each linked
independent module, the terms and conditions of the license of that
module.  An independent module is a module which is not derived from
or based on this library.  If you modify this library, you may extend
this exception to your version of the library, but you are not
obligated to do so.  If you do not wish to do so, delete this
exception statement from your version. */

#include <jni.h>
#include <gtk/gtk.h>
#include <string.h>
#include <pango/pango.h>
#include <pango/pangoft2.h>
#include <pango/pangofc-font.h>
#include <freetype/ftglyph.h>
#include <freetype/ftoutln.h>
#include "native_state.h"
#include "gdkfont.h"
#include "gnu_java_awt_peer_gtk_FreetypeGlyphVector.h"
#include "cairographics2d.h"

typedef struct gp
{
  JNIEnv *env;
  jobject obj;
  double px;
  double py;
  double sx;
  double sy;
} generalpath ;

static PangoFcFont *
getFont(JNIEnv *env, jobject obj)
{
  jfieldID fid;
  jobject data;
  jclass cls;
  struct peerfont *pfont;

  cls = (*env)->GetObjectClass (env, obj);
  fid = (*env)->GetFieldID (env, cls, "peer", 
				 "Lgnu/java/awt/peer/gtk/GdkFontPeer;");
  g_assert (fid != 0);

  data = (*env)->GetObjectField (env, obj, fid);
  g_assert (data != NULL);

  pfont = (struct peerfont *)NSA_GET_FONT_PTR (env, data);
  g_assert (pfont != NULL);
  g_assert (pfont->font != NULL);

  return (PangoFcFont *)pfont->font;
}

JNIEXPORT jint JNICALL 
Java_gnu_java_awt_peer_gtk_FreetypeGlyphVector_getGlyph
  (JNIEnv *env, jobject obj, jint codepoint)
{
  FT_Face ft_face;
  jint glyph_index;
  PangoFcFont *font;

  font = getFont(env, obj);

  ft_face = pango_fc_font_lock_face( font );
  g_assert (ft_face != NULL);

  glyph_index = FT_Get_Char_Index( ft_face, codepoint );

  pango_fc_font_unlock_face (font);

  return glyph_index;
}

JNIEXPORT jobject JNICALL 
Java_gnu_java_awt_peer_gtk_FreetypeGlyphVector_getKerning
(JNIEnv *env, jobject obj, jint rightGlyph, jint leftGlyph)
{
  FT_Face ft_face;
  FT_Vector kern;
  jclass cls;
  jmethodID method;
  jvalue values[2];
  PangoFcFont *font;

  font = getFont(env, obj);
  ft_face = pango_fc_font_lock_face( font );
  g_assert (ft_face != NULL);
  FT_Get_Kerning( ft_face, rightGlyph, leftGlyph, FT_KERNING_DEFAULT, &kern );

  pango_fc_font_unlock_face( font );

  values[0].d = (jdouble)kern.x/64.0;
  values[1].d = (jdouble)kern.y/64.0;

  cls = (*env)->FindClass (env, "java/awt/geom/Point2D$Double");
  method = (*env)->GetMethodID (env, cls, "<init>", "(DD)V");
  return (*env)->NewObjectA(env, cls, method, values);
}

JNIEXPORT jdoubleArray JNICALL 
Java_gnu_java_awt_peer_gtk_FreetypeGlyphVector_getMetricsNative
(JNIEnv *env, jobject obj, jint glyphIndex )
{
  FT_Face ft_face;
  jdouble *values;
  jdoubleArray retArray = NULL;
  PangoFcFont *font;

  font = getFont(env, obj);
  ft_face = pango_fc_font_lock_face( font );

  g_assert (ft_face != NULL);

  FT_Set_Transform( ft_face, NULL, NULL );

  if( FT_Load_Glyph( ft_face, glyphIndex, FT_LOAD_DEFAULT ) != 0 )
    {
      pango_fc_font_unlock_face( font );
      printf("Couldn't load glyph %i\n", glyphIndex);
      return NULL;
    }

  retArray = (*env)->NewDoubleArray (env, 8);
  values = (*env)->GetDoubleArrayElements (env, retArray, NULL);

  values[0] = 0;
  values[1] = (jdouble)ft_face->glyph->advance.x/64.0;
  values[2] = (jdouble)ft_face->glyph->advance.y/64.0;
  values[3] = (jdouble)ft_face->glyph->metrics.horiBearingX/64.0;
  values[4] = -(jdouble)ft_face->glyph->metrics.horiBearingY/64.0;
  values[5] = (jdouble)ft_face->glyph->metrics.width/64.0;
  values[6] = (jdouble)ft_face->glyph->metrics.height/64.0;
  values[7] = 0;

  (*env)->ReleaseDoubleArrayElements (env, retArray, values, 0);
  pango_fc_font_unlock_face( font );

  return retArray;
}

/* GetOutline code follows ****************************/
/********* Freetype callback functions *****************************/

static int _moveTo( const FT_Vector* to,
		    void *p)
{
  JNIEnv *env;
  jobject obj;
  jclass cls;
  jmethodID method;
  jvalue values[2];
  generalpath *path = (generalpath *) p;

  env = path->env;
  obj = path->obj;

  values[0].f = (jfloat)(to->x * path->sx + path->px);
  values[1].f = (jfloat)(to->y * path->sy + path->py);

  cls = (*env)->FindClass (env, "java/awt/geom/GeneralPath");
  method = (*env)->GetMethodID (env, cls, "moveTo", "(FF)V");
  (*env)->CallVoidMethodA(env, obj, method, values );

  return 0;
}

static int _lineTo( const FT_Vector*  to,
		    void *p)
{
  JNIEnv *env;
  jobject obj;
  jclass cls;
  jmethodID method;
  jvalue values[2];
  generalpath *path = (generalpath *) p;

  env = path->env;
  obj = path->obj; 
  values[0].f = (jfloat)(to->x * path->sx + path->px);
  values[1].f = (jfloat)(to->y * path->sy + path->py);

  cls = (*env)->FindClass (env, "java/awt/geom/GeneralPath");
  method = (*env)->GetMethodID (env, cls, "lineTo", "(FF)V");
  (*env)->CallVoidMethodA(env, obj, method, values );

  return 0;
}

static int _quadTo( const FT_Vector*  cp,
		    const FT_Vector*  to,
		    void *p)
{
  JNIEnv *env;
  jobject obj;
  jclass cls;
  jmethodID method;
  jvalue values[4];
  generalpath *path = (generalpath *) p;

  env = path->env;
  obj = path->obj;
  values[0].f = (jfloat)(cp->x * path->sx + path->px);
  values[1].f = (jfloat)(cp->y * path->sy + path->py);
  values[2].f = (jfloat)(to->x * path->sx + path->px);
  values[3].f = (jfloat)(to->y * path->sy + path->py);

  cls = (*env)->FindClass (env, "java/awt/geom/GeneralPath");
  method = (*env)->GetMethodID (env, cls, "quadTo", "(FFFF)V");
  (*env)->CallVoidMethodA(env, obj, method, values );

  return 0;
}

static int _curveTo( const FT_Vector*  cp1,
		     const FT_Vector*  cp2,
		     const FT_Vector*  to,
		     void *p)
{
  JNIEnv *env;
  jobject obj;
  jclass cls;
  jmethodID method;
  jvalue values[6];
  generalpath *path = (generalpath *) p;

  env = path->env;
  obj = path->obj;
  values[0].f = (jfloat)(cp1->x * path->sx + path->px);
  values[1].f = (jfloat)(cp1->y * path->sy + path->py);
  values[2].f = (jfloat)(cp2->x * path->sx + path->px);
  values[3].f = (jfloat)(cp2->y * path->sy + path->py);
  values[4].f = (jfloat)(to->x * path->sx + path->px);
  values[5].f = (jfloat)(to->y * path->sy + path->py);

  cls = (*env)->FindClass (env, "java/awt/geom/GeneralPath");
  method = (*env)->GetMethodID (env, cls, "curveTo", "(FFFFFF)V");
  (*env)->CallVoidMethodA(env, obj, method, values );

  return 0;
}


JNIEXPORT jobject JNICALL 
Java_gnu_java_awt_peer_gtk_FreetypeGlyphVector_getGlyphOutlineNative
 (JNIEnv *env, jobject obj, jint glyphIndex)
{
  generalpath *path;
  jobject gp;
  FT_Outline_Funcs ftCallbacks =
    {
      (FT_Outline_MoveToFunc) _moveTo,
      (FT_Outline_LineToFunc) _lineTo,
      (FT_Outline_ConicToFunc) _quadTo,
      (FT_Outline_CubicToFunc) _curveTo,
      0,
      0
    };
  PangoFcFont *font;
  FT_Face ft_face;
  FT_Glyph glyph;

  font = getFont(env, obj);
  ft_face = pango_fc_font_lock_face( font );

  g_assert (ft_face != NULL);

  path = g_malloc0 (sizeof (generalpath));
  g_assert(path != NULL);
  path->env = env;

  path->px = path->py = 0.0;
  path->sx = 1.0/64.0;
  path->sy = -1.0/64.0;

  {  /* create a GeneralPath instance */
    jclass cls;
    jmethodID method;
    
    cls = (*env)->FindClass (env, "java/awt/geom/GeneralPath");
    method = (*env)->GetMethodID (env, cls, "<init>", "()V");
    gp = path->obj = (*env)->NewObject (env, cls, method);
  }
	      
  if(FT_Load_Glyph(ft_face,
		   (FT_UInt)(glyphIndex),
		   FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP) != 0)
    {
      pango_fc_font_unlock_face( font );
      g_free(path); 
      return NULL;
    }

  FT_Get_Glyph( ft_face->glyph, &glyph );
  FT_Outline_Decompose (&(((FT_OutlineGlyph)glyph)->outline),
			&ftCallbacks, path);
  FT_Done_Glyph( glyph );
  
  pango_fc_font_unlock_face( font );

  g_free(path); 

  return gp; 
}


