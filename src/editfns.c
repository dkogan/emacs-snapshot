/* Lisp functions pertaining to editing.                 -*- coding: utf-8 -*-

Copyright (C) 1985-2025 Free Software Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.  */


#include <config.h>
#include <sys/types.h>
#include <stdio.h>

#ifdef HAVE_PWD_H
#include <pwd.h>
#include <grp.h>
#endif

#include <unistd.h>

#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

#ifdef HAVE_ANDROID
#include "android.h"
#endif

#include "lisp.h"

#include <float.h>
#include <limits.h>
#include <math.h>

#include <c-ctype.h>
#include <intprops.h>
#include <stdlib.h>

#include "composite.h"
#include "intervals.h"
#include "systime.h"
#include "character.h"
#include "buffer.h"
#include "window.h"
#include "blockinput.h"
#include "coding.h"

#ifdef WINDOWSNT
# include "w32common.h"
#endif

#ifdef HAVE_TREE_SITTER
#include "treesit.h"
#endif

static void update_buffer_properties (ptrdiff_t, ptrdiff_t);
static Lisp_Object styled_format (ptrdiff_t, Lisp_Object *, bool);

/* The cached value of Vsystem_name.  This is used only to compare it
   to Vsystem_name, so it need not be visible to the GC.  */
static Lisp_Object cached_system_name;

static void
init_and_cache_system_name (void)
{
  init_system_name ();
  cached_system_name = Vsystem_name;
}

void
init_editfns (void)
{
  const char *user_name;
  register char *p;
  struct passwd *pw;	/* password entry for the current user */
  Lisp_Object tem;

  /* Set up system_name even when dumping.  */
  init_and_cache_system_name ();

  pw = getpwuid (getuid ());
#ifdef MSDOS
  /* We let the real user name default to "root" because that's quite
     accurate on MS-DOS and because it lets Emacs find the init file.
     (The DVX libraries override the Djgpp libraries here.)  */
  Vuser_real_login_name = build_string (pw ? pw->pw_name : "root");
#else
  Vuser_real_login_name = build_string (pw ? pw->pw_name : "unknown");
#endif

  /* Get the effective user name, by consulting environment variables,
     or the effective uid if those are unset.  */
  user_name = getenv ("LOGNAME");
  if (!user_name)
#ifdef WINDOWSNT
    user_name = getenv ("USERNAME");	/* it's USERNAME on NT */
#else  /* WINDOWSNT */
    user_name = getenv ("USER");
#endif /* WINDOWSNT */
  if (!user_name)
    {
      pw = getpwuid (geteuid ());
      user_name = pw ? pw->pw_name : "unknown";
    }
  Vuser_login_name = build_string (user_name);

  /* If the user name claimed in the environment vars differs from
     the real uid, use the claimed name to find the full name.  */
  tem = Fstring_equal (Vuser_login_name, Vuser_real_login_name);
  if (! NILP (tem))
    tem = Vuser_login_name;
  else
    {
      uid_t euid = geteuid ();
      tem = INT_TO_INTEGER (euid);
    }
  Vuser_full_name = Fuser_full_name (tem);

  p = getenv ("NAME");
  if (p)
    Vuser_full_name = build_string (p);
  else if (NILP (Vuser_full_name))
    Vuser_full_name = build_string ("unknown");

#if defined HAVE_SYS_UTSNAME_H
  {
    struct utsname uts;
    uname (&uts);
    Voperating_system_release = build_string (uts.release);
  }
#elif defined WINDOWSNT
  Voperating_system_release = build_string (w32_version_string ());
#else
  Voperating_system_release = Qnil;
#endif
}

DEFUN ("char-to-string", Fchar_to_string, Schar_to_string, 1, 1, 0,
       doc: /* Convert arg CHAR to a string containing that character.
usage: (char-to-string CHAR)  */)
  (Lisp_Object character)
{
  int c, len;
  unsigned char str[MAX_MULTIBYTE_LENGTH];

  CHECK_CHARACTER (character);
  c = XFIXNAT (character);

  len = CHAR_STRING (c, str);
  return make_string_from_bytes ((char *) str, 1, len);
}

DEFUN ("byte-to-string", Fbyte_to_string, Sbyte_to_string, 1, 1, 0,
       doc: /* Convert arg BYTE to a unibyte string containing that byte.  */)
  (Lisp_Object byte)
{
  unsigned char b;
  CHECK_FIXNUM (byte);
  if (XFIXNUM (byte) < 0 || XFIXNUM (byte) > 255)
    error ("Invalid byte");
  b = XFIXNUM (byte);
  return make_unibyte_string ((char *) &b, 1);
}

DEFUN ("string-to-char", Fstring_to_char, Sstring_to_char, 1, 1, 0,
       doc: /* Return the first character in STRING.  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);

  /* This returns zero if STRING is empty.  */
  return make_fixnum (STRING_MULTIBYTE (string)
		      ? STRING_CHAR (SDATA (string))
		      : SREF (string, 0));
}

DEFUN ("point", Fpoint, Spoint, 0, 0, 0,
       doc: /* Return value of point, as an integer.
Beginning of buffer is position (point-min).  */)
  (void)
{
  Lisp_Object temp;
  XSETFASTINT (temp, PT);
  return temp;
}

DEFUN ("point-marker", Fpoint_marker, Spoint_marker, 0, 0, 0,
       doc: /* Return value of point, as a marker object.  */)
  (void)
{
  return build_marker (current_buffer, PT, PT_BYTE);
}

DEFUN ("goto-char", Fgoto_char, Sgoto_char, 1, 1,
         "(goto-char--read-natnum-interactive \"Go to char: \")",
       doc: /* Set point to POSITION, a number or marker.
Beginning of buffer is position (point-min), end is (point-max).

The return value is POSITION.

If called interactively, a numeric prefix argument specifies
POSITION; without a numeric prefix argument, read POSITION from the
minibuffer.  The default value is the number at point (if any).  */)
  (register Lisp_Object position)
{
  if (MARKERP (position))
    set_point_from_marker (position);
  else if (FIXNUMP (position))
    SET_PT (clip_to_bounds (BEGV, XFIXNUM (position), ZV));
  else
    wrong_type_argument (Qinteger_or_marker_p, position);
  return position;
}


/* Return the start or end position of the region.
   BEGINNINGP means return the start.
   If there is no region active, signal an error. */

static Lisp_Object
region_limit (bool beginningp)
{
  Lisp_Object m;

  if (!NILP (Vtransient_mark_mode)
      && NILP (Vmark_even_if_inactive)
      && NILP (BVAR (current_buffer, mark_active)))
    xsignal0 (Qmark_inactive);

  m = Fmarker_position (BVAR (current_buffer, mark));
  if (NILP (m))
    error ("The mark is not set now, so there is no region");

  /* Clip to the current narrowing (bug#11770).  */
  return make_fixnum ((PT < XFIXNAT (m)) == beginningp
		      ? PT
		      : clip_to_bounds (BEGV, XFIXNAT (m), ZV));
}

DEFUN ("region-beginning", Fregion_beginning, Sregion_beginning, 0, 0, 0,
       doc: /* Return the integer value of point or mark, whichever is smaller.  */)
  (void)
{
  return region_limit (1);
}

DEFUN ("region-end", Fregion_end, Sregion_end, 0, 0, 0,
       doc: /* Return the integer value of point or mark, whichever is larger.  */)
  (void)
{
  return region_limit (0);
}

DEFUN ("mark-marker", Fmark_marker, Smark_marker, 0, 0, 0,
       doc: /* Return this buffer's mark, as a marker object.
Watch out!  Moving this marker changes the mark position.
If you set the marker not to point anywhere, the buffer will have no mark.  */)
  (void)
{
  return BVAR (current_buffer, mark);
}


DEFUN ("get-pos-property", Fget_pos_property, Sget_pos_property, 2, 3, 0,
       doc: /* Return the value of POSITION's property PROP, in OBJECT.
Almost identical to `get-char-property' except for the following difference:
Whereas `get-char-property' returns the property of the char at (i.e. right
after) POSITION, this pays attention to properties's stickiness and overlays's
advancement settings, in order to find the property of POSITION itself,
i.e. the property that a char would inherit if it were inserted
at POSITION.  */)
  (Lisp_Object position, register Lisp_Object prop, Lisp_Object object)
{
  CHECK_FIXNUM_COERCE_MARKER (position);

  if (NILP (object))
    XSETBUFFER (object, current_buffer);
  else if (WINDOWP (object))
    object = XWINDOW (object)->contents;

  if (!BUFFERP (object))
    /* pos-property only makes sense in buffers right now, since strings
       have no overlays and no notion of insertion for which stickiness
       could be obeyed.  */
    return Fget_text_property (position, prop, object);
  else
    {
      EMACS_INT posn = XFIXNUM (position);
      Lisp_Object tem;
      struct buffer *obuf = current_buffer;
      struct itree_node *node;
      struct sortvec items[2];
      struct buffer *b = XBUFFER (object);
      struct sortvec *result = NULL;
      Lisp_Object res = Qnil;

      set_buffer_temp (b);

      ITREE_FOREACH (node, b->overlays, posn - 1, posn + 1, ASCENDING)
	{
	  Lisp_Object ol = node->data;
	  tem = Foverlay_get (ol, prop);
	  if (NILP (tem)
	      /* Check the overlay is indeed active at point.  */
	      || ((node->begin == posn
		   && OVERLAY_FRONT_ADVANCE_P (ol))
		  || (node->end == posn
		      && ! OVERLAY_REAR_ADVANCE_P (ol))
		  || node->begin > posn
		  || node->end < posn))
	    /* The overlay will not cover a char inserted at point.  */
	    continue;

	  struct sortvec *this = (result == items ? items + 1 : items);
          if (NILP (res)
              || (make_sortvec_item (this, node->data),
                  compare_overlays (result, this) < 0))
            {
              result = this;
              res = tem;
            }
	}
      set_buffer_temp (obuf);

      if (!NILP (res))
        return res;

      { /* Now check the text properties.  */
	int stickiness = text_property_stickiness (prop, position, object);
	if (stickiness > 0)
	  return Fget_text_property (position, prop, object);
	else if (stickiness < 0
		 && XFIXNUM (position) > BUF_BEGV (XBUFFER (object)))
	  return Fget_text_property (make_fixnum (XFIXNUM (position) - 1),
				     prop, object);
	else
	  return Qnil;
      }
    }
}

/* Find the field surrounding POS in *BEG and *END.  If POS is nil,
   the value of point is used instead.  If BEG or END is null,
   means don't store the beginning or end of the field.

   BEG_LIMIT and END_LIMIT serve to limit the ranged of the returned
   results; they do not effect boundary behavior.

   If MERGE_AT_BOUNDARY is non-nil, then if POS is at the very first
   position of a field, then the beginning of the previous field is
   returned instead of the beginning of POS's field (since the end of a
   field is actually also the beginning of the next input field, this
   behavior is sometimes useful).  Additionally in the MERGE_AT_BOUNDARY
   non-nil case, if two fields are separated by a field with the special
   value `boundary', and POS lies within it, then the two separated
   fields are considered to be adjacent, and POS between them, when
   finding the beginning and ending of the "merged" field.

   Either BEG or END may be 0, in which case the corresponding value
   is not stored.  */

void
find_field (Lisp_Object pos, Lisp_Object merge_at_boundary,
	    Lisp_Object beg_limit,
	    ptrdiff_t *beg, Lisp_Object end_limit, ptrdiff_t *end)
{
  /* Fields right before and after the point.  */
  Lisp_Object before_field, after_field;
  /* True if POS counts as the start of a field.  */
  bool at_field_start = 0;
  /* True if POS counts as the end of a field.  */
  bool at_field_end = 0;

  if (NILP (pos))
    XSETFASTINT (pos, PT);
  else
    CHECK_FIXNUM_COERCE_MARKER (pos);

  after_field
    = get_char_property_and_overlay (pos, Qfield, Qnil, NULL);
  before_field
    = (XFIXNAT (pos) > BEGV
       ? get_char_property_and_overlay (make_fixnum (XFIXNUM (pos) - 1),
					Qfield, Qnil, NULL)
       /* Using nil here would be a more obvious choice, but it would
          fail when the buffer starts with a non-sticky field.  */
       : after_field);

  /* See if we need to handle the case where MERGE_AT_BOUNDARY is nil
     and POS is at beginning of a field, which can also be interpreted
     as the end of the previous field.  Note that the case where if
     MERGE_AT_BOUNDARY is non-nil (see function comment) is actually the
     more natural one; then we avoid treating the beginning of a field
     specially.  */
  if (NILP (merge_at_boundary))
    {
      Lisp_Object field = Fget_pos_property (pos, Qfield, Qnil);
      if (!EQ (field, after_field))
	at_field_end = 1;
      if (!EQ (field, before_field))
	at_field_start = 1;
      if (NILP (field) && at_field_start && at_field_end)
	/* If an inserted char would have a nil field while the surrounding
	   text is non-nil, we're probably not looking at a
	   zero-length field, but instead at a non-nil field that's
	   not intended for editing (such as comint's prompts).  */
	at_field_end = at_field_start = 0;
    }

  /* Note about special `boundary' fields:

     Consider the case where the point (`.') is between the fields `x' and `y':

	xxxx.yyyy

     In this situation, if merge_at_boundary is non-nil, consider the
     `x' and `y' fields as forming one big merged field, and so the end
     of the field is the end of `y'.

     However, if `x' and `y' are separated by a special `boundary' field
     (a field with a `field' char-property of 'boundary), then ignore
     this special field when merging adjacent fields.  Here's the same
     situation, but with a `boundary' field between the `x' and `y' fields:

	xxx.BBBByyyy

     Here, if point is at the end of `x', the beginning of `y', or
     anywhere in-between (within the `boundary' field), merge all
     three fields and consider the beginning as being the beginning of
     the `x' field, and the end as being the end of the `y' field.  */

  if (beg)
    {
      if (at_field_start)
	/* POS is at the edge of a field, and we should consider it as
	   the beginning of the following field.  */
	*beg = XFIXNAT (pos);
      else
	/* Find the previous field boundary.  */
	{
	  Lisp_Object p = pos;
	  if (!NILP (merge_at_boundary) && EQ (before_field, Qboundary))
	    /* Skip a `boundary' field.  */
	    p = Fprevious_single_char_property_change (p, Qfield, Qnil,
						       beg_limit);

	  p = Fprevious_single_char_property_change (p, Qfield, Qnil,
						     beg_limit);
	  *beg = NILP (p) ? BEGV : XFIXNAT (p);
	}
    }

  if (end)
    {
      if (at_field_end)
	/* POS is at the edge of a field, and we should consider it as
	   the end of the previous field.  */
	*end = XFIXNAT (pos);
      else
	/* Find the next field boundary.  */
	{
	  if (!NILP (merge_at_boundary) && EQ (after_field, Qboundary))
	    /* Skip a `boundary' field.  */
	    pos = Fnext_single_char_property_change (pos, Qfield, Qnil,
						     end_limit);

	  pos = Fnext_single_char_property_change (pos, Qfield, Qnil,
						   end_limit);
	  *end = NILP (pos) ? ZV : XFIXNAT (pos);
	}
    }
}


DEFUN ("delete-field", Fdelete_field, Sdelete_field, 0, 1, 0,
       doc: /* Delete the field surrounding POS.
A field is a region of text with the same `field' property.
If POS is nil, the value of point is used for POS.  */)
  (Lisp_Object pos)
{
  ptrdiff_t beg, end;
  find_field (pos, Qnil, Qnil, &beg, Qnil, &end);
  if (beg != end)
    del_range (beg, end);
  return Qnil;
}

DEFUN ("field-string", Ffield_string, Sfield_string, 0, 1, 0,
       doc: /* Return the contents of the field surrounding POS as a string.
A field is a region of text with the same `field' property.
If POS is nil, the value of point is used for POS.  */)
  (Lisp_Object pos)
{
  ptrdiff_t beg, end;
  find_field (pos, Qnil, Qnil, &beg, Qnil, &end);
  return make_buffer_string (beg, end, 1);
}

DEFUN ("field-string-no-properties", Ffield_string_no_properties, Sfield_string_no_properties, 0, 1, 0,
       doc: /* Return the contents of the field around POS, without text properties.
A field is a region of text with the same `field' property.
If POS is nil, the value of point is used for POS.  */)
  (Lisp_Object pos)
{
  ptrdiff_t beg, end;
  find_field (pos, Qnil, Qnil, &beg, Qnil, &end);
  return make_buffer_string (beg, end, 0);
}

DEFUN ("field-beginning", Ffield_beginning, Sfield_beginning, 0, 3, 0,
       doc: /* Return the beginning of the field surrounding POS.
A field is a region of text with the same `field' property.
If POS is nil, the value of point is used for POS.
If ESCAPE-FROM-EDGE is non-nil and POS is at the beginning of its
field, then the beginning of the *previous* field is returned.
If LIMIT is non-nil, it is a buffer position; if the beginning of the field
is before LIMIT, then LIMIT will be returned instead.  */)
  (Lisp_Object pos, Lisp_Object escape_from_edge, Lisp_Object limit)
{
  ptrdiff_t beg;
  find_field (pos, escape_from_edge, limit, &beg, Qnil, 0);
  return make_fixnum (beg);
}

DEFUN ("field-end", Ffield_end, Sfield_end, 0, 3, 0,
       doc: /* Return the end of the field surrounding POS.
A field is a region of text with the same `field' property.
If POS is nil, the value of point is used for POS.
If ESCAPE-FROM-EDGE is non-nil and POS is at the end of its field,
then the end of the *following* field is returned.
If LIMIT is non-nil, it is a buffer position; if the end of the field
is after LIMIT, then LIMIT will be returned instead.  */)
  (Lisp_Object pos, Lisp_Object escape_from_edge, Lisp_Object limit)
{
  ptrdiff_t end;
  find_field (pos, escape_from_edge, Qnil, 0, limit, &end);
  return make_fixnum (end);
}

DEFUN ("constrain-to-field", Fconstrain_to_field, Sconstrain_to_field, 2, 5, 0,
       doc: /* Return the position closest to NEW-POS that is in the same field as OLD-POS.
A field is a region of text with the same `field' property.

If NEW-POS is nil, then use the current point instead, and move point
to the resulting constrained position, in addition to returning that
position.

If OLD-POS is at the boundary of two fields, then the allowable
positions for NEW-POS depends on the value of the optional argument
ESCAPE-FROM-EDGE: If ESCAPE-FROM-EDGE is nil, then NEW-POS is
constrained to the field that has the same `field' char-property
as any new characters inserted at OLD-POS, whereas if ESCAPE-FROM-EDGE
is non-nil, NEW-POS is constrained to the union of the two adjacent
fields.  Additionally, if two fields are separated by another field with
the special value `boundary', then any point within this special field is
also considered to be `on the boundary'.

If the optional argument ONLY-IN-LINE is non-nil and constraining
NEW-POS would move it to a different line, NEW-POS is returned
unconstrained.  This is useful for commands that move by line, like
\\[next-line] or \\[beginning-of-line], which should generally respect field boundaries
only in the case where they can still move to the right line.

If the optional argument INHIBIT-CAPTURE-PROPERTY is non-nil, and OLD-POS has
a non-nil property of that name, then any field boundaries are ignored.

Field boundaries are not noticed if `inhibit-field-text-motion' is non-nil.  */)
  (Lisp_Object new_pos, Lisp_Object old_pos, Lisp_Object escape_from_edge,
   Lisp_Object only_in_line, Lisp_Object inhibit_capture_property)
{
  /* If non-zero, then the original point, before re-positioning.  */
  ptrdiff_t orig_point = 0;
  bool fwd;
  Lisp_Object prev_old, prev_new;

  if (NILP (new_pos))
    /* Use the current point, and afterwards, set it.  */
    {
      orig_point = PT;
      XSETFASTINT (new_pos, PT);
    }

  CHECK_FIXNUM_COERCE_MARKER (new_pos);
  CHECK_FIXNUM_COERCE_MARKER (old_pos);

  fwd = (XFIXNUM (new_pos) > XFIXNUM (old_pos));

  prev_old = make_fixnum (XFIXNUM (old_pos) - 1);
  prev_new = make_fixnum (XFIXNUM (new_pos) - 1);

  if (NILP (Vinhibit_field_text_motion)
      && !BASE_EQ (new_pos, old_pos)
      && (!NILP (Fget_char_property (new_pos, Qfield, Qnil))
          || !NILP (Fget_char_property (old_pos, Qfield, Qnil))
          /* To recognize field boundaries, we must also look at the
             previous positions; we could use `Fget_pos_property'
             instead, but in itself that would fail inside non-sticky
             fields (like comint prompts).  */
          || (XFIXNAT (new_pos) > BEGV
              && !NILP (Fget_char_property (prev_new, Qfield, Qnil)))
          || (XFIXNAT (old_pos) > BEGV
              && !NILP (Fget_char_property (prev_old, Qfield, Qnil))))
      && (NILP (inhibit_capture_property)
          /* Field boundaries are again a problem; but now we must
             decide the case exactly, so we need to call
             `get_pos_property' as well.  */
          || (NILP (Fget_pos_property (old_pos, inhibit_capture_property, Qnil))
              && (XFIXNAT (old_pos) <= BEGV
                  || NILP (Fget_char_property
			   (old_pos, inhibit_capture_property, Qnil))
                  || NILP (Fget_char_property
			   (prev_old, inhibit_capture_property, Qnil))))))
    /* It is possible that NEW_POS is not within the same field as
       OLD_POS; try to move NEW_POS so that it is.  */
    {
      ptrdiff_t counted;
      Lisp_Object field_bound;

      if (fwd)
	field_bound = Ffield_end (old_pos, escape_from_edge, new_pos);
      else
	field_bound = Ffield_beginning (old_pos, escape_from_edge, new_pos);

      if (/* See if ESCAPE_FROM_EDGE caused FIELD_BOUND to jump to the
             other side of NEW_POS, which would mean that NEW_POS is
             already acceptable, and it's not necessary to constrain it
             to FIELD_BOUND.  */
	  ((XFIXNAT (field_bound) < XFIXNAT (new_pos)) ? fwd : !fwd)
	  /* NEW_POS should be constrained, but only if either
	     ONLY_IN_LINE is nil (in which case any constraint is OK),
	     or NEW_POS and FIELD_BOUND are on the same line (in which
	     case the constraint is OK even if ONLY_IN_LINE is non-nil).  */
	  && (NILP (only_in_line)
	      /* This is the ONLY_IN_LINE case, check that NEW_POS and
		 FIELD_BOUND are on the same line by seeing whether
		 there's an intervening newline or not.  */
	      || (find_newline (XFIXNAT (new_pos), -1,
				XFIXNAT (field_bound), -1,
				fwd ? -1 : 1, &counted, NULL, 1),
		  counted == 0)))
	/* Constrain NEW_POS to FIELD_BOUND.  */
	new_pos = field_bound;

      if (orig_point && XFIXNAT (new_pos) != orig_point)
	/* The NEW_POS argument was originally nil, so automatically set PT. */
	SET_PT (XFIXNAT (new_pos));
    }

  return new_pos;
}


