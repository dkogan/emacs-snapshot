;;; composite.el --- support character composition  -*- lexical-binding: t; -*-

;; Copyright (C) 2001-2025 Free Software Foundation, Inc.

;; Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007,
;;   2008, 2009, 2010, 2011
;;   National Institute of Advanced Industrial Science and Technology (AIST)
;;   Registration Number H14PRO021

;; Author: Kenichi Handa <handa@gnu.org>
;; (according to ack.texi)
;; Keywords: mule, multilingual, character composition
;; Package: emacs

;; This file is part of GNU Emacs.

;; GNU Emacs is free software: you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation, either version 3 of the License, or
;; (at your option) any later version.

;; GNU Emacs is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.

;;; Commentary:

;;; Code:

(defconst reference-point-alist
  '((tl . 0) (tc . 1) (tr . 2)
    (Bl . 3) (Bc . 4) (Br . 5)
    (bl . 6) (bc . 7) (br . 8)
    (cl . 9) (cc . 10) (cr . 11)
    (top-left . 0) (top-center . 1) (top-right . 2)
    (base-left . 3) (base-center . 4) (base-right . 5)
    (bottom-left . 6) (bottom-center . 7) (bottom-right . 8)
    (center-left . 9) (center-center . 10) (center-right . 11)
    ;; For backward compatibility...
    (ml . 3) (mc . 10) (mr . 5)
    (mid-left . 3) (mid-center . 10) (mid-right . 5))
  "Alist of symbols vs integer codes of glyph reference points.