static ptrdiff_t
bol (Lisp_Object n, ptrdiff_t *out_count)
{
  ptrdiff_t bytepos, charpos, count;

  if (NILP (n))
    count = 0;
  else if (FIXNUMP (n))
    count = clip_to_bounds (-BUF_BYTES_MAX, XFIXNUM (n) - 1, BUF_BYTES_MAX);
  else
    {
      CHECK_INTEGER (n);
      count = NILP (Fnatnump (n)) ? -BUF_BYTES_MAX : BUF_BYTES_MAX;
    }
  if (out_count)
    *out_count = count;
  scan_newline_from_point (count, &charpos, &bytepos);
  return charpos;
}

DEFUN ("pos-bol", Fpos_bol, Spos_bol, 0, 1, 0,
       doc: /* Return the position of the first character on the current line.
With optional argument N, scan forward N - 1 lines first.
If the scan reaches the end of the buffer, return that position.

This function ignores text display directionality; it returns the
position of the first character in logical order, i.e. the smallest
character position on the logical line.  See `vertical-motion' for
movement by screen lines.

This function does not move point.  Also see `line-beginning-position'.  */)
  (Lisp_Object n)
{
  return make_fixnum (bol (n, NULL));
}

DEFUN ("line-beginning-position",
       Fline_beginning_position, Sline_beginning_position, 0, 1, 0,
       doc: /* Return the position of the first character in the current line/field.
With optional argument N non-nil, move forward N - 1 lines first.
This function is like `pos-bol' (which see), but respects fields.

This function constrains the returned position to the current field
unless that position would be on a different line from the original,
unconstrained result.  If N is nil or 1, and a front-sticky field
starts at point, the scan stops as soon as it starts.  To ignore field
boundaries, bind `inhibit-field-text-motion' to t.

This function does not move point.  */)
  (Lisp_Object n)
{
  ptrdiff_t count, charpos = bol (n, &count);
  /* Return END constrained to the current input field.  */
  return Fconstrain_to_field (make_fixnum (charpos), make_fixnum (PT),
			      count != 0 ? Qt : Qnil,
			      Qt, Qnil);
}

static ptrdiff_t
eol (Lisp_Object n)
{
  ptrdiff_t count;

  if (NILP (n))
    count = 1;
  else if (FIXNUMP (n))
    count = clip_to_bounds (-BUF_BYTES_MAX, XFIXNUM (n), BUF_BYTES_MAX);
  else
    {
      CHECK_INTEGER (n);
      count = NILP (Fnatnump (n)) ? -BUF_BYTES_MAX : BUF_BYTES_MAX;
    }
  return find_before_next_newline (PT, 0, count - (count <= 0),
				   NULL);
}

DEFUN ("pos-eol", Fpos_eol, Spos_eol, 0, 1, 0,
       doc: /* Return the position of the last character on the current line.
With argument N not nil or 1, move forward N - 1 lines first.
If scan reaches end of buffer, return that position.

This function ignores text display directionality; it returns the
position of the last character in logical order, i.e. the largest
character position on the line.

This function does not move point.  Also see `line-end-position'.  */)
  (Lisp_Object n)
{
  return make_fixnum (eol (n));
}

DEFUN ("line-end-position", Fline_end_position, Sline_end_position, 0, 1, 0,
       doc: /* Return the position of the last character in the current line/field.
With argument N not nil or 1, move forward N - 1 lines first.
If scan reaches end of buffer, return that position.

This function is like `pos-eol' (which see), but respects fields.

This function constrains the returned position to the current field
unless that would be on a different line from the original,
unconstrained result.  If N is nil or 1, and a rear-sticky field ends
at point, the scan stops as soon as it starts.  To ignore field
boundaries bind `inhibit-field-text-motion' to t.

This function does not move point.  */)
  (Lisp_Object n)
{
  /* Return END_POS constrained to the current input field.  */
  return Fconstrain_to_field (make_fixnum (eol (n)), make_fixnum (PT),
			      Qnil, Qt, Qnil);
}

/* Save current buffer state for save-excursion special form.  */

void
save_excursion_save (union specbinding *pdl)
{
  eassert (pdl->unwind_excursion.kind == SPECPDL_UNWIND_EXCURSION);
  pdl->unwind_excursion.marker = Fpoint_marker ();
  /* Selected window if current buffer is shown in it, nil otherwise.  */
  pdl->unwind_excursion.window
    = (BASE_EQ (XWINDOW (selected_window)->contents, Fcurrent_buffer ())
       ? selected_window : Qnil);
}

/* Restore saved buffer before leaving `save-excursion' special form.  */

void
save_excursion_restore (Lisp_Object marker, Lisp_Object window)
{
  Lisp_Object buffer = Fmarker_buffer (marker);
  /* If we're unwinding to top level, saved buffer may be deleted.  This
     means that all of its markers are unchained and so BUFFER is nil.  */
  if (NILP (buffer))
    return;

  Fset_buffer (buffer);

  /* Point marker.  */
  Fgoto_char (marker);
  unchain_marker (XMARKER (marker));

  /* If buffer was visible in a window, and a different window was
     selected, and the old selected window is still showing this
     buffer, restore point in that window.  */
  if (WINDOWP (window) && !BASE_EQ (window, selected_window))
    {
      /* Set window point if WINDOW is live and shows the current buffer.  */
      Lisp_Object contents = XWINDOW (window)->contents;
      if (BUFFERP (contents) && XBUFFER (contents) == current_buffer)
	Fset_window_point (window, make_fixnum (PT));
    }
}

DEFUN ("save-excursion", Fsave_excursion, Ssave_excursion, 0, UNEVALLED, 0,
       doc: /* Save point, and current buffer; execute BODY; restore those things.
Executes BODY just like `progn'.
The values of point and the current buffer are restored
even in case of abnormal exit (throw or error).

If you only want to save the current buffer but not point,
then just use `save-current-buffer', or even `with-current-buffer'.

Before Emacs 25.1, `save-excursion' used to save the mark state.
To save the mark state as well as point and the current buffer, use
`save-mark-and-excursion'.

usage: (save-excursion &rest BODY)  */)
  (Lisp_Object args)
{
  register Lisp_Object val;
  specpdl_ref count = SPECPDL_INDEX ();

  record_unwind_protect_excursion ();

  val = Fprogn (args);
  return unbind_to (count, val);
}

DEFUN ("save-current-buffer", Fsave_current_buffer, Ssave_current_buffer, 0, UNEVALLED, 0,
       doc: /* Record which buffer is current; execute BODY; make that buffer current.
BODY is executed just like `progn'.
usage: (save-current-buffer &rest BODY)  */)
  (Lisp_Object args)
{
  specpdl_ref count = SPECPDL_INDEX ();

  record_unwind_current_buffer ();
  return unbind_to (count, Fprogn (args));
}

DEFUN ("buffer-size", Fbuffer_size, Sbuffer_size, 0, 1, 0,
       doc: /* Return the number of characters in the current buffer.
If BUFFER is not nil, return the number of characters in that buffer
instead.

This does not take narrowing into account; to count the number of
characters in the accessible portion of the current buffer, use
`(- (point-max) (point-min))', and to count the number of characters
in the accessible portion of some other BUFFER, use
`(with-current-buffer BUFFER (- (point-max) (point-min)))'.  */)
  (Lisp_Object buffer)
{
  if (NILP (buffer))
    return make_fixnum (Z - BEG);
  else
    {
      CHECK_BUFFER (buffer);
      return make_fixnum (BUF_Z (XBUFFER (buffer))
			  - BUF_BEG (XBUFFER (buffer)));
    }
}

DEFUN ("point-min", Fpoint_min, Spoint_min, 0, 0, 0,
       doc: /* Return the minimum permissible value of point in the current buffer.
This is 1, unless narrowing (a buffer restriction) is in effect.  */)
  (void)
{
  Lisp_Object temp;
  XSETFASTINT (temp, BEGV);
  return temp;
}

DEFUN ("point-min-marker", Fpoint_min_marker, Spoint_min_marker, 0, 0, 0,
       doc: /* Return a marker to the minimum permissible value of point in this buffer.
This is the beginning, unless narrowing (a buffer restriction) is in effect.  */)
  (void)
{
  return build_marker (current_buffer, BEGV, BEGV_BYTE);
}

DEFUN ("point-max", Fpoint_max, Spoint_max, 0, 0, 0,
       doc: /* Return the maximum permissible value of point in the current buffer.
This is (1+ (buffer-size)), unless narrowing (a buffer restriction)
is in effect, in which case it is less.  */)
  (void)
{
  Lisp_Object temp;
  XSETFASTINT (temp, ZV);
  return temp;
}

DEFUN ("point-max-marker", Fpoint_max_marker, Spoint_max_marker, 0, 0, 0,
       doc: /* Return a marker to the maximum permissible value of point in this buffer.
This is (1+ (buffer-size)), unless narrowing (a buffer restriction)
is in effect, in which case it is less.  */)
  (void)
{
  return build_marker (current_buffer, ZV, ZV_BYTE);
}

DEFUN ("gap-position", Fgap_position, Sgap_position, 0, 0, 0,
       doc: /* Return the position of the gap, in the current buffer.
See also `gap-size'.  */)
  (void)
{
  Lisp_Object temp;
  XSETFASTINT (temp, GPT);
  return temp;
}

DEFUN ("gap-size", Fgap_size, Sgap_size, 0, 0, 0,
       doc: /* Return the size of the current buffer's gap.
See also `gap-position'.  */)
  (void)
{
  Lisp_Object temp;
  XSETFASTINT (temp, GAP_SIZE);
  return temp;
}

DEFUN ("position-bytes", Fposition_bytes, Sposition_bytes, 1, 1, 0,
       doc: /* Return the byte position for character position POSITION.
If POSITION is out of range, the value is nil.  */)
  (Lisp_Object position)
{
  EMACS_INT pos = fix_position (position);
  if (! (BEG <= pos && pos <= Z))
    return Qnil;
  return make_fixnum (CHAR_TO_BYTE (pos));
}

DEFUN ("byte-to-position", Fbyte_to_position, Sbyte_to_position, 1, 1, 0,
       doc: /* Return the character position for byte position BYTEPOS.
If BYTEPOS is out of range, the value is nil.  */)
  (Lisp_Object bytepos)
{
  ptrdiff_t pos_byte;

  CHECK_FIXNUM (bytepos);
  pos_byte = XFIXNUM (bytepos);
  if (pos_byte < BEG_BYTE || pos_byte > Z_BYTE)
    return Qnil;
  if (Z != Z_BYTE)
    /* There are multibyte characters in the buffer.
       The argument of BYTE_TO_CHAR must be a byte position at
       a character boundary, so search for the start of the current
       character.  */
    while (!CHAR_HEAD_P (FETCH_BYTE (pos_byte)))
      pos_byte--;
  return make_fixnum (BYTE_TO_CHAR (pos_byte));
}

DEFUN ("following-char", Ffollowing_char, Sfollowing_char, 0, 0, 0,
       doc: /* Return the character following point, as a number.
At the end of the buffer or accessible region, return 0.  */)
  (void)
{
  Lisp_Object temp;
  if (PT >= ZV)
    XSETFASTINT (temp, 0);
  else
    XSETFASTINT (temp, FETCH_CHAR (PT_BYTE));
  return temp;
}

DEFUN ("preceding-char", Fprevious_char, Sprevious_char, 0, 0, 0,
       doc: /* Return the character preceding point, as a number.
At the beginning of the buffer or accessible region, return 0.  */)
  (void)
{
  Lisp_Object temp;
  if (PT <= BEGV)
    XSETFASTINT (temp, 0);
  else if (!NILP (BVAR (current_buffer, enable_multibyte_characters)))
    {
      ptrdiff_t pos = PT_BYTE;
      pos -= prev_char_len (pos);
      XSETFASTINT (temp, FETCH_CHAR (pos));
    }
  else
    XSETFASTINT (temp, FETCH_BYTE (PT_BYTE - 1));
  return temp;
}

DEFUN ("bobp", Fbobp, Sbobp, 0, 0, 0,
       doc: /* Return t if point is at the beginning of the buffer.
If the buffer is narrowed, this means the beginning of the narrowed part.  */)
  (void)
{
  if (PT == BEGV)
    return Qt;
  return Qnil;
}

DEFUN ("eobp", Feobp, Seobp, 0, 0, 0,
       doc: /* Return t if point is at the end of the buffer.
If the buffer is narrowed, this means the end of the narrowed part.  */)
  (void)
{
  if (PT == ZV)
    return Qt;
  return Qnil;
}

DEFUN ("bolp", Fbolp, Sbolp, 0, 0, 0,
       doc: /* Return t if point is at the beginning of a line.  */)
  (void)
{
  if (PT == BEGV || FETCH_BYTE (PT_BYTE - 1) == '\n')
    return Qt;
  return Qnil;
}

DEFUN ("eolp", Feolp, Seolp, 0, 0, 0,
       doc: /* Return t if point is at the end of a line.
`End of a line' includes point being at the end of the buffer.  */)
  (void)
{
  if (PT == ZV || FETCH_BYTE (PT_BYTE) == '\n')
    return Qt;
  return Qnil;
}

DEFUN ("char-after", Fchar_after, Schar_after, 0, 1, 0,
       doc: /* Return character in current buffer at position POS.
POS is an integer or a marker and defaults to point.
If POS is out of range, the value is nil.  */)
  (Lisp_Object pos)
{
  register ptrdiff_t pos_byte;

  if (NILP (pos))
    {
      pos_byte = PT_BYTE;
      if (pos_byte < BEGV_BYTE || pos_byte >= ZV_BYTE)
        return Qnil;
    }
  else if (MARKERP (pos))
    {
      pos_byte = marker_byte_position (pos);
      if (pos_byte < BEGV_BYTE || pos_byte >= ZV_BYTE)
	return Qnil;
    }
  else
    {
      EMACS_INT p = fix_position (pos);
      if (! (BEGV <= p && p < ZV))
	return Qnil;

      pos_byte = CHAR_TO_BYTE (p);
    }

  return make_fixnum (FETCH_CHAR (pos_byte));
}

DEFUN ("char-before", Fchar_before, Schar_before, 0, 1, 0,
       doc: /* Return character in current buffer preceding position POS.
POS is an integer or a marker and defaults to point.
If POS is out of range, the value is nil.  */)
  (Lisp_Object pos)
{
  register Lisp_Object val;
  register ptrdiff_t pos_byte;

  if (NILP (pos))
    {
      pos_byte = PT_BYTE;
      XSETFASTINT (pos, PT);
    }

  if (MARKERP (pos))
    {
      pos_byte = marker_byte_position (pos);

      if (pos_byte <= BEGV_BYTE || pos_byte > ZV_BYTE)
	return Qnil;
    }
  else
    {
      EMACS_INT p = fix_position (pos);

      if (! (BEGV < p && p <= ZV))
	return Qnil;

      pos_byte = CHAR_TO_BYTE (p);
    }

  if (!NILP (BVAR (current_buffer, enable_multibyte_characters)))
    {
      pos_byte -= prev_char_len (pos_byte);
      XSETFASTINT (val, FETCH_CHAR (pos_byte));
    }
  else
    {
      pos_byte--;
      XSETFASTINT (val, FETCH_BYTE (pos_byte));
    }
   return val;
}

DEFUN ("user-login-name", Fuser_login_name, Suser_login_name, 0, 1, 0,
       doc: /* Return the name under which the user logged in, as a string.
This is based on the effective uid, not the real uid.
Also, if the environment variables LOGNAME or USER are set,
that determines the value of this function.

If optional argument UID is an integer, return the login name
of the user with that uid, or nil if there is no such user.  */)
  (Lisp_Object uid)
{
  struct passwd *pw;
  uid_t id;

  /* Set up the user name info if we didn't do it before.
     (That can happen if Emacs is dumpable
     but you decide to run `temacs -l loadup' and not dump.  */
  if (NILP (Vuser_login_name))
    init_editfns ();

  if (NILP (uid))
    return Vuser_login_name;

  CONS_TO_INTEGER (uid, uid_t, id);
  block_input ();
  pw = getpwuid (id);
  unblock_input ();
  return (pw ? build_string (pw->pw_name) : Qnil);
}

DEFUN ("user-real-login-name", Fuser_real_login_name, Suser_real_login_name,
       0, 0, 0,
       doc: /* Return the name of the user's real uid, as a string.
This ignores the environment variables LOGNAME and USER, so it differs from
`user-login-name' when running under `su'.  */)
  (void)
{
  /* Set up the user name info if we didn't do it before.
     (That can happen if Emacs is dumpable
     but you decide to run `temacs -l loadup' and not dump.  */
  if (NILP (Vuser_login_name))
    init_editfns ();
  return Vuser_real_login_name;
}

DEFUN ("user-uid", Fuser_uid, Suser_uid, 0, 0, 0,
       doc: /* Return the effective uid of Emacs, as an integer.  */)
  (void)
{
  uid_t euid = geteuid ();
  return INT_TO_INTEGER (euid);
}

DEFUN ("user-real-uid", Fuser_real_uid, Suser_real_uid, 0, 0, 0,
       doc: /* Return the real uid of Emacs, as an integer.  */)
  (void)
{
  uid_t uid = getuid ();
  return INT_TO_INTEGER (uid);
}

DEFUN ("group-name", Fgroup_name, Sgroup_name, 1, 1, 0,
       doc: /* Return the name of the group whose numeric group ID is GID.
The argument GID should be an integer or a float.
Return nil if a group with such GID does not exists or is not known.  */)
  (Lisp_Object gid)
{
  struct group *gr;
  gid_t id;

  if (!NUMBERP (gid) && !CONSP (gid))
    error ("Invalid GID specification");
  CONS_TO_INTEGER (gid, gid_t, id);
  block_input ();
  gr = getgrgid (id);
  unblock_input ();
  return gr ? build_string (gr->gr_name) : Qnil;
}

DEFUN ("group-gid", Fgroup_gid, Sgroup_gid, 0, 0, 0,
       doc: /* Return the effective gid of Emacs, as an integer.  */)
  (void)
{
  gid_t egid = getegid ();
  return INT_TO_INTEGER (egid);
}

DEFUN ("group-real-gid", Fgroup_real_gid, Sgroup_real_gid, 0, 0, 0,
       doc: /* Return the real gid of Emacs, as an integer.  */)
  (void)
{
  gid_t gid = getgid ();
  return INT_TO_INTEGER (gid);
}

DEFUN ("user-full-name", Fuser_full_name, Suser_full_name, 0, 1, 0,
       doc: /* Return the full name of the user logged in, as a string.
If the full name corresponding to Emacs's userid is not known,
return "unknown".

If optional argument UID is an integer, return the full name
of the user with that uid, or nil if there is no such user.
If UID is a string, return the full name of the user with that login
name, or nil if there is no such user.

If the full name includes commas, remove everything starting with
the first comma, because the \\='gecos\\=' field of the \\='/etc/passwd\\=' file
is in general a comma-separated list.  */)
  (Lisp_Object uid)
{
  struct passwd *pw;
  register char *p, *q;
  Lisp_Object full;

  if (NILP (uid))
    return Vuser_full_name;
  else if (NUMBERP (uid))
    {
      uid_t u;
      CONS_TO_INTEGER (uid, uid_t, u);
      block_input ();
      pw = getpwuid (u);
      unblock_input ();
    }
  else if (STRINGP (uid))
    {
      block_input ();
      pw = getpwnam (SSDATA (uid));
      unblock_input ();
    }
  else
    error ("Invalid UID specification");

  if (!pw)
    return Qnil;

  p = USER_FULL_NAME;
  if (!p)
    return Qnil;

  /* Chop off everything after the first comma, since 'pw_gecos' is a
     comma-separated list. */
  q = strchr (p, ',');
  full = make_string (p, q ? q - p : strlen (p));

#ifdef AMPERSAND_FULL_NAME
  p = SSDATA (full);
  q = strchr (p, '&');
  /* Substitute the login name for the &, upcasing the first character.  */
  if (q)
    {
      Lisp_Object login = Fuser_login_name (INT_TO_INTEGER (pw->pw_uid));
      if (!NILP (login))
	{
	  USE_SAFE_ALLOCA;
	  char *r = SAFE_ALLOCA (strlen (p) + SBYTES (login) + 1);
	  memcpy (r, p, q - p);
	  char *s = lispstpcpy (&r[q - p], login);
	  r[q - p] = upcase ((unsigned char) r[q - p]);
	  strcpy (s, q + 1);
	  full = build_string (r);
	  SAFE_FREE ();
	}
    }
#endif /* AMPERSAND_FULL_NAME */

  return full;
}

DEFUN ("system-name", Fsystem_name, Ssystem_name, 0, 0, 0,
       doc: /* Return the host name of the machine you are running on, as a string.  */)
  (void)
{
  if (EQ (Vsystem_name, cached_system_name))
    init_and_cache_system_name ();
  return Vsystem_name;
}

DEFUN ("emacs-pid", Femacs_pid, Semacs_pid, 0, 0, 0,
       doc: /* Return the process ID of Emacs, as an integer.  */)
  (void)
{
  pid_t pid = getpid ();
  return INT_TO_INTEGER (pid);
}


/* Insert NARGS Lisp objects in the array ARGS by calling INSERT_FUNC
   (if a type of object is Lisp_Int) or INSERT_FROM_STRING_FUNC (if a
   type of object is Lisp_String).  INHERIT is passed to
   INSERT_FROM_STRING_FUNC as the last argument.  */

static void
general_insert_function (void (*insert_func)
			      (const char *, ptrdiff_t),
			 void (*insert_from_string_func)
			      (Lisp_Object, ptrdiff_t, ptrdiff_t,
			       ptrdiff_t, ptrdiff_t, bool),
			 bool inherit, ptrdiff_t nargs, Lisp_Object *args)
{
  ptrdiff_t argnum;
  Lisp_Object val;

  for (argnum = 0; argnum < nargs; argnum++)
    {
      val = args[argnum];
      if (CHARACTERP (val))
	{
	  int c = XFIXNAT (val);
	  unsigned char str[MAX_MULTIBYTE_LENGTH];
	  int len;

	  if (!NILP (BVAR (current_buffer, enable_multibyte_characters)))
	    len = CHAR_STRING (c, str);
	  else
	    {
	      str[0] = CHAR_TO_BYTE8 (c);
	      len = 1;
	    }
	  (*insert_func) ((char *) str, len);
	}
      else if (STRINGP (val))
	{
	  (*insert_from_string_func) (val, 0, 0,
				      SCHARS (val),
				      SBYTES (val),
				      inherit);
	}
      else
	wrong_type_argument (Qchar_or_string_p, val);
    }
}

void
insert1 (Lisp_Object arg)
{
  Finsert (1, &arg);
}


DEFUN ("insert", Finsert, Sinsert, 0, MANY, 0,
       doc: /* Insert the arguments, either strings or characters, at point.
Point and after-insertion markers move forward to end up
 after the inserted text.
Any other markers at the point of insertion remain before the text.

If the current buffer is multibyte, unibyte strings are converted
to multibyte for insertion (see `string-make-multibyte').
If the current buffer is unibyte, multibyte strings are converted
to unibyte for insertion (see `string-make-unibyte').

When operating on binary data, it may be necessary to preserve the
original bytes of a unibyte string when inserting it into a multibyte
buffer; to accomplish this, apply `decode-coding-string' with the
`no-conversion' coding system to the string and insert the result.

usage: (insert &rest ARGS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  general_insert_function (insert, insert_from_string, 0, nargs, args);
  return Qnil;
}

DEFUN ("insert-and-inherit", Finsert_and_inherit, Sinsert_and_inherit,
   0, MANY, 0,
       doc: /* Insert the arguments at point, inheriting properties from adjoining text.
Point and after-insertion markers move forward to end up
 after the inserted text.
Any other markers at the point of insertion remain before the text.

If the current buffer is multibyte, unibyte strings are converted
to multibyte for insertion (see `unibyte-char-to-multibyte').
If the current buffer is unibyte, multibyte strings are converted
to unibyte for insertion.

usage: (insert-and-inherit &rest ARGS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  general_insert_function (insert_and_inherit, insert_from_string, 1,
			   nargs, args);
  return Qnil;
}

DEFUN ("insert-before-markers", Finsert_before_markers, Sinsert_before_markers, 0, MANY, 0,
       doc: /* Insert strings or characters at point, relocating markers after the text.
Point and markers move forward to end up after the inserted text.

If the current buffer is multibyte, unibyte strings are converted
to multibyte for insertion (see `unibyte-char-to-multibyte').
If the current buffer is unibyte, multibyte strings are converted
to unibyte for insertion.

If an overlay begins at the insertion point, the inserted text falls
outside the overlay; if a nonempty overlay ends at the insertion
point, the inserted text falls inside that overlay.

usage: (insert-before-markers &rest ARGS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  general_insert_function (insert_before_markers,
			   insert_from_string_before_markers, 0,
			   nargs, args);
  return Qnil;
}

DEFUN ("insert-before-markers-and-inherit", Finsert_and_inherit_before_markers,
  Sinsert_and_inherit_before_markers, 0, MANY, 0,
       doc: /* Insert text at point, relocating markers and inheriting properties.
Point and markers move forward to end up after the inserted text.

If the current buffer is multibyte, unibyte strings are converted
to multibyte for insertion (see `unibyte-char-to-multibyte').
If the current buffer is unibyte, multibyte strings are converted
to unibyte for insertion.

usage: (insert-before-markers-and-inherit &rest ARGS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  general_insert_function (insert_before_markers_and_inherit,
			   insert_from_string_before_markers, 1,
			   nargs, args);
  return Qnil;
}

DEFUN ("insert-char", Finsert_char, Sinsert_char, 1, 3,
       "(list (read-char-by-name \"Insert character (Unicode name or hex): \")\
              (prefix-numeric-value current-prefix-arg)\
              t))",
       doc: /* Insert COUNT copies of CHARACTER.

Interactively, prompt for CHARACTER using `read-char-by-name'.
You can specify CHARACTER at the prompt in one of these ways:

 - As its Unicode character name, e.g. \"LATIN SMALL LETTER A\".
   Completion is available; if you type a substring of the name
   preceded by an asterisk `*', Emacs shows all names which include
   that substring, not necessarily at the beginning of the name.

 - As a hexadecimal code point, e.g. 263A.  Note that code points in
   Emacs are equivalent to Unicode up to 10FFFF (which is the limit of
   the Unicode code space).

 - As a code point with a radix specified with #, e.g. #o21430
   (octal), #x2318 (hex), or #10r8984 (decimal).

When called from Lisp, CHARACTER should be an integer whose value
is valid for a character; see `characterp'.  To specify a character by
its Unicode name in calls from Lisp, use `char-from-name'.

When called interactively, COUNT is the prefix argument.  If omitted or
nil, it defaults to 1.

Inserting the character(s) relocates point and before-insertion
markers in the same ways as the function `insert'.

The optional third argument INHERIT, if non-nil, says to inherit text
properties from adjoining text, if those properties are sticky.  When
called interactively, INHERIT is t.  */)
  (Lisp_Object character, Lisp_Object count, Lisp_Object inherit)
{
  int i, stringlen;
  register ptrdiff_t n;
  int c, len;
  unsigned char str[MAX_MULTIBYTE_LENGTH];
  char string[4000];

  CHECK_CHARACTER (character);
  if (NILP (count))
    XSETFASTINT (count, 1);
  else
    CHECK_FIXNUM (count);
  c = XFIXNAT (character);

  if (!NILP (BVAR (current_buffer, enable_multibyte_characters)))
    len = CHAR_STRING (c, str);
  else
    str[0] = c, len = 1;
  if (XFIXNUM (count) <= 0)
    return Qnil;
  if (BUF_BYTES_MAX / len < XFIXNUM (count))
    buffer_overflow ();
  n = XFIXNUM (count) * len;
  stringlen = min (n, sizeof string - sizeof string % len);
  for (i = 0; i < stringlen; i++)
    string[i] = str[i % len];
  while (n > stringlen)
    {
      maybe_quit ();
      if (!NILP (inherit))
	insert_and_inherit (string, stringlen);
      else
	insert (string, stringlen);
      n -= stringlen;
    }
  if (!NILP (inherit))
    insert_and_inherit (string, n);
  else
    insert (string, n);
  return Qnil;
}

DEFUN ("insert-byte", Finsert_byte, Sinsert_byte, 2, 3, 0,
       doc: /* Insert COUNT (second arg) copies of BYTE (first arg).
Both arguments are required.
BYTE is a number of the range 0..255.

If BYTE is 128..255 and the current buffer is multibyte, the
corresponding eight-bit character is inserted.

Point, and before-insertion markers, are relocated as in the function `insert'.
The optional third arg INHERIT, if non-nil, says to inherit text properties
from adjoining text, if those properties are sticky.  */)
  (Lisp_Object byte, Lisp_Object count, Lisp_Object inherit)
{
  CHECK_FIXNUM (byte);
  if (XFIXNUM (byte) < 0 || XFIXNUM (byte) > 255)
    args_out_of_range_3 (byte, make_fixnum (0), make_fixnum (255));
  if (XFIXNUM (byte) >= 128
      && ! NILP (BVAR (current_buffer, enable_multibyte_characters)))
    XSETFASTINT (byte, BYTE8_TO_CHAR (XFIXNUM (byte)));
  return Finsert_char (byte, count, inherit);
}


/* Making strings from buffer contents.  */

/* Return a Lisp_String containing the text of the current buffer from
   START to END.  If text properties are in use and the current buffer
   has properties in the range specified, the resulting string will also
   have them, if PROPS is true.

   We don't want to use plain old make_string here, because it calls
   make_uninit_string, which can cause the buffer arena to be
   compacted.  make_string has no way of knowing that the data has
   been moved, and thus copies the wrong data into the string.  This
   doesn't affect most of the other users of make_string, so it should
   be left as is.  But we should use this function when conjuring
   buffer substrings.  */

Lisp_Object
make_buffer_string (ptrdiff_t start, ptrdiff_t end, bool props)
{
  ptrdiff_t start_byte = CHAR_TO_BYTE (start);
  ptrdiff_t end_byte = CHAR_TO_BYTE (end);

  return make_buffer_string_both (start, start_byte, end, end_byte, props);
}

/* Return a Lisp_String containing the text of the current buffer from
   START / START_BYTE to END / END_BYTE.

   If text properties are in use and the current buffer
   has properties in the range specified, the resulting string will also
   have them, if PROPS is true.

   We don't want to use plain old make_string here, because it calls
   make_uninit_string, which can cause the buffer arena to be
   compacted.  make_string has no way of knowing that the data has
   been moved, and thus copies the wrong data into the string.  This
   doesn't effect most of the other users of make_string, so it should
   be left as is.  But we should use this function when conjuring
   buffer substrings.  */

Lisp_Object
make_buffer_string_both (ptrdiff_t start, ptrdiff_t start_byte,
			 ptrdiff_t end, ptrdiff_t end_byte, bool props)
{
  Lisp_Object result, tem, tem1;
  ptrdiff_t beg0, end0, beg1, end1, size;

  if (start_byte < GPT_BYTE && GPT_BYTE < end_byte)
    {
      /* Two regions, before and after the gap.  */
      beg0 = start_byte;
      end0 = GPT_BYTE;
      beg1 = GPT_BYTE + GAP_SIZE - BEG_BYTE;
      end1 = end_byte + GAP_SIZE - BEG_BYTE;
    }
  else
    {
      /* The only region.  */
      beg0 = start_byte;
      end0 = end_byte;
      beg1 = -1;
      end1 = -1;
    }

  if (! NILP (BVAR (current_buffer, enable_multibyte_characters)))
    result = make_uninit_multibyte_string (end - start, end_byte - start_byte);
  else
    result = make_uninit_string (end - start);

  size = end0 - beg0;
  memcpy (SDATA (result), BYTE_POS_ADDR (beg0), size);
  if (beg1 != -1)
    memcpy (SDATA (result) + size, BEG_ADDR + beg1, end1 - beg1);

  /* If desired, update and copy the text properties.  */
  if (props)
    {
      update_buffer_properties (start, end);

      tem = Fnext_property_change (make_fixnum (start), Qnil, make_fixnum (end));
      tem1 = Ftext_properties_at (make_fixnum (start), Qnil);

      if (XFIXNUM (tem) != end || !NILP (tem1))
	copy_intervals_to_string (result, current_buffer, start,
				  end - start);
    }

  return result;
}

/* Call Vbuffer_access_fontify_functions for the range START ... END
   in the current buffer, if necessary.  */

static void
update_buffer_properties (ptrdiff_t start, ptrdiff_t end)
{
  /* If this buffer has some access functions,
     call them, specifying the range of the buffer being accessed.  */
  if (!NILP (Vbuffer_access_fontify_functions))
    {
      /* But don't call them if we can tell that the work
	 has already been done.  */
      if (!NILP (Vbuffer_access_fontified_property))
	{
	  Lisp_Object tem
	    = Ftext_property_any (make_fixnum (start), make_fixnum (end),
				  Vbuffer_access_fontified_property,
				  Qnil, Qnil);
	  if (NILP (tem))
	    return;
	}

      CALLN (Frun_hook_with_args, Qbuffer_access_fontify_functions,
	     make_fixnum (start), make_fixnum (end));
    }
}

DEFUN ("buffer-substring", Fbuffer_substring, Sbuffer_substring, 2, 2, 0,
       doc: /* Return the contents of part of the current buffer as a string.
The two arguments START and END are character positions;
they can be in either order.
The string returned is multibyte if the buffer is multibyte.

This function copies the text properties of that part of the buffer
into the result string; if you don't want the text properties,
use `buffer-substring-no-properties' instead.  */)
  (Lisp_Object start, Lisp_Object end)
{
  register ptrdiff_t b, e;

  validate_region (&start, &end);
  b = XFIXNUM (start);
  e = XFIXNUM (end);

  return make_buffer_string (b, e, 1);
}

DEFUN ("buffer-substring-no-properties", Fbuffer_substring_no_properties,
       Sbuffer_substring_no_properties, 2, 2, 0,
       doc: /* Return the characters of part of the buffer, without the text properties.
The two arguments START and END are character positions;
they can be in either order.  */)
  (Lisp_Object start, Lisp_Object end)
{
  register ptrdiff_t b, e;

  validate_region (&start, &end);
  b = XFIXNUM (start);
  e = XFIXNUM (end);

  return make_buffer_string (b, e, 0);
}

DEFUN ("buffer-string", Fbuffer_string, Sbuffer_string, 0, 0, 0,
       doc: /* Return the contents of the current buffer as a string.
If narrowing is in effect, this function returns only the visible part
of the buffer.

This function copies the text properties of that part of the buffer
into the result string; if you don’t want the text properties,
use `buffer-substring-no-properties' instead.  */)
  (void)
{
  return make_buffer_string_both (BEGV, BEGV_BYTE, ZV, ZV_BYTE, 1);
}

DEFUN ("insert-buffer-substring", Finsert_buffer_substring, Sinsert_buffer_substring,
       1, 3, 0,
       doc: /* Insert before point a substring of the contents of BUFFER.
BUFFER may be a buffer or a buffer name.
Arguments START and END are character positions specifying the substring.
They default to the values of (point-min) and (point-max) in BUFFER.

Point and before-insertion markers move forward to end up after the
inserted text.
Any other markers at the point of insertion remain before the text.

If the current buffer is multibyte and BUFFER is unibyte, or vice
versa, strings are converted from unibyte to multibyte or vice versa
using `string-make-multibyte' or `string-make-unibyte', which see.  */)
  (Lisp_Object buffer, Lisp_Object start, Lisp_Object end)
{
  register EMACS_INT b, e, temp;
  register struct buffer *bp, *obuf;
  Lisp_Object buf;

  buf = Fget_buffer (buffer);
  if (NILP (buf))
    nsberror (buffer);
  bp = XBUFFER (buf);
  if (!BUFFER_LIVE_P (bp))
    error ("Selecting deleted buffer");

  b = !NILP (start) ? fix_position (start) : BUF_BEGV (bp);
  e = !NILP (end) ? fix_position (end) : BUF_ZV (bp);
  if (b > e)
    temp = b, b = e, e = temp;

  if (!(BUF_BEGV (bp) <= b && e <= BUF_ZV (bp)))
    args_out_of_range (start, end);

  obuf = current_buffer;
  set_buffer_internal_1 (bp);
  update_buffer_properties (b, e);
  set_buffer_internal_1 (obuf);

  insert_from_buffer (bp, b, e - b, 0);
  return Qnil;
}

DEFUN ("compare-buffer-substrings", Fcompare_buffer_substrings, Scompare_buffer_substrings,
       6, 6, 0,
       doc: /* Compare two substrings of two buffers; return result as number.
Return -N if first string is less after N-1 chars, +N if first string is
greater after N-1 chars, or 0 if strings match.
The first substring is in BUFFER1 from START1 to END1 and the second
is in BUFFER2 from START2 to END2.
All arguments may be nil.  If BUFFER1 or BUFFER2 is nil, the current
buffer is used.  If START1 or START2 is nil, the value of `point-min'
in the respective buffers is used.  If END1 or END2 is nil, the value
of `point-max' in the respective buffers is used.
The value of `case-fold-search' in the current buffer
determines whether case is significant or ignored.  */)
  (Lisp_Object buffer1, Lisp_Object start1, Lisp_Object end1, Lisp_Object buffer2, Lisp_Object start2, Lisp_Object end2)
{
  register EMACS_INT begp1, endp1, begp2, endp2, temp;
  register struct buffer *bp1, *bp2;
  register Lisp_Object trt
    = (!NILP (Vcase_fold_search)
       ? BVAR (current_buffer, case_canon_table) : Qnil);
  ptrdiff_t chars = 0;
  ptrdiff_t i1, i2, i1_byte, i2_byte;

  /* Find the first buffer and its substring.  */

  if (NILP (buffer1))
    bp1 = current_buffer;
  else
    {
      Lisp_Object buf1;
      buf1 = Fget_buffer (buffer1);
      if (NILP (buf1))
	nsberror (buffer1);
      bp1 = XBUFFER (buf1);
      if (!BUFFER_LIVE_P (bp1))
	error ("Selecting deleted buffer");
    }

  begp1 = !NILP (start1) ? fix_position (start1) : BUF_BEGV (bp1);
  endp1 = !NILP (end1) ? fix_position (end1) : BUF_ZV (bp1);
  if (begp1 > endp1)
    temp = begp1, begp1 = endp1, endp1 = temp;

  if (!(BUF_BEGV (bp1) <= begp1
	&& begp1 <= endp1
        && endp1 <= BUF_ZV (bp1)))
    args_out_of_range (start1, end1);

  /* Likewise for second substring.  */

  if (NILP (buffer2))
    bp2 = current_buffer;
  else
    {
      Lisp_Object buf2;
      buf2 = Fget_buffer (buffer2);
      if (NILP (buf2))
	nsberror (buffer2);
      bp2 = XBUFFER (buf2);
      if (!BUFFER_LIVE_P (bp2))
	error ("Selecting deleted buffer");
    }

  begp2 = !NILP (start2) ? fix_position (start2) : BUF_BEGV (bp2);
  endp2 = !NILP (end2) ? fix_position (end2) : BUF_ZV (bp2);
  if (begp2 > endp2)
    temp = begp2, begp2 = endp2, endp2 = temp;

  if (!(BUF_BEGV (bp2) <= begp2
	&& begp2 <= endp2
        && endp2 <= BUF_ZV (bp2)))
    args_out_of_range (start2, end2);

  i1 = begp1;
  i2 = begp2;
  i1_byte = buf_charpos_to_bytepos (bp1, i1);
  i2_byte = buf_charpos_to_bytepos (bp2, i2);

  while (i1 < endp1 && i2 < endp2)
    {
      /* When we find a mismatch, we must compare the
	 characters, not just the bytes.  */
      int c1, c2;

      if (! NILP (BVAR (bp1, enable_multibyte_characters)))
	{
	  c1 = BUF_FETCH_MULTIBYTE_CHAR (bp1, i1_byte);
	  i1_byte += buf_next_char_len (bp1, i1_byte);
	  i1++;
	}
      else
	{
	  c1 = make_char_multibyte (BUF_FETCH_BYTE (bp1, i1));
	  i1++;
	}

      if (! NILP (BVAR (bp2, enable_multibyte_characters)))
	{
	  c2 = BUF_FETCH_MULTIBYTE_CHAR (bp2, i2_byte);
	  i2_byte += buf_next_char_len (bp2, i2_byte);
	  i2++;
	}
      else
	{
	  c2 = make_char_multibyte (BUF_FETCH_BYTE (bp2, i2));
	  i2++;
	}

      if (!NILP (trt))
	{
	  c1 = char_table_translate (trt, c1);
	  c2 = char_table_translate (trt, c2);
	}

      if (c1 != c2)
	return make_fixnum (c1 < c2 ? -1 - chars : chars + 1);

      chars++;
      rarely_quit (chars);
    }

  /* The strings match as far as they go.
     If one is shorter, that one is less.  */
  if (chars < endp1 - begp1)
    return make_fixnum (chars + 1);
  else if (chars < endp2 - begp2)
    return make_fixnum (- chars - 1);

  /* Same length too => they are equal.  */
  return make_fixnum (0);
}


/* Set up necessary definitions for diffseq.h; see comments in
   diffseq.h for explanation.  */

#undef ELEMENT
#undef EQUAL
#define USE_HEURISTIC

#define XVECREF_YVECREF_EQUAL(ctx, xoff, yoff)  \
  buffer_chars_equal (ctx, xoff, yoff)

#define OFFSET ptrdiff_t