A glyph reference point symbol is to be used to specify a composition
rule in COMPONENTS argument to such functions as `compose-region'.

The meaning of glyph reference point codes is as follows:

    0----1----2 <---- ascent	0:tl or top-left
    |         |			1:tc or top-center
    |         |			2:tr or top-right
    |         |			3:Bl or base-left     9:cl or center-left
    9   10   11 <---- center	4:Bc or base-center  10:cc or center-center
    |         |			5:Br or base-right   11:cr or center-right
  --3----4----5-- <-- baseline	6:bl or bottom-left
    |         |			7:bc or bottom-center
    6----7----8 <---- descent	8:br or bottom-right

Glyph reference point symbols are to be used to specify a composition
rule of the form (GLOBAL-REF-POINT . NEW-REF-POINT), where
GLOBAL-REF-POINT is a reference point in the overall glyphs already
composed, and NEW-REF-POINT is a reference point in the new glyph to
be added.

For instance, if GLOBAL-REF-POINT is `br' (bottom-right) and
NEW-REF-POINT is `tc' (top-center), the overall glyph is updated as
follows (the point `*' corresponds to both reference points):

    +-------+--+ <--- new ascent
    |       |  |
    | global|  |
    | glyph |  |
 -- |       |  |-- <--- baseline (doesn't change)
    +----+--*--+
    |    | new |
    |    |glyph|
    +----+-----+ <--- new descent

A composition rule may have the form (GLOBAL-REF-POINT
NEW-REF-POINT XOFF YOFF), where XOFF and YOFF specify how much
to shift NEW-REF-POINT from GLOBAL-REF-POINT.  In this case, XOFF
and YOFF are integers in the range -100..100 representing the
shifting percentage against the font size.")


;;;###autoload
(defun encode-composition-rule (rule)
  "Encode composition rule RULE into an integer value.
RULE is a cons of global and new reference point symbols
\(see `reference-point-alist')."

  ;; This must be compatible with C macro COMPOSITION_ENCODE_RULE
  ;; defined in composite.h.

  (if (and (integerp rule) (< rule 144))
      ;; Already encoded.
      rule
    (if (consp rule)
	(let ((gref (car rule))
	      (nref (cdr rule))
	      xoff yoff)
	  (if (consp nref)		; (GREF NREF XOFF YOFF)
	      (progn
		(setq xoff (nth 1 nref)
		      yoff (nth 2 nref)
		      nref (car nref))
		(or (and (>= xoff -100) (<= xoff 100)
			 (>= yoff -100) (<= yoff 100))
		    (error "Invalid composition rule: %s" rule))
		(setq xoff (+ xoff 128) yoff (+ yoff 128)))
	    ;; (GREF . NREF)
	    (setq xoff 0 yoff 0))
	  (or (integerp gref)
	      (setq gref (cdr (assq gref reference-point-alist))))
	  (or (integerp nref)
	      (setq nref (cdr (assq nref reference-point-alist))))
	  (or (and (>= gref 0) (< gref 12) (>= nref 0) (< nref 12))
	      (error "Invalid composition rule: %S" rule))
	  (logior (ash xoff 16) (ash yoff 8) (+ (* gref 12) nref)))
      (error "Invalid composition rule: %S" rule))))

;; Decode encoded composition rule RULE-CODE.  The value is a cons of
;; global and new reference point symbols.
;; This must be compatible with C macro COMPOSITION_DECODE_RULE
;; defined in composite.h.

(defun decode-composition-rule (rule-code)
  (or (and (natnump rule-code) (< rule-code #x1000000))
      (error "Invalid encoded composition rule: %S" rule-code))
  (let ((xoff (ash rule-code -16))
	(yoff (logand (ash rule-code -8) #xFF))
	gref nref)
    (setq rule-code (logand rule-code #xFF)
	  gref (car (rassq (/ rule-code 12) reference-point-alist))
	  nref (car (rassq (% rule-code 12) reference-point-alist)))
    (or (and gref (symbolp gref) nref (symbolp nref))
	(error "Invalid composition rule code: %S" rule-code))
    (if (and (= xoff 0) (= yoff 0))
	(cons gref nref)
      (setq xoff (- xoff 128) yoff (- yoff 128))
      (list gref xoff yoff nref))))

;; Encode composition rules in composition components COMPONENTS.  The
;; value is a copy of COMPONENTS, where composition rules (cons of
;; global and new glyph reference point symbols) are replaced with
;; encoded composition rules.  Optional 2nd argument NOCOPY non-nil
;; means don't make a copy but modify COMPONENTS directly.

(defun encode-composition-components (components &optional nocopy)
  (or nocopy
      (setq components (copy-sequence components)))
  (if (vectorp components)
      (let ((len (length components))
	    (i 1))
	(while (< i len)
	  (aset components i
		(encode-composition-rule (aref components i)))
	  (setq i (+ i 2))))
    (let ((tail (cdr components)))
      (while tail
	(setcar tail
		(encode-composition-rule (car tail)))
	(setq tail (nthcdr 2 tail)))))
  components)

;; Decode composition rule codes in composition components COMPONENTS.
;; The value is a copy of COMPONENTS, where composition rule codes are
;; replaced with composition rules (cons of global and new glyph
;; reference point symbols).  Optional 2nd argument NOCOPY non-nil
;; means don't make a copy but modify COMPONENTS directly.
;; It is assumed that COMPONENTS is a vector and is for rule-base
;; composition, thus (2N+1)th elements are rule codes.

(defun decode-composition-components (components &optional nocopy)
  (or nocopy
      (setq components (copy-sequence components)))
  (let ((len (length components))
	(i 1))
    (while (< i len)
      (aset components i
	    (decode-composition-rule (aref components i)))
      (setq i (+ i 2))))
  components)

(defun compose-region (start end &optional components modification-func)
  "Compose characters in the current region.

Characters are composed relatively, i.e. composed by overstriking
or stacking depending on ascent, descent and other metrics of
glyphs.

For instance, if the region has three characters \"XYZ\", X is
regarded as BASE glyph, and Y is displayed:
  (1) above BASE if Y's descent value is not positive
  (2) below BASE if Y's ascent value is not positive
  (3) on BASE (i.e. at the BASE position) otherwise
and Z is displayed with the same rule while regarding the whole
XY glyphs as BASE.

When called from a program, expects these four arguments.

First two arguments START and END are positions (integers or markers)
specifying the region.

Optional 3rd argument COMPONENTS, if non-nil, is a character, a string
or a vector or list of integers and rules.

If it is a character, it is an alternate character to display instead
of the text in the region.

If it is a string, the elements are alternate characters.  In
this case, TAB element has a special meaning.  If the first
character is TAB, the glyphs are displayed with left padding space
so that no pixel overlaps with the previous column.  If the last
character is TAB, the glyphs are displayed with right padding
space so that no pixel overlaps with the following column.

If it is a vector or list, it is a sequence of alternate characters and
composition rules, where (2N)th elements are characters and (2N+1)th
elements are composition rules to specify how to compose (2N+2)th
elements with previously composed N glyphs.

A composition rule is a cons of global and new glyph reference point
symbols.  See the documentation of `reference-point-alist' for more
details.

Optional 4th argument MODIFICATION-FUNC is a function to call to
adjust the composition when it gets invalid because of a change of
text in the composition."
  (interactive "r")
  (let ((modified-p (buffer-modified-p))
	(inhibit-read-only t))
    (if (or (vectorp components) (listp components))
	(setq components (encode-composition-components components)))
    (compose-region-internal start end components modification-func)
    (restore-buffer-modified-p modified-p)))

(defun decompose-region (start end)
  "Decompose text in the current region.

When called from a program, expects two arguments,
positions (integers or markers) specifying the region."
  (interactive "r")
  (let ((modified-p (buffer-modified-p))
	(inhibit-read-only t))
    (remove-text-properties start end '(composition nil))
    (restore-buffer-modified-p modified-p)))

(defun compose-string (string &optional start end components modification-func)
  "Compose characters in string STRING.

The return value is STRING with the `composition' property put on all
the characters in it.

Optional 2nd and 3rd arguments START and END specify the range of
STRING to be composed.  They default to the beginning and the end of
STRING respectively.

Optional 4th argument COMPONENTS, if non-nil, is a character or a
sequence (vector, list, or string) of integers.  See the function
`compose-region' for more detail.

Optional 5th argument MODIFICATION-FUNC is a function to call to
adjust the composition when it gets invalid because of a change of
text in the composition."
  (if (or (vectorp components) (listp components))
      (setq components (encode-composition-components components)))
  (or start (setq start 0))
  (or end (setq end (length string)))
  (compose-string-internal string start end components modification-func)
  string)

(defun decompose-string (string)
  "Return STRING where `composition' property is removed."
  (remove-text-properties 0 (length string) '(composition nil) string)
  string)

(defun compose-chars (&rest args)
  "Return a string from arguments in which all characters are composed.
For relative composition, arguments are characters.
For rule-based composition, Mth (where M is odd) arguments are
characters, and Nth (where N is even) arguments are composition rules.
A composition rule is a cons of glyph reference points of the form
\(GLOBAL-REF-POINT . NEW-REF-POINT).  See the documentation of
`reference-point-alist' for more detail."
  (let (str components)
    (if (consp (car (cdr args)))
	;; Rule-base composition.
	(let ((tail (encode-composition-components args 'nocopy)))
	  (while tail
	    (setq str (cons (car tail) str))
	    (setq tail (nthcdr 2 tail)))
	  (setq str (concat (nreverse str))
		components args))
      ;; Relative composition.
      (setq str (concat args)))
    (compose-string-internal str 0 (length str) components)))

(defun find-composition (pos &optional limit string detail-p)
  "Return information about a composition at or near buffer position POS.

If the character at POS has `composition' property, the value is a list
\(FROM TO VALID-P).

FROM and TO specify the range of text that has the same `composition'
property, VALID-P is t if this composition is valid, and nil if not.

If there's no composition at POS, and the optional 2nd argument LIMIT
is non-nil, search for a composition toward the position given by LIMIT.

If no composition is found, return nil.

Optional 3rd argument STRING, if non-nil, is a string to look for a
composition in; nil means the current buffer.

If a valid composition is found and the optional 4th argument DETAIL-P
is non-nil, the return value is a list of the form

   (FROM TO COMPONENTS RELATIVE-P MOD-FUNC WIDTH)

COMPONENTS is a vector of integers, the meaning depends on RELATIVE-P.

RELATIVE-P is t if the composition method is relative, else nil.

If RELATIVE-P is t, COMPONENTS is a vector of characters to be
composed.  If RELATIVE-P is nil, COMPONENTS is a vector of characters
and composition rules as described in `compose-region'.

MOD-FUNC is a modification function of the composition.

WIDTH is a number of columns the composition occupies on the screen.

When Automatic Composition mode is on, this function also finds a
chunk of text that is automatically composed.  If such a chunk is
found closer to POS than the position that has `composition'
property, the value is a list of FROM, TO, and a glyph-string
that specifies how the chunk is to be composed; DETAIL-P is
ignored in this case.  See the function `composition-get-gstring'
for the format of the glyph-string."
  (let ((result (find-composition-internal pos limit string detail-p)))
    (if (and detail-p (> (length result) 3) (nth 2 result) (not (nth 3 result)))
	;; This is a valid rule-base composition.
	(decode-composition-components (nth 2 result) 'nocopy))
    result))


(defun compose-chars-after (pos &optional limit object)
  "Compose characters in current buffer after position POS.

It looks up the char-table `composition-function-table' (which
see) by a character at POS, and compose characters after POS
according to the contents of `composition-function-table'.

Optional 2nd arg LIMIT, if non-nil, limits characters to compose.

Optional 3rd arg OBJECT, if non-nil, is a string that contains the
text to compose.  In that case, POS and LIMIT index into the string.

This function is the default value of `compose-chars-after-function'."
  (let ((tail (aref composition-function-table (char-after pos)))
	(font-obj (and (display-multi-font-p)
		       (and (not (stringp object))
			    (font-at pos (selected-window)))))
	pattern func result)
    (or limit
	(setq limit (if (stringp object) (length object) (point-max))))
    (when (and font-obj tail)
      (save-match-data
	(save-excursion
	  (while tail
	    (if (functionp (car tail))
		(setq pattern nil func (car tail))
	      (setq pattern (car (car tail))
		    func (cdr (car tail))))
	    (goto-char pos)
	    (if pattern
		(if (and (if (stringp object)
			     (eq (string-match pattern object) 0)
			   (looking-at pattern))
			 (<= (match-end 0) limit))
		    (setq result
			  (funcall func pos (match-end 0) font-obj object nil)))
	      (setq result (funcall func pos limit font-obj  object nil)))
	    (if result (setq tail nil))))))
    result))

(defun compose-last-chars (args)
  "Compose last characters.
The argument is a parameterized event of the form
	(compose-last-chars N COMPONENTS),
where N is the number of characters before point to compose,
COMPONENTS, if non-nil, is the same as the argument to `compose-region'
\(which see).  If it is nil, `compose-chars-after' is called,
and that function finds a proper rule to compose the target characters.
This function is intended to be used from input methods.
The global keymap binds special event `compose-last-chars' to this
function.  Input method may generate an event (compose-last-chars N COMPONENTS)
after a sequence of character events."
  (interactive "e")
  (let ((chars (nth 1 args)))
    (if (and (numberp chars)
	     (>= (- (point) (point-min)) chars))
	(if (nth 2 args)
	    (compose-region (- (point) chars) (point) (nth 2 args))
	  (compose-chars-after (- (point) chars) (point))))))

(global-set-key [compose-last-chars] 'compose-last-chars)


;;; Automatic character composition.

;; These macros must match with C macros LGSTRING_XXX and LGLYPH_XXX in font.h
(defsubst lgstring-header (gstring) (aref gstring 0))
(defsubst lgstring-set-header (gstring header) (aset gstring 0 header))
(defsubst lgstring-font (gstring) (aref (lgstring-header gstring) 0))
(defsubst lgstring-char (gstring i) (aref (lgstring-header gstring) (1+ i)))
(defsubst lgstring-char-len (gstring) (1- (length (lgstring-header gstring))))
(defsubst lgstring-shaped-p (gstring) (aref gstring 1))
(defsubst lgstring-set-id (gstring id) (aset gstring 1 id))
(defsubst lgstring-glyph (gstring i) (aref gstring (+ i 2)))
(defsubst lgstring-glyph-len (gstring) (- (length gstring) 2))
(defsubst lgstring-set-glyph (gstring i glyph) (aset gstring (+ i 2) glyph))

(defsubst lglyph-from (glyph) (aref glyph 0))
(defsubst lglyph-to (glyph) (aref glyph 1))
(defsubst lglyph-char (glyph) (aref glyph 2))
(defsubst lglyph-code (glyph) (aref glyph 3))
(defsubst lglyph-width (glyph) (aref glyph 4))
(defsubst lglyph-lbearing (glyph) (aref glyph 5))
(defsubst lglyph-rbearing (glyph) (aref glyph 6))
(defsubst lglyph-ascent (glyph) (aref glyph 7))
(defsubst lglyph-descent (glyph) (aref glyph 8))
(defsubst lglyph-adjustment (glyph) (aref glyph 9))

(defsubst lglyph-set-from-to (glyph from to)
  (progn (aset glyph 0 from) (aset glyph 1 to)))
(defsubst lglyph-set-char (glyph char) (aset glyph 2 char))
(defsubst lglyph-set-code (glyph code) (aset glyph 3 code))
(defsubst lglyph-set-width (glyph width) (aset glyph 4 width))
(defsubst lglyph-set-adjustment (glyph &optional xoff yoff wadjust)
  (aset glyph 9 (vector (or xoff 0) (or yoff 0) (or wadjust 0))))

;; Return the shallow Copy of GLYPH.
(defsubst lglyph-copy (glyph) (copy-sequence glyph))

;; Insert GLYPH at the index IDX of GSTRING.
(defun lgstring-insert-glyph (gstring idx glyph)
  (let ((nglyphs (lgstring-glyph-len gstring))
	(i idx))
    (while (and (< i nglyphs) (lgstring-glyph gstring i))
      (setq i (1+ i)))
    (if (= i nglyphs)
	(setq gstring (vconcat gstring (vector glyph)))
      (if (< (1+ i) nglyphs)
	  (lgstring-set-glyph gstring (1+ i) nil)))
    (while (> i idx)
      (lgstring-set-glyph gstring i (lgstring-glyph gstring (1- i)))
      (setq i (1- i)))
    (lgstring-set-glyph gstring i glyph)
    gstring))

;; Remove glyph at IDX from GSTRING.
(defun lgstring-remove-glyph (gstring idx)
  (setq gstring (copy-sequence gstring))
  (lgstring-set-id gstring nil)
  (let ((len (length gstring)))
    (setq idx (+ idx 3))
    (while (< idx len)
      (aset gstring (1- idx) (aref gstring idx))
      (setq idx (1+ idx)))
    (aset gstring (1- len) nil))
  gstring)

(defun lgstring-glyph-boundary (gstring startpos endpos)
  "Return buffer position at or after ENDPOS where grapheme from GSTRING ends.
STARTPOS is the position where the grapheme cluster starts; it is returned
by `find-composition'."
  (let ((nglyphs (lgstring-glyph-len gstring))
        (idx 0)
        glyph found)
    (while (and (not found) (< idx nglyphs))
      (setq glyph (lgstring-glyph gstring idx))
      (cond
       ((or (null glyph)
            (= (+ startpos (lglyph-from glyph)) endpos))
        (setq found endpos))
       ((>= (+ startpos (lglyph-to glyph)) endpos)
        (setq found (+ startpos (lglyph-to glyph) 1)))
       (t
        (setq idx (1+ idx)))))
    (or found endpos)))

(defun composition-find-pos-glyph (composition pos)
  "Find in COMPOSITION a glyph that corresponds to character at position POS.
COMPOSITION is as returned by `find-composition'."
  (let* ((from-pos (car composition))
         (to-pos (nth 1 composition))
         (gstring (nth 2 composition))
         (nglyphs (lgstring-glyph-len gstring))
        (idx 0)
        glyph found)
    (if (and (>= pos from-pos) (< pos to-pos))
        (while (and (not found) (< idx nglyphs))
          (setq glyph (lgstring-glyph gstring idx))
          (if (and (>= pos (+ from-pos (lglyph-from glyph)))
                   (<= pos (+ from-pos (lglyph-to glyph))))
              (setq found (lglyph-code glyph)))
          (setq idx (1+ idx))))
    found))

(defun compose-glyph-string (gstring from to)
  (let ((glyph (lgstring-glyph gstring from))
	from-pos to-pos)
    (setq from-pos (lglyph-from glyph)
	  to-pos (lglyph-to (lgstring-glyph gstring (1- to))))
    (lglyph-set-from-to glyph from-pos to-pos)
    (setq from (1+ from))
    (while (and (< from to)
		(setq glyph (lgstring-glyph gstring from)))
      (lglyph-set-from-to glyph from-pos to-pos)
      (let ((xoff (if (<= (lglyph-rbearing glyph) 0) 0
		    (- (lglyph-width glyph)))))
	(lglyph-set-adjustment glyph xoff 0 0))
      (setq from (1+ from)))
    gstring))

(defun compose-glyph-string-relative (gstring from to &optional gap)
  (let ((font-object (lgstring-font gstring))
	(glyph (lgstring-glyph gstring from))
	from-pos to-pos
	ascent descent)
    (if gap
	(setq gap (floor (* (font-get font-object :size) gap)))
      (setq gap 0))
    (setq from-pos (lglyph-from glyph)
	  to-pos (lglyph-to (lgstring-glyph gstring (1- to)))
	  ascent (lglyph-ascent glyph)
	  descent (lglyph-descent glyph))
    (lglyph-set-from-to glyph from-pos to-pos)
    (setq from (1+ from))
    (while (< from to)
      (setq glyph (lgstring-glyph gstring from))
      (lglyph-set-from-to glyph from-pos to-pos)
      (let ((this-ascent (lglyph-ascent glyph))
	    (this-descent (lglyph-descent glyph))
	    xoff yoff)
	(setq xoff (if (<= (lglyph-rbearing glyph) 0) 0
		     (- (lglyph-width glyph))))
	(if (> this-ascent 0)
	    (if (< this-descent 0)
		(setq yoff (- 0 ascent gap this-descent)
		      ascent (+ ascent gap this-ascent this-descent))
	      (setq yoff 0))
	  (setq yoff (+ descent gap this-ascent)
		descent (+ descent gap this-ascent this-descent)))
	(if (or (/= xoff 0) (/= yoff 0))
	    (lglyph-set-adjustment glyph xoff yoff 0)))
      (setq from (1+ from)))
    gstring))

(defun compose-gstring-for-graphic (gstring direction)
  "Compose glyph-string GSTRING under bidi DIRECTION for graphic display.
DIRECTION is either L2R or R2L, or nil if unknown.
Combining characters are composed with the preceding base
character.  If the preceding character is not a base character,
each combining character is composed as a spacing character by
a padding space before and/or after the character.

All non-spacing characters have this function in
`composition-function-table' unless overwritten."
  (let ((nchars (lgstring-char-len gstring))
        (nglyphs (lgstring-glyph-len gstring))
        (glyph (lgstring-glyph gstring 0)))
    (cond
     ;; A non-spacing character not following a proper base character.
     ((= nchars 1)
      (let ((lbearing (lglyph-lbearing glyph))
	    (rbearing (lglyph-rbearing glyph))
	    (width (lglyph-width glyph))
	    xoff)
	(if (< lbearing 0)
	    (setq xoff (- lbearing))
	  (setq xoff 0 lbearing 0))
	(if (< rbearing width)
	    (setq rbearing width))
	(lglyph-set-adjustment glyph xoff 0 (- rbearing lbearing))
	gstring))

     ;; This sequence doesn't start with a proper base character.
     ((memq (get-char-code-property (lgstring-char gstring 0)
				    'general-category)
            ;; "Improper" base characters are of the following general
            ;; categories:
            ;;   Mark (nonspacing, combining, enclosing)
            ;;   Separator (line, paragraph)
            ;;   Other (control, format, surrogate)
	    '(Mn Mc Me Zl Zp Cc Cf Cs))
      nil)

     ;; A base character and the following non-spacing characters.
     (t
      (let ((gstr (font-shape-gstring gstring direction)))
	(if (and gstr
		 (> (lglyph-to (lgstring-glyph gstr 0)) 0))
	    gstr
	  ;; The shaper of the font couldn't shape the gstring.
	  ;; Shape them according to canonical-combining-class.
	  (lgstring-set-id gstring nil)
	  (let* ((width (lglyph-width glyph))
		 (ascent (lglyph-ascent glyph))
		 (descent (lglyph-descent glyph))
		 (rbearing (lglyph-rbearing glyph))
		 (lbearing (lglyph-lbearing glyph))
		 (center (/ (+ lbearing rbearing) 2))
		 ;; Artificial vertical gap between the glyphs.
		 (gap (round (* (font-get (lgstring-font gstring) :size) 0.1))))
	    (if (= gap 0)
		;; Assure at least 1 pixel vertical gap.
		(setq gap 1))
	    (dotimes (i nchars)
	      (setq glyph (lgstring-glyph gstring i))
	      (when (> i 0)
		(let* ((class (get-char-code-property
			       (lglyph-char glyph) 'canonical-combining-class))
		       (lb (lglyph-lbearing glyph))
		       (rb (lglyph-rbearing glyph))
		       (as (lglyph-ascent glyph))
		       (de (lglyph-descent glyph))
		       (ce (/ (+ lb rb) 2))
		       xoff yoff)
		  (cond
		   ((and class (>= class 200) (<= class 240))
		    (setq xoff 0 yoff 0)
		    (cond
		     ((= class 200)
		      (setq xoff (- lbearing ce)
			    yoff (if (> as 0) 0 (+ descent as))))
		     ((= class 202)
		      (if (> as 0) (setq as 0))
		      (setq xoff (- center ce)
			    yoff (if (> as 0) 0 (+ descent as))))
		     ((= class 204)
		      (if (> as 0) (setq as 0))
		      (setq xoff (- rbearing ce)
			    yoff (if (> as 0) 0 (+ descent as))))
		     ((= class 208)
		      (setq xoff (- lbearing rb)))
		     ((= class 210)
		      (setq xoff (- rbearing lb)))
		     ((= class 212)
		      (setq xoff (- lbearing ce)
			    yoff (if (>= de 0) 0 (- (- ascent) de))))
		     ((= class 214)
		      (setq xoff (- center ce)
			    yoff (if (>= de 0) 0 (- (- ascent) de))))
		     ((= class 216)
		      (setq xoff (- rbearing ce)
			    yoff (if (>= de 0) 0 (- (- ascent) de))))
		     ((= class 218)
		      (setq xoff (- lbearing ce)
			    yoff (if (> as 0) 0 (+ descent as gap))))
		     ((= class 220)
		      (setq xoff (- center ce)
			    yoff (if (> as 0) 0 (+ descent as gap))))
		     ((= class 222)
		      (setq xoff (- rbearing ce)
			    yoff (if (> as 0) 0 (+ descent as gap))))
		     ((= class 224)
		      (setq xoff (- lbearing rb)))
		     ((= class 226)
		      (setq xoff (- rbearing lb)))
		     ((= class 228)
		      (setq xoff (- lbearing ce)
			    yoff (if (>= de 0) 0 (- (- ascent) de gap))))
		     ((= class 230)
		      (setq xoff (- center ce)
			    yoff (if (>= de 0) 0 (- (- ascent) de gap))))
		     ((= class 232)
		      (setq xoff (- rbearing ce)
			    yoff (if (>= de 0) 0 (- (+ ascent de) gap)))))
		    (lglyph-set-adjustment glyph (- xoff width) yoff)
		    (setq lb (+ lb xoff)
			  rb (+ lb xoff)
			  as (- as yoff)
			  de (+ de yoff)))
		   ((and (= class 0)
			 (eq (get-char-code-property (lglyph-char glyph)
                                                     ;; Me = enclosing mark
						     'general-category)
			     'Me))
		    ;; Artificially laying out glyphs in an enclosing
		    ;; mark is difficult.  All we can do is to adjust
		    ;; the x-offset and width of the base glyph to
		    ;; align it at the center of the glyph of the
		    ;; enclosing mark hoping that the enclosing mark
		    ;; is big enough.  We also have to adjust the
		    ;; x-offset and width of the mark itself properly
		    ;; depending on how the glyph is designed.

		    ;; (non-spacing or not).  For instance, when we
		    ;; have these glyphs:
		    ;;   X position  |
		    ;;   base:       <-*-> lbearing=0 rbearing=5 width=5
		    ;;   mark: <----------.> lb=-11 rb=2 w=0
		    ;; we get a correct layout by moving them as this:
		    ;;   base:           <-*-> XOFF=4 WAD=9
		    ;;   mark:       <----------.> xoff=2 wad=4
		    ;; we have moved the base to the left by 4-pixel
		    ;; and make its width 9-pixel, then move the mark
		    ;; to the left 2-pixel and make its width 4-pixel.
		    (let* (;; Adjustment for the base glyph
			   (XOFF (/ (- rb lb width) 2))
			   (WAD (+ width XOFF))
			   ;; Adjustment for the enclosing mark glyph
			   (xoff (- (+ lb WAD)))
			   (wad (- rb lb WAD)))
		      (lglyph-set-adjustment glyph xoff 0 wad)
		      (setq glyph (lgstring-glyph gstring 0))
		      (lglyph-set-adjustment glyph XOFF 0 WAD))))
		  (if (< ascent as)
		      (setq ascent as))
		  (if (< descent de)
		      (setq descent de))))))
	  (let ((i 0))
	    (while (and (< i nglyphs) (setq glyph (lgstring-glyph gstring i)))
	      (lglyph-set-from-to glyph 0 (1- nchars))
	      (setq i (1+ i))))
	  gstring))))))

(defun compose-gstring-for-dotted-circle (gstring direction)
  (let* ((dc (lgstring-glyph gstring 0)) ; glyph of dotted-circle
	 (fc (lgstring-glyph gstring 1)) ; glyph of the following char
	 (gstr (and nil (font-shape-gstring gstring direction))))
    (if (and gstr
	     (or (= (lgstring-glyph-len gstr) 1)
		 (and (= (lgstring-glyph-len gstr) 2)
		      (= (lglyph-to (lgstring-glyph gstr 0))
			 (lglyph-to (lgstring-glyph gstr 1))))))
	;; It seems that font-shape-gstring has composed glyphs.
	gstr
      ;; Artificially compose the following glyph with the preceding
      ;; dotted-circle.
      (setq dc (lgstring-glyph gstring 0)
	    fc (lgstring-glyph gstring 1))
      (let ((dc-width (lglyph-width dc))
	    (fc-width (- (lglyph-rbearing fc) (lglyph-lbearing fc)))
	    (from (lglyph-from dc))
	    (to (lglyph-to fc))
	    (xoff 0) (yoff 0) (width 0))
	(if (and (< (lglyph-descent fc) 0)
		 (> (lglyph-ascent dc) (- (lglyph-descent fc))))
	    ;; Set YOFF so that the following glyph is put on top of
	    ;; the dotted-circle.
	    (setq yoff (- (- (lglyph-descent fc)) (lglyph-ascent dc))))
	(if (> (lglyph-width fc) 0)
	    (setq xoff (- (lglyph-rbearing fc))))
	(if (< dc-width fc-width)
	    ;; The following glyph is wider, but we don't know how to
	    ;; align both glyphs.  So, try the easiest method;
	    ;; i.e. align left edges of the glyphs.
	    (setq xoff (- xoff (- dc-width) (- (lglyph-lbearing fc )))
		  width (- fc-width dc-width)))
	(if (or (/= xoff 0) (/= yoff 0) (/= width 0) (/= (lglyph-width fc) 0))
	    (lglyph-set-adjustment fc xoff yoff width))
	(lglyph-set-from-to dc from to)
	(lglyph-set-from-to fc from to))
      (if (> (lgstring-glyph-len gstring) 2)
	  (lgstring-set-glyph gstring 2 nil))
      gstring)))

;; Allow for bootstrapping without uni-*.el.
(when unicode-category-table
  (let ((elt `(["\\c.\\c^+" 1 compose-gstring-for-graphic]
	       [nil 0 compose-gstring-for-graphic])))
    (map-char-table
     #'(lambda (key val)
	 (if (memq val '(Mn Mc Me))
	     (set-char-table-range composition-function-table key elt)))
     unicode-category-table))
  ;; for dotted-circle
  (aset composition-function-table #x25CC
        `([".\\c^" 0 compose-gstring-for-dotted-circle]))
  ;; For prettier display of fractions
  (set-char-table-range
   composition-function-table
   #x2044
   ;; We use font-shape-gstring so that if the font doesn't support
   ;; fractional display, the characters are shown separately, not as
   ;; a composed cluster.
   (list (vector "[1-9][0-9][0-9]\u2044[0-9]+"
                 3 'font-shape-gstring)
         (vector "[1-9][0-9]\u2044[0-9]+" 2 'font-shape-gstring)
         (vector "[1-9]\u2044[0-9]+" 1 'font-shape-gstring))))

(defun compose-gstring-for-terminal (gstring _direction)
  "Compose glyph-string GSTRING for terminal display.
Non-spacing characters are composed with the preceding base
character.  If the preceding character is not a base character,
each non-spacing character is composed as a spacing character by
prepending a space before it."
  (let ((nglyphs (lgstring-glyph-len gstring))
        (i 0)
        (coding (lgstring-font gstring))
        glyph)
    (while (and (< i nglyphs)
		(setq glyph (lgstring-glyph gstring i)))
      (if (not (char-charset (lglyph-char glyph) coding))
	  (progn
	    ;; As the terminal doesn't support this glyph, return a
	    ;; gstring in which each glyph is its own grapheme-cluster
	    ;; of width 1..
	    (setq i 0)
	    (while (and (< i nglyphs)
			(setq glyph (lgstring-glyph gstring i)))
	      (if (< (lglyph-width glyph) 1)
		  (lglyph-set-width glyph 1))
	      (lglyph-set-from-to glyph i i)
	      (setq i (1+ i))))
	(if (= (lglyph-width glyph) 0)
	    (if (eq (get-char-code-property (lglyph-char glyph)
					    'general-category)
		    'Cf)
		(progn
		  ;; Compose Cf (format) control characters by
		  ;; replacing with a space.
		  (lglyph-set-char glyph 32)
		  (lglyph-set-width glyph 1)
		  (setq i (1+ i)))
	      ;; Compose by prepending a space.
	      (setq gstring (lgstring-insert-glyph gstring i
						   (lglyph-copy glyph))
		    nglyphs (lgstring-glyph-len gstring))
	      (setq glyph (lgstring-glyph gstring i))
	      (lglyph-set-char glyph 32)
	      (lglyph-set-width glyph 1)
	      (setq i (+ i 2)))
	  (let ((from (lglyph-from glyph))
		(to (lglyph-to glyph))
		(j (1+ i)))
	    (while (and (< j nglyphs)
			(setq glyph (lgstring-glyph gstring j))
			(char-charset (lglyph-char glyph) coding)
			(= (lglyph-width glyph) 0))
	      (setq to (lglyph-to glyph)
		    j (1+ j)))
	    (while (< i j)
	      (setq glyph (lgstring-glyph gstring i))
	      (lglyph-set-from-to glyph from to)
	      (setq i (1+ i)))))))
    gstring))

(defun compose-gstring-for-variation-glyph (gstring _direction)
  "Compose glyph-string GSTRING for graphic display.
GSTRING must have two glyphs; the first is a glyph for a han character,
and the second is a glyph for a variation selector."
  (let* ((font (lgstring-font gstring))
	 (han (lgstring-char gstring 0))
	 (vs (lgstring-char gstring 1))
	 (glyphs (font-variation-glyphs font han))
	 (g0 (lgstring-glyph gstring 0))
	 (g1 (lgstring-glyph gstring 1)))
    (catch 'tag
      (dolist (elt glyphs)
	(if (= (car elt) vs)
	    (progn
	      (lglyph-set-code g0 (cdr elt))
	      (lglyph-set-from-to g0 (lglyph-from g0) (lglyph-to g1))
	      (lgstring-set-glyph gstring 1 nil)
	      (throw 'tag gstring)))))))

;; We explicitly don't handle #xFE0F (VS-16) here, because that's
;; taken care of by font_range in font.c, which will check for an
;; emoji font for codepoints used in compositions even if they're not
;; emoji themselves, and thus choose the Emoji presentation for them
;; when followed by VS-16.  VS-15 *is* handled here, because if it's
;; handled in font_range, we end up choosing the Emoji presentation
;; rather than the Text presentation.
(let ((elt '([".." 1 compose-gstring-for-variation-glyph])))
  (set-char-table-range composition-function-table '(#xFE00 . #xFE0D) elt)
  (set-char-table-range composition-function-table '(#xE0100 . #xE01EF) elt))

(defun auto-compose-chars (func from to font-object string direction)
  "Compose the characters at FROM by FUNC.
FUNC is called with two arguments: GSTRING, which is built for
characters in the region FROM (inclusive) and TO (exclusive);
and DIRECTION, which is the bidi directionality of the characters.

If the character are composed on a graphic display, FONT-OBJECT
is a font to use.  Otherwise, FONT-OBJECT is nil, and the function
`compose-gstring-for-terminal' is used instead of FUNC.

If STRING is non-nil, it is a string, and FROM and TO are indices
into the string.  In that case, compose characters in the string.

The value is a gstring containing information for shaping the characters.

This function is the default value of `auto-composition-function' (which see)."
  (let ((gstring (composition-get-gstring from to font-object string)))
    (if (lgstring-shaped-p gstring)
	gstring
      (or (fontp font-object 'font-object)
	  (setq func 'compose-gstring-for-terminal))
      (funcall func gstring direction))))

(put 'auto-composition-mode 'permanent-local t)

(make-variable-buffer-local 'auto-composition-function)
(setq-default auto-composition-function 'auto-compose-chars)

;;;###autoload
(define-minor-mode auto-composition-mode
  "Toggle Auto Composition mode.

When Auto Composition mode is enabled, text characters are
automatically composed by functions registered in
`composition-function-table'.

You can use `global-auto-composition-mode' to turn on
Auto Composition mode in all buffers (this is the default)."
  ;; It's defined in C, this stops the d-m-m macro defining it again.
  :variable auto-composition-mode)
;; It's not defined with DEFVAR_PER_BUFFER though.
(make-variable-buffer-local 'auto-composition-mode)

;;;###autoload
(define-minor-mode global-auto-composition-mode
  "Toggle Auto Composition mode in all buffers.

For more information on Auto Composition mode, see
`auto-composition-mode'."
  :global t
  :variable (default-value 'auto-composition-mode))

(defalias 'toggle-auto-composition 'auto-composition-mode)

(provide 'composite)

;;; composite.el ends here