#define EXTRA_CONTEXT_FIELDS                    \
  /* Buffers to compare.  */                    \
  struct buffer *buffer_a;                      \
  struct buffer *buffer_b;                      \
  /* BEGV of each buffer */			\
  ptrdiff_t beg_a;				\
  ptrdiff_t beg_b;				\
  /* Whether each buffer is unibyte/plain-ASCII or not.  */ \
  bool a_unibyte;				\
  bool b_unibyte;				\
  /* Bit vectors recording for each character whether it was deleted
     or inserted.  */                           \
  unsigned char *deletions;                     \
  unsigned char *insertions;			\
  struct timespec time_limit;			\
  sys_jmp_buf jmp;				\
  unsigned short quitcounter;

#define NOTE_DELETE(ctx, xoff) set_bit ((ctx)->deletions, xoff)
#define NOTE_INSERT(ctx, yoff) set_bit ((ctx)->insertions, yoff)
#define EARLY_ABORT(ctx) compareseq_early_abort (ctx)

struct context;
static void set_bit (unsigned char *, OFFSET);
static bool bit_is_set (const unsigned char *, OFFSET);
static bool buffer_chars_equal (struct context *, OFFSET, OFFSET);
static bool compareseq_early_abort (struct context *);

#include "minmax.h"
#include "diffseq.h"

DEFUN ("replace-region-contents", Freplace_region_contents,
       Sreplace_region_contents, 3, 6,
       "(list (if (use-region-p) (region-beginning) (point-min)) \
              (if (use-region-p) (region-end) (point-max)) 	 \
              (get-buffer (read-buffer-to-switch \"Source buffer: \")))",
       doc: /* Replace the region between BEG and END with that of SOURCE.
SOURCE can be a buffer, a string, or a vector [SBUF SBEG SEND]
denoting the subtring SBEG..SEND of buffer SBUF.

If optional argument INHERIT is non-nil, the inserted text will inherit
properties from adjoining text.

As far as possible the replacement is non-destructive, i.e. existing
buffer contents, markers, point, properties, and overlays in the current
buffer stay intact.  Point is treated like an "insert before" marker:
if point starts at END, it will always be at the end of the replacement
when this function returns, whereas if point starts at BEG it will
remain at BEG only if the replaced text is not empty.

Because this function can be very slow if there is a large number of
differences between the two buffers, there are two optional arguments
mitigating this issue.

The MAX-SECS argument, if given, defines a hard limit on the time used
for comparing the buffers.  If it takes longer than MAX-SECS, the
function falls back to a plain `delete-region' and
`insert-buffer-substring'.  (Note that the checks are not performed
too evenly over time, so in some cases it may run a bit longer than
allowed).  In partricular, passing zero as the value of MAX-SECS
disables the comparison step, so this function immediately falls
back to a plain delete/insert method.

The optional argument MAX-COSTS defines the quality of the difference
computation.  If the actual costs exceed this limit, heuristics are
used to provide a faster but suboptimal solution.  The default value
is 1000000.

Note: If the replacement is a string, it'll usually be placed internally
in a temporary buffer.  Therefore, all else being equal, it is preferable
to pass a buffer rather than a string as SOURCE argument.

This function returns t if a non-destructive replacement could be
performed.  Otherwise, i.e., if MAX-SECS was exceeded, it returns
nil.

SOURCE can also be a function that will be called with no arguments
and with current buffer narrowed to BEG..END, and should return
a buffer or a string.  But this is deprecated.  */)
  (Lisp_Object beg, Lisp_Object end, Lisp_Object source,
   Lisp_Object max_secs, Lisp_Object max_costs, Lisp_Object inherit)
{
  validate_region (&beg, &end);
  ptrdiff_t min_a = XFIXNUM (beg);
  ptrdiff_t size_a = XFIXNUM (end) - min_a;
  eassume (size_a >= 0);
  bool a_empty = size_a == 0;
  bool inh = !NILP (inherit);

  if (FUNCTIONP (source))
    {
      specpdl_ref count = SPECPDL_INDEX ();
      record_unwind_protect_excursion ();
      record_unwind_protect (save_restriction_restore,
			     save_restriction_save ());
      Fnarrow_to_region (beg, end);
      source = calln (source);
      unbind_to (count, Qnil);
    }
  ptrdiff_t min_b, size_b;
  struct buffer *b;
  if (STRINGP (source))
    {
      b = NULL;
      min_b = BEG;		/* Assuming we'll copy it into a buffer.  */
      /* Like 'size_b = SCHARS (source);', except inline to pacify -Wclobbered
	 with gcc 14.2.1 20250110 (Red Hat 14.2.1-7) x86-64 -O2; see
	 <https://gcc.gnu.org/bugzilla/show_bug.cgi?id=21161>.  */
      size_b = XSTRING (source)->u.s.size;
    }
  else if (BUFFERP (source))
    {
      b = XBUFFER (source);
      min_b = BUF_BEGV (b);
      size_b = BUF_ZV (b) - min_b;
    }
  else
    {
      CHECK_TYPE (VECTORP (source),
		  list (Qor, Qstring, Qbuffer, Qvector), source);
      /* Let Faref signal an error if SOURCE is too small.  */
      Lisp_Object send = Faref (source, make_fixnum (2));
      Lisp_Object sbeg = AREF (source, 1);
      CHECK_BUFFER (AREF (source, 0));
      b = XBUFFER (AREF (source, 0));
      specpdl_ref count = SPECPDL_INDEX ();
      record_unwind_current_buffer ();
      set_buffer_internal (b);
      validate_region (&sbeg, &send);
      unbind_to (count, Qnil);
      min_b = XFIXNUM (sbeg);
      size_b = XFIXNUM (send) - min_b;
    }
  eassume (0 <= size_b);
  bool b_empty = size_b == 0;
  if (b && !BUFFER_LIVE_P (b))
    error ("Selecting deleted buffer");

  /* Handle trivial cases where at least one accessible portion is
     empty.  */

  if (a_empty && b_empty)
    return Qt;
  else if (a_empty || b_empty
	   || EQ (max_secs, make_fixnum (0))
	   || EQ (max_costs, make_fixnum (0)))
    {
      replace_range (min_a, min_a + size_a, source, true, false, inh);
      return Qt;
    }

  struct buffer *a = current_buffer;
  if (a == b)
    error ("Cannot replace a buffer with itself");

  ptrdiff_t too_expensive;
  if (NILP (max_costs))
    too_expensive = 1000000;
  else if (FIXNUMP (max_costs))
    too_expensive = clip_to_bounds (0, XFIXNUM (max_costs), PTRDIFF_MAX);
  else
    {
      CHECK_INTEGER (max_costs);
      too_expensive = NILP (Fnatnump (max_costs)) ? 0 : PTRDIFF_MAX;
    }

  struct timespec time_limit = make_timespec (0, -1);
  if (!NILP (max_secs))
    {
      struct timespec
	tlim = timespec_add (current_timespec (),
			     lisp_time_argument (max_secs)),
	tmax = make_timespec (TYPE_MAXIMUM (time_t), TIMESPEC_HZ - 1);
      if (timespec_cmp (tlim, tmax) < 0)
	time_limit = tlim;
    }

  specpdl_ref count = SPECPDL_INDEX ();

  ptrdiff_t diags = size_a + size_b + 3;
  ptrdiff_t del_bytes = size_a / CHAR_BIT + 1;
  ptrdiff_t ins_bytes = size_b / CHAR_BIT + 1;
  ptrdiff_t *buffer;
  ptrdiff_t bytes_needed;
  if (ckd_mul (&bytes_needed, diags, 2 * sizeof *buffer)
      || ckd_add (&bytes_needed, bytes_needed, del_bytes + ins_bytes))
    memory_full (SIZE_MAX);
  USE_SAFE_ALLOCA;
  buffer = SAFE_ALLOCA (bytes_needed);
  unsigned char *deletions_insertions = memset (buffer + 2 * diags, 0,
						del_bytes + ins_bytes);

  /* The rest of the code is not prepared to handle a string SOURCE.  */
  if (!b)
    {
      Lisp_Object workbuf
	= code_conversion_save (true, STRING_MULTIBYTE (source));
      b = XBUFFER (workbuf);
      set_buffer_internal (b);
      CALLN (Finsert, source);
      set_buffer_internal (a);
    }
  Lisp_Object source_buffer = make_lisp_ptr (b, Lisp_Vectorlike);

  /* FIXME: It is not documented how to initialize the contents of the
     context structure.  This code cargo-cults from the existing
     caller in src/analyze.c of GNU Diffutils, which appears to
     work.  */
  struct context ctx = {
    .buffer_a = a,
    .buffer_b = b,
    .beg_a = min_a,
    .beg_b = min_b,
    .a_unibyte = BUF_ZV (a) == BUF_ZV_BYTE (a),
    .b_unibyte = BUF_ZV (b) == BUF_ZV_BYTE (b),
    .deletions = deletions_insertions,
    .insertions = deletions_insertions + del_bytes,
    .fdiag = buffer + size_b + 1,
    .bdiag = buffer + diags + size_b + 1,
    .heuristic = true,
    .too_expensive = too_expensive,
    .time_limit = time_limit,
  };

  /* compareseq requires indices to be zero-based.  We add BEGV back
     later.  */
  bool early_abort;
  if (! sys_setjmp (ctx.jmp))
    early_abort = compareseq (0, size_a, 0, size_b, false, &ctx);
  else
    early_abort = true;

  if (early_abort)
    {
      Lisp_Object src = CALLN (Fvector, source_buffer,
			       make_fixnum (BUF_BEGV (b)),
			       make_fixnum (BUF_ZV (b)));
      replace_range (min_a, min_a + size_a, src, true, false, inh);
      SAFE_FREE_UNBIND_TO (count, Qnil);
      return Qnil;
    }

  Fundo_boundary ();
  bool modification_hooks_inhibited = false;

  /* We are going to make a lot of small modifications, and having the
     modification hooks called for each of them will slow us down.
     Instead, we announce a single modification for the entire
     modified region.  But don't do that if the caller inhibited
     modification hooks, because then they don't want that.  */
  if (!inhibit_modification_hooks)
    {
      prepare_to_modify_buffer (min_a, min_a + size_a, NULL);
      specbind (Qinhibit_modification_hooks, Qt);
      modification_hooks_inhibited = true;
    }

  ptrdiff_t i = size_a;
  ptrdiff_t j = size_b;
  Lisp_Object src = CALLN (Fvector, source_buffer, Qnil, Qnil);
  /* Walk backwards through the lists of changes.  This was also
     cargo-culted from src/analyze.c in GNU Diffutils.  Because we
     walk backwards, we don’t have to keep the positions in sync.  */
  while (i >= 0 || j >= 0)
    {
      rarely_quit (++ctx.quitcounter);

      /* Check whether there is a change (insertion or deletion)
         before the current position.  */
      if ((i > 0 && bit_is_set (ctx.deletions, i - 1))
	  || (j > 0 && bit_is_set (ctx.insertions, j - 1)))
	{
          ptrdiff_t end_a = min_a + i;
          ptrdiff_t end_b = min_b + j;
          /* Find the beginning of the current change run.  */
	  while (i > 0 && bit_is_set (ctx.deletions, i - 1))
            --i;
	  while (j > 0 && bit_is_set (ctx.insertions, j - 1))
            --j;

          ptrdiff_t beg_a = min_a + i;
          ptrdiff_t beg_b = min_b + j;
          eassert (beg_a <= end_a);
          eassert (beg_b <= end_b);
          eassert (beg_a < end_a || beg_b < end_b);
          ASET (src, 1, make_fixed_natnum (beg_b));
          ASET (src, 2, make_fixed_natnum (end_b));
          replace_range (beg_a, end_a, src, true, false, inh);
	}
      --i;
      --j;
    }

  SAFE_FREE_UNBIND_TO (count, Qnil);

  if (modification_hooks_inhibited)
    {
      signal_after_change (min_a, size_a, size_b);
      update_compositions (min_a, min_a + size_b, CHECK_INSIDE);
      /* We've locked the buffer's file above in
	 prepare_to_modify_buffer; if the buffer is unchanged at this
	 point, i.e. no insertions or deletions have been made, unlock
	 the file now.  */
      if (SAVE_MODIFF == MODIFF
	  && STRINGP (BVAR (a, file_truename)))
	Funlock_file (BVAR (a, file_truename));
    }

  return Qt;
}

static void
set_bit (unsigned char *a, ptrdiff_t i)
{
  eassume (0 <= i);
  a[i / CHAR_BIT] |= (1 << (i % CHAR_BIT));
}

static bool
bit_is_set (const unsigned char *a, ptrdiff_t i)
{
  eassume (0 <= i);
  return a[i / CHAR_BIT] & (1 << (i % CHAR_BIT));
}

/* Return true if the characters at position POS_A of buffer
   CTX->buffer_a and at position POS_B of buffer CTX->buffer_b are
   equal.  POS_A and POS_B are zero-based.  Text properties are
   ignored.

   Implementation note: this function is called inside the inner-most
   loops of compareseq, so it absolutely must be optimized for speed,
   every last bit of it.  E.g., each additional use of BEGV or such
   likes will slow down replace-buffer-contents by dozens of percents,
   because builtin_lisp_symbol will be called one more time in the
   innermost loop.  */

static bool
buffer_chars_equal (struct context *ctx,
                    ptrdiff_t pos_a, ptrdiff_t pos_b)
{
  if (!++ctx->quitcounter)
    {
      maybe_quit ();
      if (compareseq_early_abort (ctx))
	sys_longjmp (ctx->jmp, 1);
    }

  pos_a += ctx->beg_a;
  pos_b += ctx->beg_b;

  ptrdiff_t bpos_a =
    ctx->a_unibyte ? pos_a : buf_charpos_to_bytepos (ctx->buffer_a, pos_a);
  ptrdiff_t bpos_b =
    ctx->b_unibyte ? pos_b : buf_charpos_to_bytepos (ctx->buffer_b, pos_b);

  /* We make the below a series of specific test to avoid using
     BUF_FETCH_CHAR_AS_MULTIBYTE, which references Lisp symbols, and
     is therefore significantly slower (see the note in the commentary
     to this function).  */
  if (ctx->a_unibyte && ctx->b_unibyte)
    return BUF_FETCH_BYTE (ctx->buffer_a, bpos_a)
      == BUF_FETCH_BYTE (ctx->buffer_b, bpos_b);
  if (ctx->a_unibyte && !ctx->b_unibyte)
    return UNIBYTE_TO_CHAR (BUF_FETCH_BYTE (ctx->buffer_a, bpos_a))
      == BUF_FETCH_MULTIBYTE_CHAR (ctx->buffer_b, bpos_b);
  if (!ctx->a_unibyte && ctx->b_unibyte)
    return BUF_FETCH_MULTIBYTE_CHAR (ctx->buffer_a, bpos_a)
      == UNIBYTE_TO_CHAR (BUF_FETCH_BYTE (ctx->buffer_b, bpos_b));
  return BUF_FETCH_MULTIBYTE_CHAR (ctx->buffer_a, bpos_a)
    == BUF_FETCH_MULTIBYTE_CHAR (ctx->buffer_b, bpos_b);
}

static bool
compareseq_early_abort (struct context *ctx)
{
  if (ctx->time_limit.tv_nsec < 0)
    return false;
  return timespec_cmp (ctx->time_limit, current_timespec ()) < 0;
}


static void
subst_char_in_region_unwind (Lisp_Object arg)
{
  bset_undo_list (current_buffer, arg);
}

static void
subst_char_in_region_unwind_1 (Lisp_Object arg)
{
  bset_filename (current_buffer, arg);
}

DEFUN ("subst-char-in-region", Fsubst_char_in_region,
       Ssubst_char_in_region, 4, 5, 0,
       doc: /* From START to END, replace FROMCHAR with TOCHAR each time it occurs.
If optional arg NOUNDO is non-nil, don't record this change for undo
and don't mark the buffer as really changed.
Both characters must have the same length of multi-byte form.  */)
  (Lisp_Object start, Lisp_Object end, Lisp_Object fromchar, Lisp_Object tochar, Lisp_Object noundo)
{
  register ptrdiff_t pos, pos_byte, stop, i, len, end_byte;
  /* Keep track of the first change in the buffer:
     if 0 we haven't found it yet.
     if < 0 we've found it and we've run the before-change-function.
     if > 0 we've actually performed it and the value is its position.  */
  ptrdiff_t changed = 0;
  unsigned char fromstr[MAX_MULTIBYTE_LENGTH], tostr[MAX_MULTIBYTE_LENGTH];
  unsigned char *p;
  specpdl_ref count = SPECPDL_INDEX ();
  ptrdiff_t last_changed = 0;
  bool multibyte_p
    = !NILP (BVAR (current_buffer, enable_multibyte_characters));
  int fromc, toc;

#ifdef HAVE_TREE_SITTER
  ptrdiff_t start_char = fix_position (start);
  ptrdiff_t old_end_char = fix_position (end);
  ptrdiff_t start_byte = CHAR_TO_BYTE (start_char);
  ptrdiff_t old_end_byte = CHAR_TO_BYTE (old_end_char);
  struct ts_linecol start_linecol
    = treesit_linecol_maybe (start_char, start_byte,
			     BUF_TS_LINECOL_POINT (current_buffer));
  struct ts_linecol old_end_linecol
    = treesit_linecol_maybe (old_end_char, old_end_byte,
			     BUF_TS_LINECOL_POINT (current_buffer));
#endif

 restart:

  validate_region (&start, &end);
  CHECK_CHARACTER (fromchar);
  CHECK_CHARACTER (tochar);
  fromc = XFIXNAT (fromchar);
  toc = XFIXNAT (tochar);

  if (multibyte_p)
    {
      len = CHAR_STRING (fromc, fromstr);
      if (CHAR_STRING (toc, tostr) != len)
	error ("Characters in `subst-char-in-region' have different byte-lengths");
    }
  else
    {
      len = 1;
      fromstr[0] = fromc;
      tostr[0] = toc;
    }

  pos = XFIXNUM (start);
  pos_byte = CHAR_TO_BYTE (pos);
  stop = CHAR_TO_BYTE (XFIXNUM (end));
  end_byte = stop;

  /* If we don't want undo, turn off putting stuff on the list.
     That's faster than getting rid of things,
     and it prevents even the entry for a first change.
     Also inhibit locking the file.  */
  if (!changed && !NILP (noundo))
    {
      record_unwind_protect (subst_char_in_region_unwind,
			     BVAR (current_buffer, undo_list));
      bset_undo_list (current_buffer, Qt);
      /* Don't do file-locking.  */
      record_unwind_protect (subst_char_in_region_unwind_1,
			     BVAR (current_buffer, filename));
      bset_filename (current_buffer, Qnil);
    }

  if (pos_byte < GPT_BYTE)
    stop = min (stop, GPT_BYTE);
  while (1)
    {
      ptrdiff_t pos_byte_next = pos_byte;

      if (pos_byte >= stop)
	{
	  if (pos_byte >= end_byte) break;
	  stop = end_byte;
	}
      p = BYTE_POS_ADDR (pos_byte);
      if (multibyte_p)
	pos_byte_next += next_char_len (pos_byte_next);
      else
	++pos_byte_next;
      if (pos_byte_next - pos_byte == len
	  && p[0] == fromstr[0]
	  && (len == 1
	      || (p[1] == fromstr[1]
		  && (len == 2 || (p[2] == fromstr[2]
				 && (len == 3 || p[3] == fromstr[3]))))))
	{
	  if (changed < 0)
	    /* We've already seen this and run the before-change-function;
	       this time we only need to record the actual position. */
	    changed = pos;
	  else if (!changed)
	    {
	      changed = -1;
	      modify_text (pos, XFIXNUM (end));

	      if (! NILP (noundo))
		{
		  modiff_count m = MODIFF;
		  if (SAVE_MODIFF == m - 1)
		    SAVE_MODIFF = m;
		  if (BUF_AUTOSAVE_MODIFF (current_buffer) == m - 1)
		    BUF_AUTOSAVE_MODIFF (current_buffer) = m;
		}

	      /* The before-change-function may have moved the gap
		 or even modified the buffer so we should start over. */
	      goto restart;
	    }

	  if (NILP (noundo))
	    record_change (pos, 1);
	  for (i = 0; i < len; i++) *p++ = tostr[i];

	  last_changed =  pos + 1;
	}
      pos_byte = pos_byte_next;
      pos++;
    }

  if (changed > 0)
    {
#ifdef HAVE_TREE_SITTER
      treesit_record_change (start_byte, old_end_byte, old_end_byte,
			     start_linecol, old_end_linecol, old_end_char);
#endif
      signal_after_change (changed,
			   last_changed - changed, last_changed - changed);
      update_compositions (changed, last_changed, CHECK_ALL);
    }

  return unbind_to (count, Qnil);
}


static Lisp_Object check_translation (ptrdiff_t, ptrdiff_t, ptrdiff_t,
				      Lisp_Object);

/* Helper function for Ftranslate_region_internal.

   Check if a character sequence at POS (POS_BYTE) matches an element
   of VAL.  VAL is a list (([FROM-CHAR ...] . TO) ...).  If a matching
   element is found, return it.  Otherwise return Qnil.  */

static Lisp_Object
check_translation (ptrdiff_t pos, ptrdiff_t pos_byte, ptrdiff_t end,
		   Lisp_Object val)
{
  int initial_buf[16];
  int *buf = initial_buf;
  ptrdiff_t buf_size = ARRAYELTS (initial_buf);
  int *bufalloc = 0;
  ptrdiff_t buf_used = 0;
  Lisp_Object result = Qnil;

  for (; CONSP (val); val = XCDR (val))
    {
      Lisp_Object elt;
      ptrdiff_t len, i;

      elt = XCAR (val);
      if (! CONSP (elt))
	continue;
      elt = XCAR (elt);
      if (! VECTORP (elt))
	continue;
      len = ASIZE (elt);
      if (len <= end - pos)
	{
	  for (i = 0; i < len; i++)
	    {
	      if (buf_used <= i)
		{
		  unsigned char *p = BYTE_POS_ADDR (pos_byte);
		  int len1;

		  if (buf_used == buf_size)
		    {
		      bufalloc = xpalloc (bufalloc, &buf_size, 1, -1,
					  sizeof *bufalloc);
		      if (buf == initial_buf)
			memcpy (bufalloc, buf, sizeof initial_buf);
		      buf = bufalloc;
		    }
		  buf[buf_used++] = string_char_and_length (p, &len1);
		  pos_byte += len1;
		}
	      if (XFIXNUM (AREF (elt, i)) != buf[i])
		break;
	    }
	  if (i == len)
	    {
	      result = XCAR (val);
	      break;
	    }
	}
    }

  xfree (bufalloc);
  return result;
}


DEFUN ("translate-region-internal", Ftranslate_region_internal,
       Stranslate_region_internal, 3, 3, 0,
       doc: /* Internal use only.
From START to END, translate characters according to TABLE.
TABLE is a string or a char-table; the Nth character in it is the
mapping for the character with code N.
It returns the number of characters changed.  */)
  (Lisp_Object start, Lisp_Object end, Lisp_Object table)
{
  int translatable_chars = MAX_CHAR + 1;
  bool multibyte = !NILP (BVAR (current_buffer, enable_multibyte_characters));
  bool string_multibyte UNINIT;

  validate_region (&start, &end);
  if (STRINGP (table))
    {
      if (! multibyte)
	table = string_make_unibyte (table);
      translatable_chars = min (translatable_chars, SBYTES (table));
      string_multibyte = STRING_MULTIBYTE (table);
    }
  else if (! (CHAR_TABLE_P (table)
	      && EQ (XCHAR_TABLE (table)->purpose, Qtranslation_table)))
    error ("Not a translation table");

  ptrdiff_t pos = XFIXNUM (start);
  ptrdiff_t pos_byte = CHAR_TO_BYTE (pos);
  ptrdiff_t end_pos = XFIXNUM (end);
  modify_text (pos, end_pos);

  ptrdiff_t characters_changed = 0;

  while (pos < end_pos)
    {
      unsigned char *p = BYTE_POS_ADDR (pos_byte);
      unsigned char *str UNINIT;
      unsigned char buf[MAX_MULTIBYTE_LENGTH];
      int len, oc;

      if (multibyte)
	oc = string_char_and_length (p, &len);
      else
	oc = *p, len = 1;
      if (oc < translatable_chars)
	{
	  int nc; /* New character.  */
	  int str_len UNINIT;
	  Lisp_Object val;

	  if (STRINGP (table))
	    {
	      /* Reload as signal_after_change in last iteration may GC.  */
	      unsigned char *tt = SDATA (table);

	      if (string_multibyte)
		{
		  str = tt + string_char_to_byte (table, oc);
		  nc = string_char_and_length (str, &str_len);
		}
	      else
		{
		  nc = tt[oc];
		  if (! ASCII_CHAR_P (nc) && multibyte)
		    {
		      str_len = BYTE8_STRING (nc, buf);
		      str = buf;
		    }
		  else
		    {
		      str_len = 1;
		      str = tt + oc;
		    }
		}
	    }
	  else
	    {
	      nc = oc;
	      val = CHAR_TABLE_REF (table, oc);
	      if (CHARACTERP (val))
		{
		  nc = XFIXNAT (val);
		  str_len = CHAR_STRING (nc, buf);
		  str = buf;
		}
	      else if (VECTORP (val) || (CONSP (val)))
		{
		  /* VAL is [TO_CHAR ...] or (([FROM-CHAR ...] .  TO) ...)
		     where TO is TO-CHAR or [TO-CHAR ...].  */
		  nc = -1;
		}
	    }

	  if (nc != oc && nc >= 0)
	    {
	      /* Simple one char to one char translation.  */
	      if (len != str_len)
		{
		  Lisp_Object string;

		  /* This is less efficient, because it moves the gap,
		     but it should handle multibyte characters correctly.  */
		  string = make_multibyte_string ((char *) str, 1, str_len);
		  replace_range (pos, pos + 1, string,
				 true, false, false);
		  len = str_len;
		}
	      else
		{
#ifdef HAVE_TREE_SITTER
		  struct ts_linecol linecol_cache
		    = BUF_TS_LINECOL_POINT (current_buffer);
		  struct ts_linecol start_linecol
		    = treesit_linecol_maybe (pos, pos_byte, linecol_cache);
		  struct ts_linecol old_end_linecol
		    = treesit_linecol_maybe (pos + 1, pos_byte + len,
					     start_linecol);
#endif
		  record_change (pos, 1);
		  while (str_len-- > 0)
		    *p++ = *str++;
		  signal_after_change (pos, 1, 1);
		  update_compositions (pos, pos + 1, CHECK_BORDER);

#ifdef HAVE_TREE_SITTER
		  /* In the previous branch, replace_range() notifies
                     changes to tree-sitter, but in this branch, we
                     modified buffer content manually, so we need to
                     notify tree-sitter manually.  */
		  treesit_record_change (pos_byte, pos_byte + len,
					 pos_byte + len, start_linecol,
					 old_end_linecol, pos + 1);
#endif
		}
	      characters_changed++;
	    }
	  else if (nc < 0)
	    {
	      if (CONSP (val))
		{
		  val = check_translation (pos, pos_byte, end_pos, val);
		  if (NILP (val))
		    {
		      pos_byte += len;
		      pos++;
		      continue;
		    }
		  /* VAL is ([FROM-CHAR ...] . TO).  */
		  len = ASIZE (XCAR (val));
		  val = XCDR (val);
		}
	      else
		len = 1;

	      Lisp_Object string
		= (VECTORP (val)
		   ? Fconcat (1, &val)
		   : Fmake_string (make_fixnum (1), val, Qnil));
	      replace_range (pos, pos + len, string, true, false, false);
	      pos_byte += SBYTES (string);
	      pos += SCHARS (string);
	      characters_changed += SCHARS (string);
	      end_pos += SCHARS (string) - len;
	      continue;
	    }
	}
      pos_byte += len;
      pos++;
    }

  return make_fixnum (characters_changed);
}

DEFUN ("delete-region", Fdelete_region, Sdelete_region, 2, 2, "r",
       doc: /* Delete the text between START and END.
If called interactively, delete the region between point and mark.
This command deletes buffer text without modifying the kill ring.  */)
  (Lisp_Object start, Lisp_Object end)
{
  validate_region (&start, &end);
  del_range (XFIXNUM (start), XFIXNUM (end));
  return Qnil;
}

DEFUN ("delete-and-extract-region", Fdelete_and_extract_region,
       Sdelete_and_extract_region, 2, 2, 0,
       doc: /* Delete the text between START and END and return it.  */)
  (Lisp_Object start, Lisp_Object end)
{
  validate_region (&start, &end);
  if (XFIXNUM (start) == XFIXNUM (end))
    return empty_unibyte_string;
  return del_range_1 (XFIXNUM (start), XFIXNUM (end), 1, 1);
}

/* Alist of buffers in which labeled restrictions are used.  The car
   of each list element is a buffer, the cdr is a list of triplets
   (label begv-marker zv-marker).  The last triplet of that list
   always uses the (uninterned) Qoutermost_restriction label, and
   records the restriction bounds that were current when the first
   labeled restriction was entered (which may be a narrowing that was
   set by the user and is visible on display).  This alist is used
   internally by narrow-to-region, internal--labeled-narrow-to-region,
   widen, internal--labeled-widen and save-restriction.  For
   efficiency reasons, an alist is used instead of a buffer-local
   variable: otherwise reset_outermost_restrictions, which is called
   during each redisplay cycle, would have to loop through all live
   buffers.  */
static Lisp_Object labeled_restrictions;

/* Add BUF with its list of labeled RESTRICTIONS in the
   labeled_restrictions alist.  */
static void
labeled_restrictions_add (Lisp_Object buf, Lisp_Object restrictions)
{
  labeled_restrictions = nconc2 (list1 (list2 (buf, restrictions)),
				 labeled_restrictions);
}

/* Remove BUF and its list of labeled restrictions from the
   labeled_restrictions alist.  Do nothing if BUF is not present in
   labeled_restrictions.  */
static void
labeled_restrictions_remove (Lisp_Object buf)
{
  labeled_restrictions = Fdelq (Fassoc (buf, labeled_restrictions, Qnil),
				labeled_restrictions);
}

/* Retrieve one of the labeled restriction bounds in BUF from the
   labeled_restrictions alist, as a marker, or return nil if BUF is
   not in labeled_restrictions or is a killed buffer.  When OUTERMOST
   is true, the restriction bounds that were current when the first
   labeled restriction was entered are returned.  Otherwise the bounds
   of the innermost labeled restriction are returned.  */
static Lisp_Object
labeled_restrictions_get_bound (Lisp_Object buf, bool begv, bool outermost)
{
  if (NILP (Fbuffer_live_p (buf)))
    return Qnil;
  Lisp_Object restrictions = assq_no_quit (buf, labeled_restrictions);
  if (NILP (restrictions))
    return Qnil;
  restrictions = XCAR (XCDR (restrictions));
  Lisp_Object bounds
    = outermost
      ? XCDR (assq_no_quit (Qoutermost_restriction, restrictions))
      : XCDR (XCAR (restrictions));
  eassert (! NILP (bounds));
  Lisp_Object marker = begv ? XCAR (bounds) : XCAR (XCDR (bounds));
  eassert (EQ (Fmarker_buffer (marker), buf));
  return marker;
}

/* Retrieve the label of the innermost labeled restriction in BUF.
   Return nil if BUF is not in labeled_restrictions or is a killed
   buffer.  */
static Lisp_Object
labeled_restrictions_peek_label (Lisp_Object buf)
{
  if (NILP (Fbuffer_live_p (buf)))
    return Qnil;
  Lisp_Object restrictions = assq_no_quit (buf, labeled_restrictions);
  if (NILP (restrictions))
    return Qnil;
  Lisp_Object label = XCAR (XCAR (XCAR (XCDR (restrictions))));
  eassert (! NILP (label));
  return label;
}

/* Add a labeled RESTRICTION for BUF in the labeled_restrictions
   alist.  */
static void
labeled_restrictions_push (Lisp_Object buf, Lisp_Object restriction)
{
  Lisp_Object restrictions = assq_no_quit (buf, labeled_restrictions);
  if (NILP (restrictions))
    labeled_restrictions_add (buf, list1 (restriction));
  else
    XSETCDR (restrictions, list1 (nconc2 (list1 (restriction),
					  XCAR (XCDR (restrictions)))));
}

/* Remove the innermost labeled restriction in BUF from the
   labeled_restrictions alist.  Do nothing if BUF is not present in
   labeled_restrictions.  */
static void
labeled_restrictions_pop (Lisp_Object buf)
{
  Lisp_Object restrictions = assq_no_quit (buf, labeled_restrictions);
  if (NILP (restrictions))
    return;
  if (BASE_EQ (labeled_restrictions_peek_label (buf), Qoutermost_restriction))
    labeled_restrictions_remove (buf);
  else
    XSETCDR (restrictions, list1 (XCDR (XCAR (XCDR (restrictions)))));
}

/* Unconditionally remove all labeled restrictions in current_buffer.  */
void
labeled_restrictions_remove_in_current_buffer (void)
{
  labeled_restrictions_remove (Fcurrent_buffer ());
}

static void
unwind_reset_outermost_restriction (Lisp_Object buf)
{
  Lisp_Object begv = labeled_restrictions_get_bound (buf, true, false);
  Lisp_Object zv = labeled_restrictions_get_bound (buf, false, false);
  if (! NILP (begv) && ! NILP (zv))
    {
      SET_BUF_BEGV_BOTH (XBUFFER (buf),
			 marker_position (begv), marker_byte_position (begv));
      SET_BUF_ZV_BOTH (XBUFFER (buf),
		       marker_position (zv), marker_byte_position (zv));
    }
  else
    labeled_restrictions_remove (buf);
}

/* Restore the restriction bounds that were current when the first
   labeled restriction was entered, and restore the bounds of the
   innermost labeled restriction upon return.
   In particular, this function is called when redisplay starts, so
   that if a Lisp function executed during redisplay calls (redisplay)
   while labeled restrictions are in effect, these restrictions will
   not become visible on display.
   See https://debbugs.gnu.org/cgi/bugreport.cgi?bug=57207#140 and
   https://debbugs.gnu.org/cgi/bugreport.cgi?bug=57207#254 for example
   recipes that demonstrate why this is necessary.  */
void
reset_outermost_restrictions (void)
{
  Lisp_Object val, buf;
  for (val = labeled_restrictions; CONSP (val); val = XCDR (val))
    {
      buf = XCAR (XCAR (val));
      eassert (BUFFERP (buf));
      Lisp_Object begv = labeled_restrictions_get_bound (buf, true, true);
      Lisp_Object zv = labeled_restrictions_get_bound (buf, false, true);
      if (! NILP (begv) && ! NILP (zv))
	{
	  SET_BUF_BEGV_BOTH (XBUFFER (buf),
			     marker_position (begv), marker_byte_position (begv));
	  SET_BUF_ZV_BOTH (XBUFFER (buf),
			   marker_position (zv), marker_byte_position (zv));
	  record_unwind_protect (unwind_reset_outermost_restriction, buf);
	}
      else
	labeled_restrictions_remove (buf);
    }
}

/* Helper functions to save and restore the labeled restrictions of
   the current buffer in Fsave_restriction.  */
static Lisp_Object
labeled_restrictions_save (void)
{
  Lisp_Object buf = Fcurrent_buffer ();
  Lisp_Object restrictions = assq_no_quit (buf, labeled_restrictions);
  if (! NILP (restrictions))
    restrictions = XCAR (XCDR (restrictions));
  return Fcons (buf, Fcopy_sequence (restrictions));
}

static void
labeled_restrictions_restore (Lisp_Object buf_and_restrictions)
{
  Lisp_Object buf = XCAR (buf_and_restrictions);
  Lisp_Object restrictions = XCDR (buf_and_restrictions);
  labeled_restrictions_remove (buf);
  if (! NILP (restrictions))
    labeled_restrictions_add (buf, restrictions);
}

static void
unwind_labeled_narrow_to_region (Lisp_Object label)
{
  Finternal__labeled_widen (label);
}

/* Narrow current_buffer to BEGV-ZV with a restriction labeled with
   LABEL.  */
void
labeled_narrow_to_region (Lisp_Object begv, Lisp_Object zv,
			  Lisp_Object label)
{
  record_unwind_protect (restore_point_unwind, Fpoint_marker ());
  record_unwind_protect (unwind_labeled_narrow_to_region, label);
  Finternal__labeled_narrow_to_region (begv, zv, label);
}

DEFUN ("widen", Fwiden, Swiden, 0, 0, "",
       doc: /* Remove restrictions (narrowing) from current buffer.

This allows the buffer's full text to be seen and edited.

However, when restrictions have been set by `with-restriction' with a
label, `widen' restores the narrowing limits set by `with-restriction'.
To gain access to other portions of the buffer, use
`without-restriction' with the same label.  */)
  (void)
{
  Lisp_Object buf = Fcurrent_buffer ();
  Lisp_Object label = labeled_restrictions_peek_label (buf);

  if (NILP (label))
    {
      if (BEG != BEGV || Z != ZV)
	current_buffer->clip_changed = 1;
      BEGV = BEG;
      BEGV_BYTE = BEG_BYTE;
      SET_BUF_ZV_BOTH (current_buffer, Z, Z_BYTE);
    }
  else
    {
      Lisp_Object begv = labeled_restrictions_get_bound (buf, true, false);
      Lisp_Object zv = labeled_restrictions_get_bound (buf, false, false);
      eassert (! NILP (begv) && ! NILP (zv));
      ptrdiff_t begv_charpos = marker_position (begv);
      ptrdiff_t zv_charpos = marker_position (zv);
      if (begv_charpos != BEGV || zv_charpos != ZV)
	current_buffer->clip_changed = 1;
      SET_BUF_BEGV_BOTH (current_buffer,
			 begv_charpos, marker_byte_position (begv));
      SET_BUF_ZV_BOTH (current_buffer,
		       zv_charpos, marker_byte_position (zv));
      /* If the only remaining bounds in labeled_restrictions for
	 current_buffer are the bounds that were set by the user, no
	 labeled restriction is in effect in current_buffer anymore:
	 remove it from the labeled_restrictions alist.  */
      if (BASE_EQ (label, Qoutermost_restriction))
	labeled_restrictions_pop (buf);
    }
  /* Changing the buffer bounds invalidates any recorded current column.  */
  invalidate_current_column ();
  return Qnil;
}

DEFUN ("narrow-to-region", Fnarrow_to_region, Snarrow_to_region, 2, 2, "r",
       doc: /* Restrict editing in this buffer to the current region.
The rest of the text becomes temporarily invisible and untouchable
but is not deleted; if you save the buffer in a file, the invisible
text is included in the file.  \\[widen] makes all visible again.
See also `save-restriction'.

When calling from Lisp, pass two arguments START and END:
positions (integers or markers) bounding the text that should
remain visible.

However, when restrictions have been set by `with-restriction' with a
label, `narrow-to-region' can be used only within the limits of these
restrictions.  If the START or END arguments are outside these limits,
the corresponding limit set by `with-restriction' is used instead of the
argument.  To gain access to other portions of the buffer, use
`without-restriction' with the same label.  */)
  (Lisp_Object start, Lisp_Object end)
{
  EMACS_INT s = fix_position (start), e = fix_position (end);

  if (e < s)
    {
      EMACS_INT tem = s; s = e; e = tem;
    }

  if (!(BEG <= s && s <= e && e <= Z))
    args_out_of_range (start, end);

  Lisp_Object buf = Fcurrent_buffer ();
  if (! NILP (labeled_restrictions_peek_label (buf)))
    {
      /* Limit the start and end positions to those of the innermost
	 labeled restriction.  */
      Lisp_Object begv = labeled_restrictions_get_bound (buf, true, false);
      Lisp_Object zv = labeled_restrictions_get_bound (buf, false, false);
      eassert (! NILP (begv) && ! NILP (zv));
      ptrdiff_t begv_charpos = marker_position (begv);
      ptrdiff_t zv_charpos = marker_position (zv);
      if (s < begv_charpos) s = begv_charpos;
      if (s > zv_charpos) s = zv_charpos;
      if (e < begv_charpos) e = begv_charpos;
      if (e > zv_charpos) e = zv_charpos;
    }

  if (BEGV != s || ZV != e)
    current_buffer->clip_changed = 1;

  SET_BUF_BEGV (current_buffer, s);
  SET_BUF_ZV (current_buffer, e);

  if (PT < s)
    SET_PT (s);
  if (e < PT)
    SET_PT (e);
  /* Changing the buffer bounds invalidates any recorded current column.  */
  invalidate_current_column ();
  return Qnil;
}

DEFUN ("internal--labeled-narrow-to-region", Finternal__labeled_narrow_to_region,
       Sinternal__labeled_narrow_to_region, 3, 3, 0,
       doc: /* Restrict this buffer to START-END, and label the restriction with LABEL.

This is an internal function used by `with-restriction'.  */)
  (Lisp_Object start, Lisp_Object end, Lisp_Object label)
{
  Lisp_Object buf = Fcurrent_buffer ();
  Lisp_Object outermost_restriction = list3 (Qoutermost_restriction,
					     Fpoint_min_marker (),
					     Fpoint_max_marker ());
  Fnarrow_to_region (start, end);
  if (NILP (labeled_restrictions_peek_label (buf)))
    labeled_restrictions_push (buf, outermost_restriction);
  labeled_restrictions_push (buf, list3 (label,
					 Fpoint_min_marker (),
					 Fpoint_max_marker ()));
  return Qnil;
}

DEFUN ("internal--labeled-widen", Finternal__labeled_widen,
       Sinternal__labeled_widen, 1, 1, 0,
       doc: /* Remove the current restriction if it is labeled with LABEL, and widen.

This is an internal function used by `without-restriction'.  */)
  (Lisp_Object label)
{
  Lisp_Object buf = Fcurrent_buffer ();
  if (EQ (labeled_restrictions_peek_label (buf), label))
    labeled_restrictions_pop (buf);
  Fwiden ();
  return Qnil;
}

static Lisp_Object
save_restriction_save_1 (void)
{
  if (BEGV == BEG && ZV == Z)
    /* The common case that the buffer isn't narrowed.
       We return just the buffer object, which save_restriction_restore
       recognizes as meaning `no restriction'.  */
    return Fcurrent_buffer ();
  else
    /* We have to save a restriction, so return a pair of markers, one
       for the beginning and one for the end.  */
    {
      Lisp_Object beg, end;

      beg = build_marker (current_buffer, BEGV, BEGV_BYTE);
      end = build_marker (current_buffer, ZV, ZV_BYTE);

      /* END must move forward if text is inserted at its exact location.  */
      XMARKER (end)->insertion_type = 1;

      return Fcons (beg, end);
    }
}

static void
save_restriction_restore_1 (Lisp_Object data)
{
  struct buffer *cur = NULL;
  struct buffer *buf = (CONSP (data)
			? XMARKER (XCAR (data))->buffer
			: XBUFFER (data));

  if (buf && buf != current_buffer && !NILP (BVAR (buf, pt_marker)))
    { /* If `buf' uses markers to keep track of PT, BEGV, and ZV (as
	 is the case if it is or has an indirect buffer), then make
	 sure it is current before we update BEGV, so
	 set_buffer_internal takes care of managing those markers.  */
      cur = current_buffer;
      set_buffer_internal (buf);
    }

  if (CONSP (data))
    /* A pair of marks bounding a saved restriction.  */
    {
      struct Lisp_Marker *beg = XMARKER (XCAR (data));
      struct Lisp_Marker *end = XMARKER (XCDR (data));
      eassert (buf == end->buffer);

      if (buf /* Verify marker still points to a buffer.  */
	  && (beg->charpos != BUF_BEGV (buf) || end->charpos != BUF_ZV (buf)))
	/* The restriction has changed from the saved one, so restore
	   the saved restriction.  */
	{
	  ptrdiff_t pt = BUF_PT (buf);

	  SET_BUF_BEGV_BOTH (buf, beg->charpos, beg->bytepos);
	  SET_BUF_ZV_BOTH (buf, end->charpos, end->bytepos);

	  if (pt < beg->charpos || pt > end->charpos)
	    /* The point is outside the new visible range, move it inside. */
	    SET_BUF_PT_BOTH (buf,
			     clip_to_bounds (beg->charpos, pt, end->charpos),
			     clip_to_bounds (beg->bytepos, BUF_PT_BYTE (buf),
					     end->bytepos));

	  buf->clip_changed = 1; /* Remember that the narrowing changed. */
	}
      /* Detach the markers, and free the cons instead of waiting for GC.  */
      detach_marker (XCAR (data));
      detach_marker (XCDR (data));
      free_cons (XCONS (data));
    }
  else
    /* A buffer, which means that there was no old restriction.  */
    {
      if (buf /* Verify marker still points to a buffer.  */
	  && (BUF_BEGV (buf) != BUF_BEG (buf) || BUF_ZV (buf) != BUF_Z (buf)))
	/* The buffer has been narrowed, get rid of the narrowing.  */
	{
	  SET_BUF_BEGV_BOTH (buf, BUF_BEG (buf), BUF_BEG_BYTE (buf));
	  SET_BUF_ZV_BOTH (buf, BUF_Z (buf), BUF_Z_BYTE (buf));

	  buf->clip_changed = 1; /* Remember that the narrowing changed. */
	}
    }

  /* Changing the buffer bounds invalidates any recorded current column.  */
  invalidate_current_column ();

  if (cur)
    set_buffer_internal (cur);
}

Lisp_Object
save_restriction_save (void)
{
  Lisp_Object restriction = save_restriction_save_1 ();
  Lisp_Object labeled_restrictions = labeled_restrictions_save ();
  return Fcons (restriction, labeled_restrictions);
}

void
save_restriction_restore (Lisp_Object data)
{
  labeled_restrictions_restore (XCDR (data));
  save_restriction_restore_1 (XCAR (data));
}

DEFUN ("save-restriction", Fsave_restriction, Ssave_restriction, 0, UNEVALLED, 0,
       doc: /* Execute BODY, saving and restoring current buffer's restrictions.
The buffer's restrictions make parts of the beginning and end invisible.
\(They are set up with `narrow-to-region' and eliminated with `widen'.)
This special form, `save-restriction', saves the current buffer's
restrictions, including those that were set by `with-restriction' with a
label argument, when it is entered, and restores them when it is exited.
So any `narrow-to-region' within BODY lasts only until the end of the form.
The old restrictions settings are restored even in case of abnormal exit
\(throw or error).

The value returned is the value of the last form in BODY.

Note: if you are using both `save-excursion' and `save-restriction',
use `save-excursion' outermost:
    (save-excursion (save-restriction ...))

usage: (save-restriction &rest BODY)  */)
  (Lisp_Object body)
{
  register Lisp_Object val;
  specpdl_ref count = SPECPDL_INDEX ();

  record_unwind_protect (save_restriction_restore, save_restriction_save ());
  val = Fprogn (body);
  return unbind_to (count, val);
}

/* i18n (internationalization).  */

DEFUN ("ngettext", Fngettext, Sngettext, 3, 3, 0,
       doc: /* Return the translation of MSGID (plural MSGID-PLURAL) depending on N.
MSGID is the singular form of the string to be converted;
use it as the key for the search in the translation catalog.
MSGID-PLURAL is the plural form.  Use N to select the proper translation.
If no message catalog is found, MSGID is returned if N is equal to 1,
otherwise MSGID-PLURAL.  */)
  (Lisp_Object msgid, Lisp_Object msgid_plural, Lisp_Object n)
{
  CHECK_STRING (msgid);
  CHECK_STRING (msgid_plural);
  CHECK_INTEGER (n);

  /* Placeholder implementation until we get our act together.  */
  return BASE_EQ (n, make_fixnum (1)) ? msgid : msgid_plural;
}

DEFUN ("message", Fmessage, Smessage, 1, MANY, 0,
       doc: /* Display a message at the bottom of the screen.
The message also goes into the `*Messages*' buffer, if `message-log-max'
is non-nil.  (In keyboard macros, that's all it does.)
Return the message.

In batch mode, the message is printed to the standard error stream,
followed by a newline.

If the variable `inhibit-message' is non-nil, the message is not
displayed, only logged in the `*Messages*' buffer.

The first argument is a format control string, and the rest are data
to be formatted under control of the string.  Percent sign (%), grave
accent (\\=`) and apostrophe (\\=') are special in the format; see
`format-message' for details.  To display STRING without special
treatment, use (message "%s" STRING).

If the first argument is nil or the empty string, the function clears
any existing message; this lets the minibuffer contents show.  See
also `current-message'.

usage: (message FORMAT-STRING &rest ARGS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  if (NILP (args[0])
      || (STRINGP (args[0])
	  && SBYTES (args[0]) == 0))
    {
      message1 (0);
      return args[0];
    }
  else
    {
      Lisp_Object val = Fformat_message (nargs, args);
      message3 (val);
      return val;
    }
}

DEFUN ("message-box", Fmessage_box, Smessage_box, 1, MANY, 0,
       doc: /* Display a message, in a dialog box if possible.
If a dialog box is not available, use the echo area.
The first argument is a format control string, and the rest are data
to be formatted under control of the string.  See `format-message' for
details.

If the first argument is nil or the empty string, clear any existing
message; let the minibuffer contents show.

usage: (message-box FORMAT-STRING &rest ARGS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  if (NILP (args[0]))
    {
      message1 (0);
      return Qnil;
    }
  else
    {
      Lisp_Object val = Fformat_message (nargs, args);
      Lisp_Object pane, menu;

      pane = list1 (Fcons (build_string ("OK"), Qt));
      menu = Fcons (val, pane);
      Fx_popup_dialog (Qt, menu, Qt);
      return val;
    }
}

DEFUN ("message-or-box", Fmessage_or_box, Smessage_or_box, 1, MANY, 0,
       doc: /* Display a message in a dialog box or in the echo area.
If this command was invoked with the mouse, use a dialog box if
`use-dialog-box' is non-nil.
Otherwise, use the echo area.
The first argument is a format control string, and the rest are data
to be formatted under control of the string.  See `format-message' for
details.

If the first argument is nil or the empty string, clear any existing
message; let the minibuffer contents show.

usage: (message-or-box FORMAT-STRING &rest ARGS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  if ((NILP (last_nonmenu_event) || CONSP (last_nonmenu_event))
      && use_dialog_box)
    return Fmessage_box (nargs, args);
  return Fmessage (nargs, args);
}

DEFUN ("current-message", Fcurrent_message, Scurrent_message, 0, 0, 0,
       doc: /* Return the string currently displayed in the echo area, or nil if none.  */)
  (void)
{
  return current_message ();
}


DEFUN ("propertize", Fpropertize, Spropertize, 1, MANY, 0,
       doc: /* Return a copy of STRING with text properties added.
First argument is the string to copy.
Remaining arguments form a sequence of PROPERTY VALUE pairs for text
properties to add to the result.

See Info node `(elisp) Text Properties' for more information.
usage: (propertize STRING &rest PROPERTIES)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  Lisp_Object properties, string;
  ptrdiff_t i;

  /* Number of args must be odd.  */
  if ((nargs & 1) == 0)
    xsignal2 (Qwrong_number_of_arguments, Qpropertize, make_fixnum (nargs));

  properties = string = Qnil;

  /* First argument must be a string.  */
  CHECK_STRING (args[0]);
  string = Fcopy_sequence (args[0]);

  for (i = 1; i < nargs; i += 2)
    properties = Fcons (args[i], Fcons (args[i + 1], properties));

  Fadd_text_properties (make_fixnum (0),
			make_fixnum (SCHARS (string)),
			properties, string);
  return string;
}

/* Convert the prefix of STR from ASCII decimal digits to a number.
   Set *STR_END to the address of the first non-digit.  Return the
   number, or PTRDIFF_MAX on overflow.  Return 0 if there is no number.
   This is like strtol for ptrdiff_t and base 10 and C locale,
   except without negative numbers or errno.  */

static ptrdiff_t
str2num (char *str, char **str_end)
{
  ptrdiff_t n = 0;
  for (; c_isdigit (*str); str++)
    if (ckd_mul (&n, n, 10) || ckd_add (&n, n, *str - '0'))
      n = PTRDIFF_MAX;
  *str_end = str;
  return n;
}

DEFUN ("format", Fformat, Sformat, 1, MANY, 0,
       doc: /* Format a string out of a format-string and arguments.
The first argument is a format control string.
The other arguments are substituted into it to make the result, a string.

The format control string may contain %-sequences meaning to substitute
the next available argument, or the argument explicitly specified:

%s means produce a string argument.  Actually, produces any object with `princ'.
%d means produce as signed number in decimal.
%o means produce a number in octal.
%x means produce a number in hex.
%X is like %x, but uses upper case.
%e means produce a number in exponential notation.
%f means produce a number in decimal-point notation.
%g means produce a number in exponential notation if the exponent would be
   less than -4 or greater than or equal to the precision (default: 6);
   otherwise it produces in decimal-point notation.
%c means produce a number as a single character.
%S means produce any object as an s-expression (using `prin1').

The argument used for %d, %o, %x, %e, %f, %g or %c must be a number.
%o, %x, and %X treat arguments as unsigned if `binary-as-unsigned' is t
  (this is experimental; email 32252@debbugs.gnu.org if you need it).
Use %% to put a single % into the output.

A %-sequence other than %% may contain optional field number, flag,
width, and precision specifiers, as follows:

  %<field><flags><width><precision>character

where field is [0-9]+ followed by a literal dollar "$", flags is
[+ #0-]+, width is [0-9]+, and precision is a literal period "."
followed by [0-9]+.

If a %-sequence is numbered with a field with positive value N, the
Nth argument is substituted instead of the next one.  A format can
contain either numbered or unnumbered %-sequences but not both, except
that %% can be mixed with numbered %-sequences.

The + flag character inserts a + before any nonnegative number, while a
space inserts a space before any nonnegative number; these flags
affect only numeric %-sequences, and the + flag takes precedence.
The - and 0 flags affect the width specifier, as described below.

The # flag means to use an alternate display form for %o, %x, %X, %e,
%f, and %g sequences: for %o, it ensures that the result begins with
\"0\"; for %x and %X, it prefixes nonzero results with \"0x\" or \"0X\";
for %e and %f, it causes a decimal point to be included even if the
precision is zero; for %g, it causes a decimal point to be
included even if the precision is zero, and also forces trailing
zeros after the decimal point to be left in place.

The width specifier supplies a lower limit for the length of the
produced representation.  The padding, if any, normally goes on the
left, but it goes on the right if the - flag is present.  The padding
character is normally a space, but it is 0 if the 0 flag is present.
The 0 flag is ignored if the - flag is present, or the format sequence
is something other than %d, %o, %x, %e, %f, and %g.

For %e and %f sequences, the number after the "." in the precision
specifier says how many decimal places to show; if zero, the decimal
point itself is omitted.  For %g, the precision specifies how many
significant digits to produce; zero or omitted are treated as 1.
For %s and %S, the precision specifier truncates the string to the
given width.

Text properties, if any, are copied from the format-string to the
produced text.

usage: (format STRING &rest OBJECTS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  return styled_format (nargs, args, false);
}

DEFUN ("format-message", Fformat_message, Sformat_message, 1, MANY, 0,
       doc: /* Format a string out of a format-string and arguments.
The first argument is a format control string.
The other arguments are substituted into it to make the result, a string.

This acts like `format', except it also replaces each grave accent (\\=`)
by a left quote, and each apostrophe (\\=') by a right quote.  The left
and right quote replacement characters are specified by
`text-quoting-style'.

usage: (format-message STRING &rest OBJECTS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  return styled_format (nargs, args, true);
}

/* Implement ‘format-message’ if MESSAGE is true, ‘format’ otherwise.  */

static Lisp_Object
styled_format (ptrdiff_t nargs, Lisp_Object *args, bool message)
{
  enum
  {
   /* Maximum precision for a %f conversion such that the trailing
      output digit might be nonzero.  Any precision larger than this
      will not yield useful information.  */
   USEFUL_PRECISION_MAX = ((1 - LDBL_MIN_EXP)
			   * (FLT_RADIX == 2 || FLT_RADIX == 10 ? 1
			      : FLT_RADIX == 16 ? 4
			      : -1)),

   /* Maximum number of bytes (including terminating null) generated
      by any format, if precision is no more than USEFUL_PRECISION_MAX.
      On all practical hosts, %Lf is the worst case.  */
   SPRINTF_BUFSIZE = (sizeof "-." + (LDBL_MAX_10_EXP + 1)
		      + USEFUL_PRECISION_MAX)
  };
  static_assert (USEFUL_PRECISION_MAX > 0);

  ptrdiff_t n;		/* The number of the next arg to substitute.  */
  char initial_buffer[1000 + SPRINTF_BUFSIZE];
  char *buf = initial_buffer;
  ptrdiff_t bufsize = sizeof initial_buffer;
  ptrdiff_t max_bufsize = STRING_BYTES_BOUND + 1;
  char *p;
  specpdl_ref buf_save_value_index UNINIT;
  char *format, *end;
  ptrdiff_t nchars;
  /* When we make a multibyte string, we must pay attention to the
     byte combining problem, i.e., a byte may be combined with a
     multibyte character of the previous string.  This flag tells if we
     must consider such a situation or not.  */
  bool maybe_combine_byte;
  Lisp_Object val;
  bool arg_intervals = false;
  USE_SAFE_ALLOCA;
  sa_avail -= sizeof initial_buffer;

  /* Information recorded for each format spec.  */
  struct info
  {
    /* The start and end bytepos in the output string.  */
    ptrdiff_t start, end;

    /* The start bytepos of the spec in the format string.  */
    ptrdiff_t fbeg;

    /* Whether the argument is a string with intervals.  */
    bool_bf intervals : 1;
  } *info;

  CHECK_STRING (args[0]);
  bool multibyte_format = STRING_MULTIBYTE (args[0]);
  ptrdiff_t formatlen = SBYTES (args[0]);
  char *format_start = SAFE_ALLOCA (formatlen + 1);
  memcpy (format_start, SSDATA (args[0]), formatlen + 1);
  bool fmt_props = !!string_intervals (args[0]);

  /* Upper bound on number of format specs.  Each uses at least 2 chars.  */
  ptrdiff_t nspec_bound = SCHARS (args[0]) >> 1;

  /* Allocate the info and discarded tables.  */
  ptrdiff_t info_size, alloca_size;
  if (ckd_mul (&info_size, nspec_bound, sizeof *info)
      || ckd_add (&alloca_size, formatlen, info_size)
      || SIZE_MAX < alloca_size)
    memory_full (SIZE_MAX);
  info = SAFE_ALLOCA (alloca_size);
  /* One argument belonging to each spec; but needs to be allocated
     separately so GC doesn't free the strings (bug#75754).  */
  Lisp_Object *spec_arguments;
  SAFE_ALLOCA_LISP (spec_arguments, nspec_bound);
  /* discarded[I] is 1 if byte I of the format
     string was not copied into the output.
     It is 2 if byte I was not the first byte of its character.  */
  char *discarded = (char *) &info[nspec_bound];
  memset (discarded, 0, formatlen);

  /* Try to determine whether the result should be multibyte.
     This is not always right; sometimes the result needs to be multibyte
     because of an object that we will pass through prin1.
     or because a grave accent or apostrophe is requoted,
     and in that case, we won't know it here.  */

  /* True if the output should be a multibyte string,
     which is true if any of the inputs is one.  */
  bool multibyte = multibyte_format;
  for (ptrdiff_t i = 1; !multibyte && i < nargs; i++)
    if (STRINGP (args[i]) && STRING_MULTIBYTE (args[i]))
      multibyte = true;

  Lisp_Object quoting_style = message ? Ftext_quoting_style () : Qnil;

  ptrdiff_t ispec;
  ptrdiff_t nspec = 0;

  /* True if a string needs to be allocated to hold the result.  */
  bool new_result = false;

  /* If we start out planning a unibyte result,
     then discover it has to be multibyte, we jump back to retry.  */
 retry:

  p = buf;
  nchars = 0;

  /* N is the argument index, ISPEC is the specification index.  */
  n = 0;
  ispec = 0;

  /* Scan the format and store result in BUF.  */
  format = format_start;
  end = format + formatlen;
  maybe_combine_byte = false;

  while (format != end)
    {
      /* The values of N, ISPEC, and FORMAT when the loop body is
         entered.  */
      ptrdiff_t n0 = n;
      ptrdiff_t ispec0 = ispec;
      char *format0 = format;
      char const *convsrc = format;
      unsigned char format_char = *format++;

      /* Number of bytes to be preallocated for the next directive's
	 output.  At the end of each iteration this is at least
	 CONVBYTES_ROOM, and is greater if the current directive
	 output was so large that it will be retried after buffer
	 reallocation.  */
      ptrdiff_t convbytes = 1;
      enum { CONVBYTES_ROOM = SPRINTF_BUFSIZE - 1 };
      eassert (p <= buf + bufsize - SPRINTF_BUFSIZE);

      if (format_char == '%')
	{
	  /* General format specifications look like

	     '%' [field-number] [flags] [field-width] [precision] format

	     where

             field-number ::= [0-9]+ '$'
	     flags ::= [-+0# ]+
	     field-width ::= [0-9]+
	     precision ::= '.' [0-9]*

	     If present, a field-number specifies the argument number
	     to substitute.  Otherwise, the next argument is taken.

	     If a field-width is specified, it specifies to which width
	     the output should be padded with blanks, if the output
	     string is shorter than field-width.

	     If precision is specified, it specifies the number of
	     digits to print after the '.' for floats, or the max.
	     number of chars to print from a string.  */

	  ptrdiff_t num;
	  char *num_end;
	  if (c_isdigit (*format))
	    {
	      num = str2num (format, &num_end);
	      if (*num_end == '$')
		{
		  n = num - 1;
		  format = num_end + 1;
		}
	    }

	  bool minus_flag = false;
	  bool  plus_flag = false;
	  bool space_flag = false;
	  bool sharp_flag = false;
	  bool  zero_flag = false;

	  for (; ; format++)
	    {
	      switch (*format)
		{
		case '-': minus_flag = true; continue;
		case '+':  plus_flag = true; continue;
		case ' ': space_flag = true; continue;
		case '#': sharp_flag = true; continue;
		case '0':  zero_flag = true; continue;
		}
	      break;
	    }

	  /* Ignore flags when sprintf ignores them.  */
	  space_flag &= ! plus_flag;
	  zero_flag &= ! minus_flag;

	  num = str2num (format, &num_end);
	  if (max_bufsize <= num)
	    string_overflow ();
	  ptrdiff_t field_width = num;

	  bool precision_given = *num_end == '.';
	  ptrdiff_t precision = (precision_given
				 ? str2num (num_end + 1, &num_end)
				 : PTRDIFF_MAX);
	  format = num_end;

	  if (format == end)
	    error ("Format string ends in middle of format specifier");

	  char conversion = *format++;
	  memset (&discarded[format0 - format_start], 1,
		  format - format0 - (conversion == '%'));
	  info[ispec].fbeg = format0 - format_start;
	  if (conversion == '%')
	    {
	      new_result = true;
	      goto copy_char;
	    }

	  ++n;
	  if (! (n < nargs))
	    error ("Not enough arguments for format string");

	  ptrdiff_t spec_index = ispec++;
	  struct info *spec = &info[spec_index];
	  if (nspec < ispec)
	    {
	      spec_arguments[spec_index] = args[n];
	      spec->intervals = false;
	      nspec = ispec;
	    }
	  Lisp_Object arg = spec_arguments[spec_index];

	  /* For 'S', prin1 the argument, and then treat like 's'.
	     For 's', princ any argument that is not a string or
	     symbol.  But don't do this conversion twice, which might
	     happen after retrying.  */
	  if ((conversion == 'S'
	       || (conversion == 's'
		   && ! STRINGP (arg) && ! SYMBOLP (arg))))
	    {
	      if (EQ (arg, args[n]))
		{
		  Lisp_Object noescape = conversion == 'S' ? Qnil : Qt;
		  spec_arguments[spec_index] = arg = Fprin1_to_string (arg, noescape, Qnil);
		  if (STRING_MULTIBYTE (arg) && ! multibyte)
		    {
		      multibyte = true;
		      goto retry;
		    }
		}
	      conversion = 's';
	    }
	  else if (conversion == 'c')
	    {
	      if (FIXNUMP (arg) && ! ASCII_CHAR_P (XFIXNUM (arg)))
		{
		  if (!multibyte)
		    {
		      multibyte = true;
		      goto retry;
		    }
		  spec_arguments[spec_index] = arg = Fchar_to_string (arg);
		}

	      if (!EQ (arg, args[n]))
		conversion = 's';
	      zero_flag = false;
	    }

	  if (SYMBOLP (arg))
	    {
	      spec_arguments[spec_index] = arg = SYMBOL_NAME (arg);
	      if (STRING_MULTIBYTE (arg) && ! multibyte)
		{
		  multibyte = true;
		  goto retry;
		}
	    }

	  bool float_conversion
	    = conversion == 'e' || conversion == 'f' || conversion == 'g';

	  if (conversion == 's')
	    {
	      if (format == end && format - format_start == 2
		  && ! string_intervals (args[0]))
		{
		  val = arg;
		  goto return_val;
		}

	      /* handle case (precision[n] >= 0) */

	      ptrdiff_t prec = -1;
	      if (precision_given)
		prec = precision;

	      /* lisp_string_width ignores a precision of 0, but GNU
		 libc functions print 0 characters when the precision
		 is 0.  Imitate libc behavior here.  Changing
		 lisp_string_width is the right thing, and will be
		 done, but meanwhile we work with it. */

	      ptrdiff_t width, nbytes;
	      ptrdiff_t nchars_string;
	      if (prec == 0)
		width = nchars_string = nbytes = 0;
	      else
		{
		  ptrdiff_t nch, nby;
		  nchars_string = SCHARS (arg);
		  width = lisp_string_width (arg, 0, nchars_string, prec,
					     &nch, &nby, false);
		  if (prec < 0)
		    nbytes = SBYTES (arg);
		  else
		    {
		      nchars_string = nch;
		      nbytes = nby;
		    }
		}

	      convbytes = nbytes;
	      if (convbytes && multibyte && ! STRING_MULTIBYTE (arg))
		convbytes = count_size_as_multibyte (SDATA (arg), nbytes);

	      ptrdiff_t padding
		= width < field_width ? field_width - width : 0;

	      if (max_bufsize - padding <= convbytes)
		string_overflow ();
	      convbytes += padding;
	      if (convbytes <= buf + bufsize - p)
		{
		  /* If the format spec has properties, we should account
		     for the padding on the left in the info[] array.  */
		  if (fmt_props)
		    spec->start = nchars;
		  if (! minus_flag)
		    {
		      memset (p, ' ', padding);
		      p += padding;
		      nchars += padding;
		    }
		  /* If the properties will come from the argument, we
		     don't extend them to the left due to padding.  */
		  if (!fmt_props)
		    spec->start = nchars;

		  if (p > buf
		      && multibyte
		      && !ASCII_CHAR_P (*((unsigned char *) p - 1))
		      && STRING_MULTIBYTE (arg)
		      && !CHAR_HEAD_P (SREF (arg, 0)))
		    maybe_combine_byte = true;

		  p += copy_text (SDATA (arg), (unsigned char *) p,
				  nbytes,
				  STRING_MULTIBYTE (arg), multibyte);

		  nchars += nchars_string;

		  if (minus_flag)
		    {
		      memset (p, ' ', padding);
		      p += padding;
		      nchars += padding;
		    }
		  spec->end = nchars;

		  /* If this argument has text properties, record where
		     in the result string it appears.  */
		  if (string_intervals (arg))
		    spec->intervals = arg_intervals = true;

		  new_result = true;
		  convbytes = CONVBYTES_ROOM;
		}
	    }
	  else if (! (conversion == 'c' || conversion == 'd'
		      || float_conversion || conversion == 'i'
		      || conversion == 'o' || conversion == 'x'
		      || conversion == 'X'))
	    {
	      unsigned char *p = (unsigned char *) format - 1;
	      if (multibyte_format)
		error ("Invalid format operation %%%c", STRING_CHAR (p));
	      else
		error (*p <= 127 ? "Invalid format operation %%%c"
		                 : "Invalid format operation char #o%03o",
		       *p);
	    }
	  else if (! (FIXNUMP (arg) || ((BIGNUMP (arg) || FLOATP (arg))
					&& conversion != 'c')))
	    error ("Format specifier doesn't match argument type");
	  else
	    {
	      /* Length of PRIdMAX without the trailing "d".  */
	      enum { pMlen = sizeof PRIdMAX - 2 };

	      /* Avoid undefined behavior in underlying sprintf.  */
	      if (conversion == 'd' || conversion == 'i')
		sharp_flag = false;

	      /* Create the copy of the conversion specification, with
		 any width and precision removed, with ".*" inserted,
		 with "L" possibly inserted for floating-point formats,
		 and with PRIdMAX (sans "d") inserted for integer formats.
		 At most two flags F can be specified at once.  */
	      char convspec[sizeof "%FF.*d" + max (sizeof "L" - 1, pMlen)];
	      char *f = convspec;
	      *f++ = '%';
	      /* MINUS_FLAG and ZERO_FLAG are dealt with later.  */
	      *f = '+'; f +=  plus_flag;
	      *f = ' '; f += space_flag;
	      *f = '#'; f += sharp_flag;
	      *f++ = '.';
	      *f++ = '*';
	      if (! (float_conversion || conversion == 'c'))
		{
		  memcpy (f, PRIdMAX, pMlen);
		  f += pMlen;
		  zero_flag &= ! precision_given;
		}
	      *f++ = conversion;
	      *f = '\0';

	      int prec = -1;
	      if (precision_given)
		prec = min (precision, USEFUL_PRECISION_MAX);

	      /* Characters to be inserted after spaces and before
		 leading zeros.  This can occur with bignums, since
		 bignum_to_string does only leading '-'.  */
	      char prefix[sizeof "-0x" - 1];
	      int prefixlen = 0;

	      /* Use sprintf or bignum_to_string to format this number.  Omit
		 padding and excess precision, though, because sprintf limits
		 output length to INT_MAX and bignum_to_string doesn't
		 do padding or precision.

		 Use five sprintf conversions: double, long double, unsigned
		 char (passed as int), wide signed int, and wide
		 unsigned int.  Treat them separately because the
		 sprintf ABI is sensitive to which type is passed.  Be
		 careful about integer overflow, NaNs, infinities, and
		 conversions; for example, the min and max macros are
		 not suitable here.  */
	      ptrdiff_t sprintf_bytes;
	      if (float_conversion)
		{
		  /* Format as a long double if the arg is an integer
		     that would lose less information than when formatting
		     it as a double.  Otherwise, format as a double;
		     this is likely to be faster and better-tested.  */

		  bool format_as_long_double = false;
		  double darg;
		  long double ldarg UNINIT;

		  if (FLOATP (arg))
		    darg = XFLOAT_DATA (arg);
		  else
		    {
		      bool format_bignum_as_double = false;
		      if (LDBL_MANT_DIG <= DBL_MANT_DIG)
			{
			  if (FIXNUMP (arg))
			    darg = XFIXNUM (arg);
			  else
			    format_bignum_as_double = true;
			}
		      else
			{
			  if (INTEGERP (arg))
			    {
			      intmax_t iarg;
			      uintmax_t uarg;
			      if (integer_to_intmax (arg, &iarg))
				ldarg = iarg;
			      else if (integer_to_uintmax (arg, &uarg))
				ldarg = uarg;
			      else
				format_bignum_as_double = true;
			    }
			  if (!format_bignum_as_double)
			    {
			      darg = ldarg;
			      format_as_long_double = darg != ldarg;
			    }
			}
		      if (format_bignum_as_double)
			darg = bignum_to_double (arg);
		    }

		  if (format_as_long_double)
		    {
		      f[-1] = 'L';
		      *f++ = conversion;
		      *f = '\0';
		      sprintf_bytes = sprintf (p, convspec, prec, ldarg);
		    }
		  else
		    sprintf_bytes = sprintf (p, convspec, prec, darg);
		}
	      else if (conversion == 'c')
		{
		  /* Don't use sprintf here, as it might mishandle prec.  */
		  p[0] = XFIXNUM (arg);
		  p[1] = '\0';
		  sprintf_bytes = prec != 0;
		}
	      else if (BIGNUMP (arg))
	      bignum_arg:
		{
		  int base = ((conversion == 'd' || conversion == 'i') ? 10
			      : conversion == 'o' ? 8 : 16);
		  sprintf_bytes = bignum_bufsize (arg, base);
		  if (sprintf_bytes <= buf + bufsize - p)
		    {
		      int signedbase = conversion == 'X' ? -base : base;
		      sprintf_bytes = bignum_to_c_string (p, sprintf_bytes,
							  arg, signedbase);
		      bool negative = p[0] == '-';
		      prec = min (precision, sprintf_bytes - prefixlen);
		      prefix[prefixlen] = plus_flag ? '+' : ' ';
		      prefixlen += (plus_flag | space_flag) & !negative;
		      prefix[prefixlen] = '0';
		      prefix[prefixlen + 1] = conversion;
		      prefixlen += sharp_flag && base == 16 ? 2 : 0;
		    }
		}
	      else if (conversion == 'd' || conversion == 'i')
		{
		  if (FIXNUMP (arg))
		    {
		      intmax_t x = XFIXNUM (arg);
		      sprintf_bytes = sprintf (p, convspec, prec, x);
		    }
		  else
		    {
		      strcpy (f - pMlen - 1, "f");
		      double x = XFLOAT_DATA (arg);

		      /* Truncate and then convert -0 to 0, to be more
			 consistent with %x etc.; see Bug#31938.  */
		      x = trunc (x);
		      x = x ? x : 0;

		      sprintf_bytes = sprintf (p, convspec, 0, x);
		      bool signedp = ! c_isdigit (p[0]);
		      prec = min (precision, sprintf_bytes - signedp);
		    }
		}
	      else
		{
		  uintmax_t x;
		  bool negative;
		  if (FIXNUMP (arg))
		    {
		      if (binary_as_unsigned)
			{
			  x = XUFIXNUM (arg);
			  negative = false;
			}
		      else
			{
			  EMACS_INT i = XFIXNUM (arg);
			  negative = i < 0;
			  x = negative ? -i : i;
			}
		    }
		  else
		    {
		      double d = XFLOAT_DATA (arg);
		      double abs_d = fabs (d);
		      if (abs_d < UINTMAX_MAX + 1.0)
			{
			  negative = d <= -1;
			  x = abs_d;
			}
		      else
			{
			  arg = double_to_integer (d);
			  goto bignum_arg;
			}
		    }
		  p[0] = negative ? '-' : plus_flag ? '+' : ' ';
		  bool signedp = negative | plus_flag | space_flag;
		  sprintf_bytes = sprintf (p + signedp, convspec, prec, x);
		  sprintf_bytes += signedp;
		}

	      /* Now the length of the formatted item is known, except it omits
		 padding and excess precision.  Deal with excess precision
		 first.  This happens when the format specifies ridiculously
		 large precision, or when %d or %i formats a float that would
		 ordinarily need fewer digits than a specified precision,
		 or when a bignum is formatted using an integer format
		 with enough precision.  */
	      ptrdiff_t excess_precision
		= precision_given ? precision - prec : 0;
	      ptrdiff_t trailing_zeros = 0;
	      if (excess_precision != 0 && float_conversion)
		{
		  if (! c_isdigit (p[sprintf_bytes - 1])
		      || (conversion == 'g'
			  && ! (sharp_flag && strchr (p, '.'))))
		    excess_precision = 0;
		  trailing_zeros = excess_precision;
		}
	      ptrdiff_t leading_zeros = excess_precision - trailing_zeros;

	      /* Compute the total bytes needed for this item, including
		 excess precision and padding.  */
	      ptrdiff_t numwidth;
	      if (ckd_add (&numwidth, prefixlen + sprintf_bytes,
			   excess_precision))
		numwidth = PTRDIFF_MAX;
	      ptrdiff_t padding
		= numwidth < field_width ? field_width - numwidth : 0;
	      if (max_bufsize - (prefixlen + sprintf_bytes) <= excess_precision
		  || max_bufsize - padding <= numwidth)
		string_overflow ();
	      convbytes = numwidth + padding;

	      if (convbytes <= buf + bufsize - p)
		{
		  bool signedp = p[0] == '-' || p[0] == '+' || p[0] == ' ';
		  int beglen = (signedp
				   + ((p[signedp] == '0'
				       && (p[signedp + 1] == 'x'
					   || p[signedp + 1] == 'X'))
				      ? 2 : 0));
		  eassert (prefixlen == 0 || beglen == 0
			   || (beglen == 1 && p[0] == '-'
			       && ! (prefix[0] == '-' || prefix[0] == '+'
				     || prefix[0] == ' ')));
		  if (zero_flag && 0 <= char_hexdigit (p[beglen]))
		    {
		      leading_zeros += padding;
		      padding = 0;
		    }
		  if (leading_zeros == 0 && sharp_flag && conversion == 'o'
		      && p[beglen] != '0')
		    {
		      leading_zeros++;
		      padding -= padding != 0;
		    }

		  int endlen = 0;
		  if (trailing_zeros
		      && (conversion == 'e' || conversion == 'g'))
		    {
		      char *e = strchr (p, 'e');
		      if (e)
			endlen = p + sprintf_bytes - e;
		    }

		  ptrdiff_t midlen = sprintf_bytes - beglen - endlen;
		  ptrdiff_t leading_padding = minus_flag ? 0 : padding;
		  ptrdiff_t trailing_padding = padding - leading_padding;

		  /* Insert padding and excess-precision zeros.  The output
		     contains the following components, in left-to-right order:

		     LEADING_PADDING spaces.
		     BEGLEN bytes taken from the start of sprintf output.
		     PREFIXLEN bytes taken from the start of the prefix array.
		     LEADING_ZEROS zeros.
		     MIDLEN bytes taken from the middle of sprintf output.
		     TRAILING_ZEROS zeros.
		     ENDLEN bytes taken from the end of sprintf output.
		     TRAILING_PADDING spaces.

		     The sprintf output is taken from the buffer starting at
		     P and continuing for SPRINTF_BYTES bytes.  */

		  ptrdiff_t incr
		    = (padding + leading_zeros + prefixlen
		       + sprintf_bytes + trailing_zeros);

		  /* Optimize for the typical case with padding or zeros.  */
		  if (incr != sprintf_bytes)
		    {
		      /* Move data to make room to insert spaces and '0's.
		         As this may entail overlapping moves, process
			 the output right-to-left and use memmove.
			 With any luck this code is rarely executed.  */
		      char *src = p + sprintf_bytes;
		      char *dst = p + incr;
		      dst -= trailing_padding;
		      memset (dst, ' ', trailing_padding);
		      src -= endlen;
		      dst -= endlen;
		      memmove (dst, src, endlen);
		      dst -= trailing_zeros;
		      memset (dst, '0', trailing_zeros);
		      src -= midlen;
		      dst -= midlen;
		      memmove (dst, src, midlen);
		      dst -= leading_zeros;
		      memset (dst, '0', leading_zeros);
		      dst -= prefixlen;
		      memcpy (dst, prefix, prefixlen);
		      src -= beglen;
		      dst -= beglen;
		      memmove (dst, src, beglen);
		      dst -= leading_padding;
		      memset (dst, ' ', leading_padding);
		    }

		  p += incr;
		  spec->start = nchars;
		  spec->end = nchars += incr;
		  new_result = true;
		  convbytes = CONVBYTES_ROOM;
		}
	    }
	}
      else
	{
	  unsigned char str[MAX_MULTIBYTE_LENGTH];

	  if ((format_char == '`' || format_char == '\'')
	      && EQ (quoting_style, Qcurve))
	    {
	      if (! multibyte)
		{
		  multibyte = true;
		  goto retry;
		}
	      convsrc = format_char == '`' ? uLSQM : uRSQM;
	      convbytes = 3;
	      new_result = true;
	    }
	  else if (format_char == '`' && EQ (quoting_style, Qstraight))
	    {
	      convsrc = "'";
	      new_result = true;
	    }
	  else
	    {
	      /* Copy a single character from format to buf.  */
	      if (multibyte_format)
		{
		  /* Copy a whole multibyte character.  */
		  if (p > buf
		      && !ASCII_CHAR_P (*((unsigned char *) p - 1))
		      && !CHAR_HEAD_P (format_char))
		    maybe_combine_byte = true;

		  while (! CHAR_HEAD_P (*format))
		    format++;

		  convbytes = format - format0;
		  memset (&discarded[format0 + 1 - format_start], 2,
			  convbytes - 1);
		}
	      else if (multibyte && !ASCII_CHAR_P (format_char))
		{
		  int c = BYTE8_TO_CHAR (format_char);
		  convbytes = CHAR_STRING (c, str);
		  convsrc = (char *) str;
		  new_result = true;
		}
	    }

	copy_char:
	  memcpy (p, convsrc, convbytes);
	  p += convbytes;
	  nchars++;
	  convbytes = CONVBYTES_ROOM;
	}

      ptrdiff_t used = p - buf;
      ptrdiff_t buflen_needed;
      if (ckd_add (&buflen_needed, used, convbytes))
	string_overflow ();
      if (bufsize <= buflen_needed)
	{
	  if (max_bufsize <= buflen_needed)
	    string_overflow ();

	  /* Either there wasn't enough room to store this conversion,
	     or there won't be enough room to do a sprintf the next
	     time through the loop.  Allocate enough room (and then some).  */

	  bufsize = (buflen_needed <= max_bufsize / 2
		     ? buflen_needed * 2 : max_bufsize);

	  if (buf == initial_buffer)
	    {
	      buf = xmalloc (bufsize);
	      buf_save_value_index = SPECPDL_INDEX ();
	      record_unwind_protect_ptr (xfree, buf);
	      memcpy (buf, initial_buffer, used);
	    }
	  else
	    {
	      buf = xrealloc (buf, bufsize);
	      set_unwind_protect_ptr (buf_save_value_index, xfree, buf);
	    }

	  p = buf + used;
	  if (convbytes != CONVBYTES_ROOM)
	    {
	      /* There wasn't enough room for this conversion; do it over.  */
	      eassert (CONVBYTES_ROOM < convbytes);
	      format = format0;
	      n = n0;
	      ispec = ispec0;
	    }
	}
    }

  if (bufsize < p - buf)
    emacs_abort ();

  if (! new_result)
    {
      val = args[0];
      goto return_val;
    }

  if (maybe_combine_byte)
    nchars = multibyte_chars_in_text ((unsigned char *) buf, p - buf);
  val = make_specified_string (buf, nchars, p - buf, multibyte);

  /* If the format string has text properties, or any of the string
     arguments has text properties, set up text properties of the
     result string.  */

  if (string_intervals (args[0]) || arg_intervals)
    {
      /* Add text properties from the format string.  */
      Lisp_Object len = make_fixnum (SCHARS (args[0]));
      Lisp_Object props = text_property_list (args[0], make_fixnum (0),
					      len, Qnil);
      if (CONSP (props))
	{
	  ptrdiff_t bytepos = 0, position = 0, translated = 0;
	  ptrdiff_t fieldn = 0;

	  /* Adjust the bounds of each text property
	     to the proper start and end in the output string.  */

	  /* Put the positions in PROPS in increasing order, so that
	     we can do (effectively) one scan through the position
	     space of the format string.  */
	  props = Fnreverse (props);

	  /* BYTEPOS is the byte position in the format string,
	     POSITION is the untranslated char position in it,
	     TRANSLATED is the translated char position in BUF,
	     and ARGN is the number of the next arg we will come to.  */
	  for (Lisp_Object list = props; CONSP (list); list = XCDR (list))
	    {
	      Lisp_Object item = XCAR (list);

	      /* First adjust the property start position.  */
	      ptrdiff_t pos = XFIXNUM (XCAR (item));

	      /* Advance BYTEPOS, POSITION, TRANSLATED and ARGN
		 up to this position.  */
	      for (; position < pos; bytepos++)
		{
		  if (! discarded[bytepos])
		    position++, translated++;
		  else if (discarded[bytepos] == 1)
		    {
		      position++;
		      if (fieldn < nspec
			  && bytepos >= info[fieldn].fbeg
			  && translated == info[fieldn].start)
			{
			  translated += info[fieldn].end - info[fieldn].start;
			  fieldn++;
			}
		    }
		}

	      XSETCAR (item, make_fixnum (translated));

	      /* Likewise adjust the property end position.  */
	      pos = XFIXNUM (XCAR (XCDR (item)));

	      for (; position < pos; bytepos++)
		{
		  if (! discarded[bytepos])
		    position++, translated++;
		  else if (discarded[bytepos] == 1)
		    {
		      position++;
		      if (fieldn < nspec
			  && bytepos >= info[fieldn].fbeg
			  && translated == info[fieldn].start)
			{
			  translated += info[fieldn].end - info[fieldn].start;
			  fieldn++;
			}
		    }
		}

	      XSETCAR (XCDR (item), make_fixnum (translated));
	    }

	  add_text_properties_from_list (val, props, make_fixnum (0));
	}

      /* Add text properties from arguments.  */
      if (arg_intervals)
	for (ptrdiff_t i = 0; i < nspec; i++)
	  if (info[i].intervals)
	    {
	      len = make_fixnum (SCHARS (spec_arguments[i]));
	      Lisp_Object new_len = make_fixnum (info[i].end - info[i].start);
	      props = text_property_list (spec_arguments[i],
                                          make_fixnum (0), len, Qnil);
	      props = extend_property_ranges (props, len, new_len);
	      /* If successive arguments have properties, be sure that
		 the value of `composition' property be the copy.  */
	      if (1 < i && info[i - 1].end)
		make_composition_value_copy (props);
	      add_text_properties_from_list (val, props,
					     make_fixnum (info[i].start));
	    }
    }

 return_val:
  /* If we allocated BUF or INFO with malloc, free it too.  */
  SAFE_FREE ();

  return val;
}

DEFUN ("char-equal", Fchar_equal, Schar_equal, 2, 2, 0,
       doc: /* Return t if two characters match, optionally ignoring case.
Both arguments must be characters (i.e. integers).
Case is ignored if `case-fold-search' is non-nil in the current buffer.  */)
  (register Lisp_Object c1, Lisp_Object c2)
{
  int i1, i2;
  /* Check they're chars, not just integers, otherwise we could get array
     bounds violations in downcase.  */
  CHECK_CHARACTER (c1);
  CHECK_CHARACTER (c2);

  if (XFIXNUM (c1) == XFIXNUM (c2))
    return Qt;
  if (NILP (Vcase_fold_search))
    return Qnil;

  i1 = XFIXNAT (c1);
  i2 = XFIXNAT (c2);

  /* FIXME: It is possible to compare multibyte characters even when
     the current buffer is unibyte.  Unfortunately this is ambiguous
     for characters between 128 and 255, as they could be either
     eight-bit raw bytes or Latin-1 characters.  Assume the former for
     now.  See Bug#17011, and also see casefiddle.c's casify_object,
     which has a similar problem.  */
  if (NILP (BVAR (current_buffer, enable_multibyte_characters)))
    {
      if (SINGLE_BYTE_CHAR_P (i1))
	i1 = UNIBYTE_TO_CHAR (i1);
      if (SINGLE_BYTE_CHAR_P (i2))
	i2 = UNIBYTE_TO_CHAR (i2);
    }

  return (downcase (i1) == downcase (i2) ? Qt :  Qnil);
}

/* Transpose the markers in two regions of the current buffer, and
   adjust the ones between them if necessary (i.e.: if the regions
   differ in size).

   START1, END1 are the character positions of the first region.
   START1_BYTE, END1_BYTE are the byte positions.
   START2, END2 are the character positions of the second region.
   START2_BYTE, END2_BYTE are the byte positions.

   Traverses the entire marker list of the buffer to do so, adding an
   appropriate amount to some, subtracting from some, and leaving the
   rest untouched.  Most of this is copied from adjust_markers in insdel.c.

   It's the caller's job to ensure that START1 <= END1 <= START2 <= END2.  */

static void
transpose_markers (ptrdiff_t start1, ptrdiff_t end1,
		   ptrdiff_t start2, ptrdiff_t end2,
		   ptrdiff_t start1_byte, ptrdiff_t end1_byte,
		   ptrdiff_t start2_byte, ptrdiff_t end2_byte)
{
  register ptrdiff_t amt1, amt1_byte, amt2, amt2_byte, diff, diff_byte, mpos;
  register struct Lisp_Marker *marker;

  /* Update point as if it were a marker.  */
  if (PT < start1)
    ;
  else if (PT < end1)
    TEMP_SET_PT_BOTH (PT + (end2 - end1),
		      PT_BYTE + (end2_byte - end1_byte));
  else if (PT < start2)
    TEMP_SET_PT_BOTH (PT + (end2 - start2) - (end1 - start1),
		      (PT_BYTE + (end2_byte - start2_byte)
		       - (end1_byte - start1_byte)));
  else if (PT < end2)
    TEMP_SET_PT_BOTH (PT - (start2 - start1),
		      PT_BYTE - (start2_byte - start1_byte));

  /* We used to adjust the endpoints here to account for the gap, but that
     isn't good enough.  Even if we assume the caller has tried to move the
     gap out of our way, it might still be at start1 exactly, for example;
     and that places it `inside' the interval, for our purposes.  The amount
     of adjustment is nontrivial if there's a `denormalized' marker whose
     position is between GPT and GPT + GAP_SIZE, so it's simpler to leave
     the dirty work to Fmarker_position, below.  */

  /* The difference between the region's lengths */
  diff = (end2 - start2) - (end1 - start1);
  diff_byte = (end2_byte - start2_byte) - (end1_byte - start1_byte);

  /* For shifting each marker in a region by the length of the other
     region plus the distance between the regions.  */
  amt1 = (end2 - start2) + (start2 - end1);
  amt2 = (end1 - start1) + (start2 - end1);
  amt1_byte = (end2_byte - start2_byte) + (start2_byte - end1_byte);
  amt2_byte = (end1_byte - start1_byte) + (start2_byte - end1_byte);

  for (marker = BUF_MARKERS (current_buffer); marker; marker = marker->next)
    {
      mpos = marker->bytepos;
      if (mpos >= start1_byte && mpos < end2_byte)
	{
	  if (mpos < end1_byte)
	    mpos += amt1_byte;
	  else if (mpos < start2_byte)
	    mpos += diff_byte;
	  else
	    mpos -= amt2_byte;
	  marker->bytepos = mpos;
	}
      mpos = marker->charpos;
      if (mpos >= start1 && mpos < end2)
	{
	  if (mpos < end1)
	    mpos += amt1;
	  else if (mpos < start2)
	    mpos += diff;
	  else
	    mpos -= amt2;
	}
      marker->charpos = mpos;
    }
}

DEFUN ("transpose-regions", Ftranspose_regions, Stranspose_regions, 4, 5,
       "(if (< (length mark-ring) 2)\
	    (error \"Other region must be marked before transposing two regions\")\
	  (let* ((num (if current-prefix-arg\
			 (prefix-numeric-value current-prefix-arg)\
			0))\
		 (ring-length (length mark-ring))\
		 (eltnum (mod num ring-length))\
		 (eltnum2 (mod (1+ num) ring-length)))\
	    (list (point) (mark) (elt mark-ring eltnum) (elt mark-ring eltnum2))))",
       doc: /* Transpose region STARTR1 to ENDR1 with STARTR2 to ENDR2.
The regions should not be overlapping, because the size of the buffer is
never changed in a transposition.

Optional fifth arg LEAVE-MARKERS, if non-nil, means don't update
any markers that happen to be located in the regions.

Transposing beyond buffer boundaries is an error.

Interactively, STARTR1 and ENDR1 are point and mark; STARTR2 and ENDR2
are the last two marks pushed to the mark ring; LEAVE-MARKERS is nil.
If a prefix argument N is given, STARTR2 and ENDR2 are the two
successive marks N entries back in the mark ring.  A negative prefix
argument instead counts forward from the oldest mark in the mark
ring.  */)
  (Lisp_Object startr1, Lisp_Object endr1, Lisp_Object startr2, Lisp_Object endr2, Lisp_Object leave_markers)
{
  register ptrdiff_t start1, end1, start2, end2;
  ptrdiff_t start1_byte, start2_byte, len1_byte, len2_byte, end2_byte;
  ptrdiff_t gap, len1, len_mid, len2;
  unsigned char *start1_addr, *start2_addr, *temp;

  INTERVAL cur_intv, tmp_interval1, tmp_interval2, tmp_interval3;
  Lisp_Object buf;

  XSETBUFFER (buf, current_buffer);
  cur_intv = buffer_intervals (current_buffer);

  validate_region (&startr1, &endr1);
  validate_region (&startr2, &endr2);

  start1 = XFIXNAT (startr1);
  end1 = XFIXNAT (endr1);
  start2 = XFIXNAT (startr2);
  end2 = XFIXNAT (endr2);
  gap = GPT;

  /* Swap the regions if they're reversed.  */
  if (start2 < end1)
    {
      register ptrdiff_t glumph = start1;
      start1 = start2;
      start2 = glumph;
      glumph = end1;
      end1 = end2;
      end2 = glumph;
    }

  len1 = end1 - start1;
  len2 = end2 - start2;

  if (start2 < end1)
    error ("Transposed regions overlap");
  /* Nothing to change for adjacent regions with one being empty */
  else if ((start1 == end1 || start2 == end2) && end1 == start2)
    return Qnil;

  /* The possibilities are:
     1. Adjacent (contiguous) regions, or separate but equal regions
     (no, really equal, in this case!), or
     2. Separate regions of unequal size.

     The worst case is usually No. 2.  It means that (aside from
     potential need for getting the gap out of the way), there also
     needs to be a shifting of the text between the two regions.  So
     if they are spread far apart, we are that much slower... sigh.  */

  /* It must be pointed out that the really studly thing to do would
     be not to move the gap at all, but to leave it in place and work
     around it if necessary.  This would be extremely efficient,
     especially considering that people are likely to do
     transpositions near where they are working interactively, which
     is exactly where the gap would be found.  However, such code
     would be much harder to write and to read.  So, if you are
     reading this comment and are feeling squirrely, by all means have
     a go!  I just didn't feel like doing it, so I will simply move
     the gap the minimum distance to get it out of the way, and then
     deal with an unbroken array.  */

  start1_byte = CHAR_TO_BYTE (start1);
  end2_byte = CHAR_TO_BYTE (end2);

#ifdef HAVE_TREE_SITTER
  struct ts_linecol start_linecol
    = treesit_linecol_maybe (start1, start1_byte,
			     BUF_TS_LINECOL_POINT (current_buffer));
  struct ts_linecol old_end_linecol
    = treesit_linecol_maybe (end2, end2_byte,
			     BUF_TS_LINECOL_POINT (current_buffer));
#endif

  /* Run the before-change-functions *before* we move the gap.  */
  modify_text (start1, end2);

  /* Make sure the gap won't interfere, by moving it out of the text
     we will operate on.  */
  if (start1 < gap && gap < end2)
    {
      if (gap - start1 < end2 - gap)
	move_gap_both (start1, start1_byte);
      else
	move_gap_both (end2, end2_byte);
    }

  start2_byte = CHAR_TO_BYTE (start2);
  ptrdiff_t end1_byte = CHAR_TO_BYTE (end1);
  len1_byte = end1_byte - start1_byte;
  len2_byte = end2_byte - start2_byte;

#ifdef BYTE_COMBINING_DEBUG
  if (end1 == start2)
    {
      if (count_combining_before (BYTE_POS_ADDR (start2_byte),
				  len2_byte, start1, start1_byte)
	  || count_combining_before (BYTE_POS_ADDR (start1_byte),
				     len1_byte, end2, start2_byte + len2_byte)
	  || count_combining_after (BYTE_POS_ADDR (start1_byte),
				    len1_byte, end2, start2_byte + len2_byte))
	emacs_abort ();
    }
  else
    {
      if (count_combining_before (BYTE_POS_ADDR (start2_byte),
				  len2_byte, start1, start1_byte)
	  || count_combining_before (BYTE_POS_ADDR (start1_byte),
				     len1_byte, start2, start2_byte)
	  || count_combining_after (BYTE_POS_ADDR (start2_byte),
				    len2_byte, end1, start1_byte + len1_byte)
	  || count_combining_after (BYTE_POS_ADDR (start1_byte),
				    len1_byte, end2, start2_byte + len2_byte))
	emacs_abort ();
    }
#endif

  /* Hmmm... how about checking to see if the gap is large
     enough to use as the temporary storage?  That would avoid an
     allocation... interesting.  Later, don't fool with it now.  */

  tmp_interval1 = copy_intervals (cur_intv, start1, len1);
  tmp_interval2 = copy_intervals (cur_intv, start2, len2);
  USE_SAFE_ALLOCA;
  if (len1_byte == len2_byte && len1 == len2)
    /* Regions are same size, though, how nice.  */
    /* The char lengths also have to match, for text-properties.  */
    {
      if (end1 == start2)	/* Merge the two parts into a single one.  */
	record_change (start1, (end2 - start1));
      else
	{
	  record_change (start1, len1);
	  record_change (start2, len2);
	}

      tmp_interval3 = validate_interval_range (buf, &startr1, &endr1, 0);
      if (tmp_interval3)
	set_text_properties_1 (startr1, endr1, Qnil, buf, tmp_interval3);

      tmp_interval3 = validate_interval_range (buf, &startr2, &endr2, 0);
      if (tmp_interval3)
	set_text_properties_1 (startr2, endr2, Qnil, buf, tmp_interval3);

      temp = SAFE_ALLOCA (len1_byte);
      start1_addr = BYTE_POS_ADDR (start1_byte);
      start2_addr = BYTE_POS_ADDR (start2_byte);
      memcpy (temp, start1_addr, len1_byte);
      memcpy (start1_addr, start2_addr, len2_byte);
      memcpy (start2_addr, temp, len1_byte);
    }
  else
    {
      len_mid = start2_byte - end1_byte;
      record_change (start1, (end2 - start1));
      INTERVAL tmp_interval_mid = copy_intervals (cur_intv, end1, len_mid);
      tmp_interval3 = validate_interval_range (buf, &startr1, &endr2, 0);
      if (tmp_interval3)
	set_text_properties_1 (startr1, endr2, Qnil, buf, tmp_interval3);
      if (len1_byte < len2_byte)	/* Second region larger than first */
	{
	  /* holds region 2 */
	  temp = SAFE_ALLOCA (len2_byte);
	  start1_addr = BYTE_POS_ADDR (start1_byte);
	  start2_addr = BYTE_POS_ADDR (start2_byte);
	  memcpy (temp, start2_addr, len2_byte);
	  memcpy (start1_addr + len_mid + len2_byte, start1_addr, len1_byte);
	  memmove (start1_addr + len2_byte, start1_addr + len1_byte, len_mid);
	  memcpy (start1_addr, temp, len2_byte);
	}
      else
	/* Second region smaller than first.  */
	{
	  /* holds region 1 */
	  temp = SAFE_ALLOCA (len1_byte);
	  start1_addr = BYTE_POS_ADDR (start1_byte);
	  start2_addr = BYTE_POS_ADDR (start2_byte);
	  memcpy (temp, start1_addr, len1_byte);
	  memcpy (start1_addr, start2_addr, len2_byte);
	  memmove (start1_addr + len2_byte, start1_addr + len1_byte, len_mid);
	  memcpy (start1_addr + len2_byte + len_mid, temp, len1_byte);
	}
      graft_intervals_into_buffer (tmp_interval_mid, start1 + len2,
                                   len_mid, current_buffer, 0);
    }
  SAFE_FREE ();
  graft_intervals_into_buffer (tmp_interval1, end2 - len1,
                               len1, current_buffer, 0);
  graft_intervals_into_buffer (tmp_interval2, start1,
                               len2, current_buffer, 0);

  update_compositions (start1, start1 + len2, CHECK_BORDER);
  update_compositions (end2 - len1, end2, CHECK_BORDER);

  /* When doing multiple transpositions, it might be nice
     to optimize this.  Perhaps the markers in any one buffer
     should be organized in some sorted data tree.  */
  if (NILP (leave_markers))
    {
      /* FIXME: Since the undo info doesn't record the transposition as its own
	 operation, we won't enjoy 'transpose_markers' during undo :-(  */
      transpose_markers (start1, end1, start2, end2,
			 start1_byte, start1_byte + len1_byte,
			 start2_byte, start2_byte + len2_byte);
    }
  else
    {
      /* The character positions of the markers remain intact, but we
	 still need to update their byte positions, because the
	 transposed regions might include multibyte sequences which
	 make some original byte positions of the markers invalid.  */
      adjust_markers_bytepos (start1, start1_byte, end2, end2_byte, 0);
    }

#ifdef HAVE_TREE_SITTER
  treesit_record_change (start1_byte, end2_byte, end2_byte,
			 start_linecol, old_end_linecol, end2);
#endif

  signal_after_change (start1, end2 - start1, end2 - start1);
  return Qnil;
}


void
syms_of_editfns (void)
{
  DEFSYM (Qbuffer_access_fontify_functions, "buffer-access-fontify-functions");
  DEFSYM (Qwall, "wall");
  DEFSYM (Qpropertize, "propertize");

  staticpro (&labeled_restrictions);

  DEFVAR_LISP ("inhibit-field-text-motion", Vinhibit_field_text_motion,
	       doc: /* Non-nil means text motion commands don't notice fields.  */);
  Vinhibit_field_text_motion = Qnil;

  DEFVAR_LISP ("buffer-access-fontify-functions",
	       Vbuffer_access_fontify_functions,
	       doc: /* List of functions called by `buffer-substring' to fontify if necessary.
Each function is called with two arguments which specify the range
of the buffer being accessed.  */);
  Vbuffer_access_fontify_functions = Qnil;

  {
    Lisp_Object obuf;
    obuf = Fcurrent_buffer ();
    /* Do this here, because init_buffer_once is too early--it won't work.  */
    Fset_buffer (Vprin1_to_string_buffer);
    /* Make sure buffer-access-fontify-functions is nil in this buffer.  */
    Fset (Fmake_local_variable (Qbuffer_access_fontify_functions), Qnil);
    Fset_buffer (obuf);
  }

  DEFVAR_LISP ("buffer-access-fontified-property",
	       Vbuffer_access_fontified_property,
	       doc: /* Property which (if non-nil) indicates text has been fontified.
`buffer-substring' need not call the `buffer-access-fontify-functions'
functions if all the text being accessed has this property.  */);
  Vbuffer_access_fontified_property = Qnil;

  DEFVAR_LISP ("system-name", Vsystem_name,
	       doc: /* The host name of the machine Emacs is running on.  */);
  Vsystem_name = cached_system_name = Qnil;

  DEFVAR_LISP ("user-full-name", Vuser_full_name,
	       doc: /* The full name of the user logged in.  */);

  DEFVAR_LISP ("user-login-name", Vuser_login_name,
	       doc: /* The user's name, taken from environment variables if possible.  */);
  Vuser_login_name = Qnil;

  DEFVAR_LISP ("user-real-login-name", Vuser_real_login_name,
	       doc: /* The user's name, based upon the real uid only.  */);

  DEFVAR_LISP ("operating-system-release", Voperating_system_release,
	       doc: /* The kernel version of the operating system on which Emacs is running.
The value is a string.  It can also be nil if Emacs doesn't
know how to get the kernel version on the underlying OS.  */);

  DEFVAR_BOOL ("binary-as-unsigned",
	       binary_as_unsigned,
	       doc: /* Non-nil means `format' %x and %o treat integers as unsigned.
This has machine-dependent results.  Nil means to treat integers as
signed, which is portable and is the default; for example, if N is a
negative integer, (read (format "#x%x" N)) returns N only when this
variable is nil.

This variable is experimental; email 32252@debbugs.gnu.org if you need
it to be non-nil.  */);
  binary_as_unsigned = false;

  DEFSYM (Qoutermost_restriction, "outermost-restriction");
  Funintern (Qoutermost_restriction, Qnil);

  defsubr (&Spropertize);
  defsubr (&Schar_equal);
  defsubr (&Sgoto_char);
  defsubr (&Sstring_to_char);
  defsubr (&Schar_to_string);
  defsubr (&Sbyte_to_string);
  defsubr (&Sbuffer_substring);
  defsubr (&Sbuffer_substring_no_properties);
  defsubr (&Sbuffer_string);
  defsubr (&Sget_pos_property);

  defsubr (&Spoint_marker);
  defsubr (&Smark_marker);
  defsubr (&Spoint);
  defsubr (&Sregion_beginning);
  defsubr (&Sregion_end);

  /* Symbol for the text property used to mark fields.  */
  DEFSYM (Qfield, "field");

  /* A special value for Qfield properties.  */
  DEFSYM (Qboundary, "boundary");

  defsubr (&Sfield_beginning);
  defsubr (&Sfield_end);
  defsubr (&Sfield_string);
  defsubr (&Sfield_string_no_properties);
  defsubr (&Sdelete_field);
  defsubr (&Sconstrain_to_field);

  defsubr (&Sline_beginning_position);
  defsubr (&Sline_end_position);
  defsubr (&Spos_bol);
  defsubr (&Spos_eol);

  defsubr (&Ssave_excursion);
  defsubr (&Ssave_current_buffer);

  defsubr (&Sbuffer_size);
  defsubr (&Spoint_max);
  defsubr (&Spoint_min);
  defsubr (&Spoint_min_marker);
  defsubr (&Spoint_max_marker);
  defsubr (&Sgap_position);
  defsubr (&Sgap_size);
  defsubr (&Sposition_bytes);
  defsubr (&Sbyte_to_position);

  defsubr (&Sbobp);
  defsubr (&Seobp);
  defsubr (&Sbolp);
  defsubr (&Seolp);
  defsubr (&Sfollowing_char);
  defsubr (&Sprevious_char);
  defsubr (&Schar_after);
  defsubr (&Schar_before);
  defsubr (&Sinsert);
  defsubr (&Sinsert_before_markers);
  defsubr (&Sinsert_and_inherit);
  defsubr (&Sinsert_and_inherit_before_markers);
  defsubr (&Sinsert_char);
  defsubr (&Sinsert_byte);

  defsubr (&Sngettext);

  defsubr (&Suser_login_name);
  defsubr (&Sgroup_name);
  defsubr (&Suser_real_login_name);
  defsubr (&Suser_uid);
  defsubr (&Suser_real_uid);
  defsubr (&Sgroup_gid);
  defsubr (&Sgroup_real_gid);
  defsubr (&Suser_full_name);
  defsubr (&Semacs_pid);
  defsubr (&Ssystem_name);
  defsubr (&Smessage);
  defsubr (&Smessage_box);
  defsubr (&Smessage_or_box);
  defsubr (&Scurrent_message);
  defsubr (&Sformat);
  defsubr (&Sformat_message);

  defsubr (&Sinsert_buffer_substring);
  defsubr (&Scompare_buffer_substrings);
  defsubr (&Sreplace_region_contents);
  defsubr (&Ssubst_char_in_region);
  defsubr (&Stranslate_region_internal);
  defsubr (&Sdelete_region);
  defsubr (&Sdelete_and_extract_region);
  defsubr (&Swiden);
  defsubr (&Snarrow_to_region);
  defsubr (&Sinternal__labeled_narrow_to_region);
  defsubr (&Sinternal__labeled_widen);
  defsubr (&Ssave_restriction);
  defsubr (&Stranspose_regions);
}
