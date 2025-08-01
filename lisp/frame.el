;;; frame.el --- multi-frame management independent of window systems  -*- lexical-binding:t -*-

;; Copyright (C) 1993-1994, 1996-1997, 2000-2025 Free Software
;; Foundation, Inc.

;; Maintainer: emacs-devel@gnu.org
;; Keywords: internal
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
(eval-when-compile (require 'cl-lib))

(cl-defgeneric frame-creation-function (params)
  "Method for window-system dependent functions to create a new frame.
The window system startup file should add its frame creation
function to this method, which should take an alist of parameters
as its argument.")

(cl-generic-define-context-rewriter window-system (value)
  ;; If `value' is a `consp', it's probably an old-style specializer,
  ;; so just use it, and anyway `eql' isn't very useful on cons cells.
  `(window-system ,(if (consp value) value `(eql ',value))))

(cl-defmethod frame-creation-function (params &context (window-system nil))
  ;; It's tempting to get rid of tty-create-frame-with-faces and turn it into
  ;; this method (i.e. move this method to faces.el), but faces.el is loaded
  ;; much earlier from loadup.el (before cl-generic and even before
  ;; cl-preloaded), so we'd first have to reorder that part.
  (tty-create-frame-with-faces params))

(defvar window-system-default-frame-alist nil
  "Window-system dependent default frame parameters.
The value should be an alist of elements (WINDOW-SYSTEM . ALIST),
where WINDOW-SYSTEM is a window system symbol (as returned by `framep')
and ALIST is a frame parameter alist like `default-frame-alist'.
Then, for frames on WINDOW-SYSTEM, any parameters specified in
ALIST supersede the corresponding parameters specified in
`default-frame-alist'.")

(defvar display-format-alist nil
  "Alist of patterns to decode display names.
The car of each entry is a regular expression matching a display
name string.  The cdr is a symbol giving the window-system that
handles the corresponding kind of display.")

;; If you're adding a new frame parameter to `frame_parms' in frame.c,
;; consider if it makes sense for the user to customize it via
;; `initial-frame-alist' and the like.
;; If it does, add it here, in order to provide completion for
;; that parameter in the Customize UI.
;; If the parameter has some special values, modify
;; `frame--complete-parameter-value' to provide completion for those
;; values as well.
(defconst frame--special-parameters
  '("alpha" "alpha-background" "auto-hide-function" "auto-lower"
    "auto-raise" "background-color" "background-mode" "border-color"
    "border-width" "bottom-divider-width" "bottom-visible" "buffer-list"
    "buffer-predicate" "child-frame-border-width" "cursor-color"
    "cursor-type" "delete-before" "display" "display-type"
    "drag-internal-border" "drag-with-header-line" "drag-with-mode-line"
    "drag-with-tab-line" "explicit-name" "fit-frame-to-buffer-margins"
    "fit-frame-to-buffer-sizes" "font" "font-backend" "foreground-color"
    "fullscreen" "fullscreen-restore" "height" "horizontal-scroll-bars"
    "icon-left" "icon-name" "icon-top" "icon-type"
    "inhibit-double-buffering" "internal-border-width" "keep-ratio"
    "left" "left-fringe" "line-spacing" "menu-bar-lines" "min-height"
    "min-width" "minibuffer" "minibuffer-exit" "mouse-color"
    "mouse-wheel-frame" "name" "no-accept-focus" "no-focus-on-map"
    "no-other-frame" "no-special-glyphs" "ns-appearance"
    "ns-transparent-titlebar" "outer-window-id" "override-redirect"
    "parent-frame" "right-fringe" "rigth-divider-width" "screen-gamma"
    "scroll-bar-background" "scroll-bar-foreground" "scroll-bar-height"
    "scroll-bar-width" "shaded" "skip-taskbar" "snap-width" "sticky"
    "tab-bar-lines" "title" "tool-bar-lines" "tool-bar-position" "top"
    "top-visible" "tty-color-mode" "undecorated" "unspittable"
    "use-frame-synchronization" "user-position" "user-size"
    "vertical-scroll-bars" "visibility" "wait-for-wm" "width" "z-group")
  "List of special frame parameters that makes sense to customize.")

(declare-function widget-field-text-end "wid-edit")
(declare-function widget-field-start "wid-edit")
(declare-function widget-get "wid-edit")

(defun frame--complete-parameter-value (widget)
  "Provide completion for WIDGET, which holds frame parameter's values."
  (let* ((parameter (widget-value
                     (nth 0
                          (widget-get (widget-get widget :parent) :children))))
         (comps (cond ((eq parameter 'display-type)
                       '("color" "grayscale" "mono"))
                      ((eq parameter 'z-group) '("nil" "above" "below"))
                      ((memq parameter '(fullscreen fullscreen-restore))
                       '("fullwidth" "fullheight" "fullboth" "maximized"))
                      ((eq parameter 'cursor-type)
                       '("t" "nil" "box" "hollow" "bar" "hbar"))
                      ((eq parameter 'vertical-scroll-bars)
                       '("nil" "left" "right"))
                      ((eq parameter 'tool-bar-position)
                       '("top" "bottom" "left" "right"))
                      ((eq parameter 'minibuffer)
                       '("t" "nil" "only"))
                      ((eq parameter 'minibuffer-exit)
                       '("nil" "t" "iconify-frame" "delete-frame"))
                      ((eq parameter 'visibility) '("nil" "t" "icon"))
                      ((memq parameter '(ns-appearance background-mode))
                       '("dark" "light"))
                      ((eq parameter 'font-backend)
                       '("x" "xft" "xfthb" "ftcr" "ftcrhb" "gdi"
                         "uniscribe" "harfbuzz"))
                      ((memq parameter '(buffer-predicate auto-hide-function))
                       (apply-partially
                        #'completion-table-with-predicate
                        obarray #'fboundp 'strict))
                      (t nil))))
    (completion-in-region (widget-field-start widget)
                          (max (point) (widget-field-text-end widget))
                          comps)))

;; The initial value given here used to ask for a minibuffer.
;; But that's not necessary, because the default is to have one.
;; By not specifying it here, we let an X resource specify it.
(defcustom initial-frame-alist nil
  "Alist of parameters for the initial window-system (a.k.a. \"GUI\") frame.
You can set this in your init file; for example,

 (setq initial-frame-alist
       \\='((top . 1) (left . 1) (width . 80) (height . 55)))

Parameters specified here supersede the values given in
`default-frame-alist'.

If the value calls for a frame without a minibuffer, and you have
not created a minibuffer frame on your own, a minibuffer frame is
created according to `minibuffer-frame-alist'.

Emacs reads your main init file after creating the initial frame,
so setting it there won't have the expected effect.  Instead, you
can set it in `early-init-file'.

If you're using X, and you want (for instance) to have different
geometries on different displays, you need to use this three-step
process:

* Specify X resources to give the geometry you want.
* Set `default-frame-alist' to override these options so that they
  don't affect subsequent frames.
* Set `initial-frame-alist' in your normal init file in a way
  that matches the X resources, to override what you put in
  `default-frame-alist'."
  :type `(repeat (cons :format "%v"
                       (symbol :tag "Parameter"
                               :completions ,frame--special-parameters)
                       (sexp :tag "Value"
                             :complete frame--complete-parameter-value)))
  :group 'frames)

(defcustom minibuffer-frame-alist '((width . 80) (height . 2))
  "Alist of parameters for the initial minibuffer frame.
This is the minibuffer frame created if `initial-frame-alist'
calls for a frame without a minibuffer.  The parameters specified
here supersede those given in `default-frame-alist', for the
initial minibuffer frame.

You can set this in your init file; for example,

 (setq minibuffer-frame-alist
       \\='((top . 1) (left . 1) (width . 80) (height . 2)))

It is not necessary to include (minibuffer . only); that is
appended when the minibuffer frame is created."
  :type `(repeat (cons :format "%v"
                       (symbol :tag "Parameter"
                               :completions ,frame--special-parameters)
                       (sexp :tag "Value"
                             :complete frame--complete-parameter-value)))
  :group 'frames)

(defun frame-deletable-p (&optional frame)
  "Return non-nil if specified FRAME can be safely deleted.
FRAME must be a live frame and defaults to the selected frame.

FRAME cannot be safely deleted in the following cases:

- FRAME is the only visible or iconified frame.

- FRAME hosts the active minibuffer window that does not follow the
  selected frame.

- All other visible or iconified frames are either child frames or have
  a non-nil `delete-before' parameter.

- FRAME or one of its descendants hosts the minibuffer window of a frame
  that is not a descendant of FRAME.

This covers most cases where `delete-frame' might fail when called from
top-level.  It does not catch some special cases like, for example,
deleting a frame during a drag-and-drop operation.  In any such case, it
will be better to wrap the `delete-frame' call in a `condition-case'
form."
  (setq frame (window-normalize-frame frame))
  (let ((active-minibuffer-window (active-minibuffer-window))
	deletable)
    (catch 'deletable
      (when (and active-minibuffer-window
		 (eq (window-frame active-minibuffer-window) frame)
		 (not (eq (default-toplevel-value
			   'minibuffer-follows-selected-frame)
			  t)))
	(setq deletable nil)
	(throw 'deletable nil))

      (let ((frames (delq frame (frame-list))))
	(dolist (other frames)
	  ;; A suitable "other" frame must be either visible or
	  ;; iconified.  Child frames and frames with a non-nil
	  ;; 'delete-before' parameter do not qualify as other frame -
	  ;; either of these will depend on a "suitable" frame found in
	  ;; this loop.
	  (unless (or (frame-parent other)
		      (frame-parameter other 'delete-before)
		      (not (frame-visible-p other)))
	    (setq deletable t))

	  ;; Some frame not descending from FRAME may use the minibuffer
	  ;; window of FRAME or the minibuffer window of a frame
	  ;; descending from FRAME.
	  (when (let* ((minibuffer-window (minibuffer-window other))
		       (minibuffer-frame
			(and minibuffer-window
			     (window-frame minibuffer-window))))
		  (and minibuffer-frame
		       ;; If the other frame is a descendant of
		       ;; FRAME, it will be deleted together with
		       ;; FRAME ...
		       (not (frame-ancestor-p frame other))
		       ;; ... but otherwise the other frame must
		       ;; neither use FRAME nor any descendant of
		       ;; it as minibuffer frame.
		       (or (eq minibuffer-frame frame)
			   (frame-ancestor-p frame minibuffer-frame))))
	    (setq deletable nil)
	    (throw 'deletable nil))))

      deletable)))

(defun handle-delete-frame (event)
  "Handle delete-frame events from the X server."
  (interactive "e")
  (let* ((frame (posn-window (event-start event))))
    (if (catch 'other-frame
          (dolist (frame-1 (frame-list))
            ;; A valid "other" frame is visible, has its `delete-before'
            ;; parameter unset and is not a child frame.
            (when (and (not (eq frame-1 frame))
                       (frame-visible-p frame-1)
                       (not (frame-parent frame-1))
                       (not (frame-parameter frame-1 'delete-before)))
              (throw 'other-frame t))))
	(delete-frame frame t)
      ;; Gildea@x.org says it is ok to ask questions before terminating.
      (save-buffers-kill-emacs))))

(defun frame-focus-state (&optional frame)
  "Return FRAME's last known focus state.
If nil or omitted, FRAME defaults to the selected frame.

Return nil if the frame is definitely known not be focused, t if
the frame is known to be focused, and `unknown' if we don't know."
  (let* ((frame (or frame (selected-frame)))
         (tty-top-frame (tty-top-frame frame)))
    (if (not tty-top-frame)
        (frame-parameter frame 'last-focus-update)
      ;; All tty frames are frame-visible-p if the terminal is
      ;; visible, so check whether the frame is the top tty frame
      ;; before checking visibility.
      (cond ((not (eq tty-top-frame frame)) nil)
            ((not (frame-visible-p frame)) nil)
            (t (let ((tty-focus-state
                      (terminal-parameter frame 'tty-focus-state)))
                 (cond ((eq tty-focus-state 'focused) t)
                       ((eq tty-focus-state 'defocused) nil)
                       (t 'unknown))))))))

(defvar after-focus-change-function #'ignore
  "Function called after frame focus may have changed.

This function is called with no arguments when Emacs notices that
the set of focused frames may have changed.  Code wanting to do
something when frame focus changes should use `add-function' to
add a function to this one, and in this added function, re-scan
the set of focused frames, calling `frame-focus-state' to
retrieve the last known focus state of each frame.  Focus events
are delivered asynchronously, and frame input focus according to
an external system may not correspond to the notion of the Emacs
selected frame.  Multiple frames may appear to have input focus
simultaneously due to focus event delivery differences, the
presence of multiple Emacs terminals, and other factors, and code
should be robust in the face of this situation.

Depending on window system, focus events may also be delivered
repeatedly and with different focus states before settling to the
expected values.  Code relying on focus notifications should
\"debounce\" any user-visible updates arising from focus changes,
perhaps by deferring work until redisplay.

This function may be called in arbitrary contexts, including from
inside `read-event', so take the same care as you might when
writing a process filter.")

(defvar focus-in-hook nil
  "Normal hook run when a frame gains focus.
The frame gaining focus is selected at the time this hook is run.

This hook is obsolete.  Despite its name, this hook may be run in
situations other than when a frame obtains input focus: for
example, we also run this hook when switching the selected frame
internally to handle certain input events (like mouse wheel
scrolling) even when the user's notion of input focus
hasn't changed.

Prefer using `after-focus-change-function'.")
(make-obsolete-variable
 'focus-in-hook "after-focus-change-function" "27.1" 'set)

(defvar focus-out-hook nil
  "Normal hook run when all frames lost input focus.

This hook is obsolete; see `focus-in-hook'.  Depending on timing,
this hook may be delivered when a frame does in fact have focus.
Prefer `after-focus-change-function'.")
(make-obsolete-variable
 'focus-out-hook "after-focus-change-function" "27.1" 'set)

(defun handle-focus-in (event)
  "Handle a focus-in event.
Focus-in events are bound to this function; do not change this
binding.  Focus-in events occur when a frame receives focus from
the window system."
  ;; N.B. tty focus goes down a different path; see xterm.el.
  (interactive "e")
  (unless (eq (car-safe event) 'focus-in)
    (error "handle-focus-in should handle focus-in events"))
  (let ((frame (nth 1 event)))
    (when (frame-live-p frame)
      (internal-handle-focus-in event)
      (setf (frame-parameter frame 'last-focus-update) t)
      (run-hooks 'focus-in-hook)))
  (funcall after-focus-change-function))

(defun handle-focus-out (event)
  "Handle a focus-out event.
Focus-out events are bound to this function; do not change this
binding.  Focus-out events occur when a frame loses focus, but
that's not the whole story: see `after-focus-change-function'."
  ;; N.B. tty focus goes down a different path; see xterm.el.
  (interactive "e")
  (unless (eq (car event) 'focus-out)
    (error "handle-focus-out should handle focus-out events"))
  (let ((frame (nth 1 event)))
    (when (frame-live-p frame)
      (setf (frame-parameter frame 'last-focus-update) nil)
      (run-hooks 'focus-out-hook)))
  (funcall after-focus-change-function))

(defun handle-move-frame (event)
  "Handle a move-frame event.
This function runs the abnormal hook `move-frame-functions'."
  (interactive "e")
  (let ((frame (posn-window (event-start event))))
    (when (frame-live-p frame) ;Experience shows it can die in the meantime.
      (run-hook-with-args 'move-frame-functions frame))))

;;;; Arrangement of frames at startup

;; 1) Load the window system startup file from the lisp library and read the
;; high-priority arguments (-q and the like).  The window system startup
;; file should create any frames specified in the window system defaults.
;;
;; 2) If no frames have been opened, we open an initial text frame.
;;
;; 3) Once the init file is done, we apply any newly set parameters
;; in initial-frame-alist to the frame.

;; If we create the initial frame, this is it.
(defvar frame-initial-frame nil)

;; Record the parameters used in frame-initialize to make the initial frame.
(defvar frame-initial-frame-alist)

(defvar frame-initial-geometry-arguments nil)

;; startup.el calls this function before loading the user's init
;; file - if there is no frame with a minibuffer open now, create
;; one to display messages while loading the init file.
(defun frame-initialize ()
  "Create an initial frame if necessary."
  ;; Are we actually running under a window system at all?
  (if (and initial-window-system
	   (not noninteractive)
	   (not (eq initial-window-system 'pc)))
      (progn
	;; If there is no frame with a minibuffer besides the terminal
	;; frame, then we need to create the opening frame.  Make sure
	;; it has a minibuffer, but let initial-frame-alist omit the
	;; minibuffer spec.
	(or (delq terminal-frame (minibuffer-frame-list))
	    (progn
	      (setq frame-initial-frame-alist
		    (append initial-frame-alist default-frame-alist nil))
	      (setq frame-initial-frame-alist
		    (cons (cons 'window-system initial-window-system)
			  frame-initial-frame-alist))
	      (setq default-minibuffer-frame
		    (setq frame-initial-frame
			  (make-frame frame-initial-frame-alist)))
	      ;; Delete any specifications for window geometry parameters
	      ;; so that we won't reapply them in frame-notice-user-settings.
	      ;; It would be wrong to reapply them then,
	      ;; because that would override explicit user resizing.
	      (setq initial-frame-alist
		    (frame-remove-geometry-params initial-frame-alist))))
	;; Copy the environment of the Emacs process into the new frame.
	(set-frame-parameter frame-initial-frame 'environment
			     (frame-parameter terminal-frame 'environment))
	;; At this point, we know that we have a frame open, so we
	;; can delete the terminal frame.
	(delete-frame terminal-frame)
	(setq terminal-frame nil))))

(defvar frame-notice-user-settings t
  "Non-nil means function `frame-notice-user-settings' wasn't run yet.")

(declare-function tool-bar-mode "tool-bar" (&optional arg))
(declare-function tool-bar-height "xdisp.c" (&optional frame pixelwise))

(defalias 'tool-bar-lines-needed #'tool-bar-height)

;; startup.el calls this function after loading the user's init
;; file.  Now default-frame-alist and initial-frame-alist contain
;; information to which we must react; do what needs to be done.
(defun frame-notice-user-settings ()
  "Act on user's init file settings of frame parameters.
React to settings of `initial-frame-alist',
`window-system-default-frame-alist' and `default-frame-alist'
there (in decreasing order of priority)."
  ;; Creating and deleting frames may shift the selected frame around,
  ;; and thus the current buffer.  Protect against that.  We don't
  ;; want to use save-excursion here, because that may also try to set
  ;; the buffer of the selected window, which fails when the selected
  ;; window is the minibuffer.
  (let* ((old-buffer (current-buffer))
	 (window-system-frame-alist
          (cdr (assq initial-window-system
                     window-system-default-frame-alist)))
         (minibuffer
          (cdr (or (assq 'minibuffer initial-frame-alist)
		   (assq 'minibuffer window-system-frame-alist)
		   (assq 'minibuffer default-frame-alist)
		   '(minibuffer . t)))))

    (when (and frame-notice-user-settings
	       (null frame-initial-frame))
      ;; This case happens when we don't have a window system, and
      ;; also for MS-DOS frames.
      (let ((parms (frame-parameters)))
	;; Don't change the frame names.
	(setq parms (delq (assq 'name parms) parms))
	;; Can't modify the minibuffer parameter, so don't try.
	(setq parms (delq (assq 'minibuffer parms) parms))
	(modify-frame-parameters
	 nil
	 (if initial-window-system
	     parms
	   ;; initial-frame-alist and default-frame-alist were already
	   ;; applied in pc-win.el.
	   (setq parms (append initial-frame-alist window-system-frame-alist
			       default-frame-alist parms nil))
	   ;; Don't enable tab-bar in daemon's initial frame.
	   (when (and (daemonp) (not (frame-parameter nil 'client)))
	     (setq parms (delq (assq 'tab-bar-lines parms) parms)))
	   parms))
	(if (null initial-window-system) ;; MS-DOS does this differently in pc-win.el
	    (let ((newparms (frame-parameters))
		  (frame (selected-frame)))
	      (tty-handle-reverse-video frame newparms)
	      ;; tty-handle-reverse-video might change the frame's
	      ;; color parameters, and we need to use the updated
	      ;; value below.
	      (setq newparms (frame-parameters))
	      ;; If we changed the background color, we need to update
	      ;; the background-mode parameter, and maybe some faces,
	      ;; too.
	      (when (assq 'background-color newparms)
		(unless (or (assq 'background-mode initial-frame-alist)
			    (assq 'background-mode default-frame-alist))
		  (frame-set-background-mode frame))
		(face-set-after-frame-default frame newparms))))))

    ;; If the initial frame is still around, apply initial-frame-alist
    ;; and default-frame-alist to it.
    (when (frame-live-p frame-initial-frame)
      ;; When tab-bar has been switched off, correct the frame size
      ;; by the lines added in x-create-frame for the tab-bar and
      ;; switch `tab-bar-mode' off.
      (when (display-graphic-p)
        (declare-function tab-bar-height "xdisp.c" (&optional frame pixelwise))
	(let* ((init-lines
		(assq 'tab-bar-lines initial-frame-alist))
	       (other-lines
		(or (assq 'tab-bar-lines window-system-frame-alist)
		    (assq 'tab-bar-lines default-frame-alist)))
	       (lines (or init-lines other-lines))
	       (height (tab-bar-height frame-initial-frame t)))
	  ;; Adjust frame top if either zero (nil) tab bar lines have
	  ;; been requested in the most relevant of the frame's alists
	  ;; or tab bar mode has been explicitly turned off in the
	  ;; user's init file.
	  (when (and (> height 0)
		     (or (and lines
			      (or (null (cdr lines))
				  (eq 0 (cdr lines))))
			 (not tab-bar-mode)))
	    (let* ((initial-top
		    (cdr (assq 'top frame-initial-geometry-arguments)))
		   (top (frame-parameter frame-initial-frame 'top)))
	      (when (and (consp initial-top) (eq '- (car initial-top)))
		(let ((adjusted-top
		       (cond
			((and (consp top) (eq '+ (car top)))
			 (list '+ (+ (cadr top) height)))
			((and (consp top) (eq '- (car top)))
			 (list '- (- (cadr top) height)))
			(t (+ top height)))))
		  (modify-frame-parameters
		   frame-initial-frame `((top . ,adjusted-top))))))
	    ;; Reset `tab-bar-mode' when zero tab bar lines have been
	    ;; requested for the window-system or default frame alists.
	    (when (and tab-bar-mode
		       (and other-lines
			    (or (null (cdr other-lines))
				(eq 0 (cdr other-lines)))))
	      (tab-bar-mode -1)))))

      ;; When tool-bar has been switched off, correct the frame size
      ;; by the lines added in x-create-frame for the tool-bar and
      ;; switch `tool-bar-mode' off.
      (when (display-graphic-p)
	(let* ((init-lines
		(assq 'tool-bar-lines initial-frame-alist))
	       (other-lines
		(or (assq 'tool-bar-lines window-system-frame-alist)
		    (assq 'tool-bar-lines default-frame-alist)))
	       (lines (or init-lines other-lines))
	       (height (tool-bar-height frame-initial-frame t)))
	  ;; Adjust frame top if either zero (nil) tool bar lines have
	  ;; been requested in the most relevant of the frame's alists
	  ;; or tool bar mode has been explicitly turned off in the
	  ;; user's init file.
	  (when (and (> height 0)
		     (or (and lines
			      (or (null (cdr lines))
				  (eq 0 (cdr lines))))
			 (not tool-bar-mode)))
	    (let* ((initial-top
		    (cdr (assq 'top frame-initial-geometry-arguments)))
		   (top (frame-parameter frame-initial-frame 'top)))
	      (when (and (consp initial-top) (eq '- (car initial-top)))
		(let ((adjusted-top
		       (cond
			((and (consp top) (eq '+ (car top)))
			 (list '+ (+ (cadr top) height)))
			((and (consp top) (eq '- (car top)))
			 (list '- (- (cadr top) height)))
			(t (+ top height)))))
		  (modify-frame-parameters
		   frame-initial-frame `((top . ,adjusted-top))))))
	    ;; Reset `tool-bar-mode' when zero tool bar lines have been
	    ;; requested for the window-system or default frame alists.
	    (when (and tool-bar-mode
		       (and other-lines
			    (or (null (cdr other-lines))
				(eq 0 (cdr other-lines)))))
	      (tool-bar-mode -1)))))

      ;; The initial frame we create above always has a minibuffer.
      ;; If the user wants to remove it, or make it a minibuffer-only
      ;; frame, then we'll have to delete the current frame and make a
      ;; new one; you can't remove or add a root window to/from an
      ;; existing frame.
      ;;
      ;; NOTE: default-frame-alist was nil when we created the
      ;; existing frame.  We need to explicitly include
      ;; default-frame-alist in the parameters of the screen we
      ;; create here, so that its new value, gleaned from the user's
      ;; init file, will be applied to the existing screen.
      (if (not (eq minibuffer t))
	  ;; Create the new frame.
	  (let (parms new)
	    ;; MS-Windows needs this to avoid inflooping below.
	    (if (eq system-type 'windows-nt)
		(sit-for 0 t))
	    ;; If the frame isn't visible yet, wait till it is.
	    ;; If the user has to position the window,
	    ;; Emacs doesn't know its real position until
	    ;; the frame is seen to be visible.
	    (while (not (cdr (assq 'visibility
				   (frame-parameters frame-initial-frame))))
	      (sleep-for 1))
	    (setq parms (frame-parameters frame-initial-frame))

            ;; Get rid of `name' unless it was specified explicitly before.
	    (or (assq 'name frame-initial-frame-alist)
		(setq parms (delq (assq 'name parms) parms)))
	    ;; An explicit parent-id is a request to XEmbed the frame.
	    (or (assq 'parent-id frame-initial-frame-alist)
                (setq parms (delq (assq 'parent-id parms) parms)))

	    (setq parms (append initial-frame-alist
				window-system-frame-alist
				default-frame-alist
				parms
				nil))

	    (when (eq minibuffer 'child-frame)
              ;; When the minibuffer shall be shown in a child frame,
              ;; remove the 'minibuffer' parameter from PARMS.  It
              ;; will get assigned by the usual routines to the child
              ;; frame's root window below.
              (setq parms (cons '(minibuffer)
				(delq (assq 'minibuffer parms) parms))))

            ;; Get rid of `reverse', because that was handled
	    ;; when we first made the frame.
	    (setq parms (cons '(reverse) (delq (assq 'reverse parms) parms)))

	    (if (assq 'height frame-initial-geometry-arguments)
		(setq parms (assq-delete-all 'height parms)))
	    (if (assq 'width frame-initial-geometry-arguments)
		(setq parms (assq-delete-all 'width parms)))
	    (if (assq 'left frame-initial-geometry-arguments)
		(setq parms (assq-delete-all 'left parms)))
	    (if (assq 'top frame-initial-geometry-arguments)
		(setq parms (assq-delete-all 'top parms)))
	    (setq new
		  (make-frame
		   ;; Use the geometry args that created the existing
		   ;; frame, rather than the parms we get for it.
		   (append frame-initial-geometry-arguments
			   '((user-size . t) (user-position . t))
			   parms)))
	    ;; The initial frame, which we are about to delete, may be
	    ;; the only frame with a minibuffer.  If it is, create a
	    ;; new one.
	    (or (delq frame-initial-frame (minibuffer-frame-list))
                (and (eq minibuffer 'child-frame)
                     ;; Create a minibuffer child frame and parent it
                     ;; immediately.  Take any other parameters for
                     ;; the child frame from 'minibuffer-frame-list'.
                     (let* ((minibuffer-frame-alist
                             (cons `(parent-frame . ,new) minibuffer-frame-alist)))
                       (make-initial-minibuffer-frame nil)
                       ;; With a minibuffer child frame we do not want
                       ;; to select the minibuffer frame initially as
                       ;; we do for standard minibuffer-only frames.
                       (select-frame new)))
                (make-initial-minibuffer-frame nil))

	    ;; If the initial frame is serving as a surrogate
	    ;; minibuffer frame for any frames, we need to wean them
	    ;; onto a new frame.  The default-minibuffer-frame
	    ;; variable must be handled similarly.
	    (let ((users-of-initial
		   (filtered-frame-list
                    (lambda (frame)
                      (and (not (eq frame frame-initial-frame))
                           (eq (window-frame
                                (minibuffer-window frame))
                               frame-initial-frame))))))
              (if (or users-of-initial
		      (eq default-minibuffer-frame frame-initial-frame))

		  ;; Choose an appropriate frame.  Prefer frames which
		  ;; are only minibuffers.
		  (let* ((new-surrogate
			  (car
			   (or (filtered-frame-list
                                (lambda (frame)
                                  (eq (cdr (assq 'minibuffer
                                                 (frame-parameters frame)))
                                      'only)))
			       (minibuffer-frame-list))))
			 (new-minibuffer (minibuffer-window new-surrogate)))

		    (if (eq default-minibuffer-frame frame-initial-frame)
			(setq default-minibuffer-frame new-surrogate))

		    ;; Wean the frames using frame-initial-frame as
		    ;; their minibuffer frame.
		    (dolist (frame users-of-initial)
                      (modify-frame-parameters
                       frame (list (cons 'minibuffer new-minibuffer)))))))

            ;; Redirect events enqueued at this frame to the new frame.
	    ;; Is this a good idea?
	    (redirect-frame-focus frame-initial-frame new)

	    ;; Finally, get rid of the old frame.
	    (delete-frame frame-initial-frame t))

	;; Otherwise, we don't need all that rigmarole; just apply
	;; the new parameters.
	(let (newparms allparms tail)
	  (setq allparms (append initial-frame-alist
				 window-system-frame-alist
				 default-frame-alist nil))
	  (if (assq 'height frame-initial-geometry-arguments)
	      (setq allparms (assq-delete-all 'height allparms)))
	  (if (assq 'width frame-initial-geometry-arguments)
	      (setq allparms (assq-delete-all 'width allparms)))
	  (if (assq 'left frame-initial-geometry-arguments)
	      (setq allparms (assq-delete-all 'left allparms)))
	  (if (assq 'top frame-initial-geometry-arguments)
	      (setq allparms (assq-delete-all 'top allparms)))
	  (setq tail allparms)
	  ;; Find just the parms that have changed since we first
	  ;; made this frame.  Those are the ones actually set by
          ;; the init file.  For those parms whose values we already knew
	  ;; (such as those spec'd by command line options)
	  ;; it is undesirable to specify the parm again
          ;; once the user has seen the frame and been able to alter it
	  ;; manually.
	  (let (newval oldval)
	    (dolist (entry tail)
	      (setq oldval (assq (car entry) frame-initial-frame-alist))
	      (setq newval (cdr (assq (car entry) allparms)))
	      (or (and oldval (eq (cdr oldval) newval))
		  (setq newparms
			(cons (cons (car entry) newval) newparms)))))
	  (setq newparms (nreverse newparms))

	  (let ((new-bg (assq 'background-color newparms)))
	    ;; If the `background-color' parameter is changed, apply
	    ;; it first, then make sure that the `background-mode'
	    ;; parameter and other faces are updated, before applying
	    ;; the other parameters.
	    (when new-bg
	      (modify-frame-parameters frame-initial-frame
				       (list new-bg))
	      (unless (assq 'background-mode newparms)
		(frame-set-background-mode frame-initial-frame))
	      (face-set-after-frame-default frame-initial-frame)
	      (setq newparms (delq new-bg newparms)))

	    (modify-frame-parameters frame-initial-frame newparms)))))

    ;; Restore the original buffer.
    (set-buffer old-buffer)

    ;; Make sure the initial frame can be GC'd if it is ever deleted.
    ;; Make sure frame-notice-user-settings does nothing if called twice.
    (setq frame-notice-user-settings nil)
    (setq frame-initial-frame nil)))

(defun make-initial-minibuffer-frame (display)
  (let ((parms (append minibuffer-frame-alist '((minibuffer . only)))))
    (if display
	(make-frame-on-display display parms)
      (make-frame parms))))

;;;; Creation of additional frames, and other frame miscellanea

(defun modify-all-frames-parameters (alist)
  "Modify all current and future frames' parameters according to ALIST.
This changes `default-frame-alist' and possibly `initial-frame-alist'.
Furthermore, this function removes all parameters in ALIST from
`window-system-default-frame-alist'.
See help of `modify-frame-parameters' for more information."
  (dolist (frame (frame-list))
    (modify-frame-parameters frame alist))

  (dolist (pair alist) ;; conses to add/replace
    ;; initial-frame-alist needs setting only when
    ;; frame-notice-user-settings is true.
    (and frame-notice-user-settings
	 (setq initial-frame-alist
	       (assq-delete-all (car pair) initial-frame-alist)))
    (setq default-frame-alist
	  (assq-delete-all (car pair) default-frame-alist))
    ;; Remove any similar settings from the window-system specific
    ;; parameters---they would override default-frame-alist.
    (dolist (w window-system-default-frame-alist)
      (setcdr w (assq-delete-all (car pair) (cdr w)))))

  (and frame-notice-user-settings
       (setq initial-frame-alist (append initial-frame-alist alist)))
  (setq default-frame-alist (append default-frame-alist alist)))

(defun get-other-frame ()
  "Return some frame other than the current frame.
Create one if necessary.  Note that the minibuffer frame, if separate,
is not considered (see `next-frame')."
  (if (equal (next-frame) (selected-frame)) (make-frame) (next-frame)))

(defun next-window-any-frame ()
  "Select the next window, regardless of which frame it is on."
  (interactive)
  (select-window (next-window (selected-window)
			      (> (minibuffer-depth) 0)
			      0))
  (select-frame-set-input-focus (selected-frame)))

(defun previous-window-any-frame ()
  "Select the previous window, regardless of which frame it is on."
  (interactive)
  (select-window (previous-window (selected-window)
				  (> (minibuffer-depth) 0)
				  0))
  (select-frame-set-input-focus (selected-frame)))

(defalias 'next-multiframe-window #'next-window-any-frame)
(defalias 'previous-multiframe-window #'previous-window-any-frame)

(defun window-system-for-display (display)
  "Return the window system for DISPLAY.
Return nil if we don't know how to interpret DISPLAY."
  ;; MS-Windows doesn't know how to create a GUI frame in a -nw session.
  (if (and (eq system-type 'windows-nt)
	   (null (window-system))
	   (not (daemonp)))
      nil
    (cl-loop for descriptor in display-format-alist
	     for pattern = (car descriptor)
	     for system = (cdr descriptor)
	     when (string-match-p pattern display) return system)))

(defun make-frame-on-display (display &optional parameters)
  "Make a frame on display DISPLAY.
The optional argument PARAMETERS specifies additional frame parameters."
  (interactive (if (fboundp 'x-display-list)
                   (list (completing-read "Make frame on display: "
                                          (x-display-list) nil
                                          nil (car (x-display-list))
                                          nil (car (x-display-list))))
                 (user-error "This Emacs build does not support X displays")))
  (make-frame (cons (cons 'display display) parameters)))

(defun make-frame-on-current-monitor (&optional parameters)
  "Make a frame on the currently selected monitor.
Like `make-frame-on-monitor' and with the same PARAMETERS as in `make-frame'."
  (interactive)
  (let* ((monitor-workarea
          (cdr (assq 'workarea (frame-monitor-attributes))))
         (geometry-parameters
          (when monitor-workarea
            `((top . ,(nth 1 monitor-workarea))
              (left . ,(nth 0 monitor-workarea))))))
    (make-frame (append geometry-parameters parameters))))

(defun make-frame-on-monitor (monitor &optional display parameters)
  "Make a frame on monitor MONITOR.
The optional argument DISPLAY can be a display name, and the optional
argument PARAMETERS specifies additional frame parameters."
  (interactive
   (list
    (let* ((default (cdr (assq 'name (frame-monitor-attributes)))))
      (completing-read
       (format-prompt "Make frame on monitor" default)
       (or (delq nil (mapcar (lambda (a)
                               (cdr (assq 'name a)))
                             (display-monitor-attributes-list)))
           '(""))
       nil nil nil nil default))))
  (let* ((monitor-workarea
          (catch 'done
            (dolist (a (display-monitor-attributes-list display))
              (when (equal (cdr (assq 'name a)) monitor)
                (throw 'done (cdr (assq 'workarea a)))))))
         (geometry-parameters
          (when monitor-workarea
            `((top . ,(nth 1 monitor-workarea))
              (left . ,(nth 0 monitor-workarea))))))
    (make-frame (append geometry-parameters parameters))))

(declare-function x-close-connection "xfns.c" (terminal))

(defun close-display-connection (display)
  "Close the connection to a display, deleting all its associated frames.
For DISPLAY, specify either a frame or a display name (a string).
If DISPLAY is nil, that stands for the selected frame's display."
  (interactive
   (list
    (let* ((default (frame-parameter nil 'display))
           (display (completing-read
                     (format-prompt "Close display" default)
                     (delete-dups
                      (mapcar (lambda (frame)
                                (frame-parameter frame 'display))
                              (frame-list)))
                     nil t nil nil
                     default)))
      (if (zerop (length display)) default display))))
  (let ((frames (delq nil
                      (mapcar (lambda (frame)
                                (if (equal display
                                           (frame-parameter frame 'display))
                                    frame))
                              (frame-list)))))
    (if (and (consp frames)
             (not (y-or-n-p (if (cdr frames)
                                (format "Delete %s frames? " (length frames))
                              (format "Delete %s ? " (car frames))))))
        (error "Abort!")
      (mapc #'delete-frame frames)
      (x-close-connection display))))

(defun make-frame-command ()
  "Make a new frame, on the same terminal as the selected frame.
If the terminal is a text-only terminal, this also selects the
new frame.

When called from Lisp, returns the new frame."
  (interactive)
  (if (display-graphic-p)
      (make-frame)
    (select-frame (make-frame))))

(defun clone-frame (&optional frame no-windows)
  "Make a new frame with the same parameters and windows as FRAME.
If NO-WINDOWS is non-nil (interactively, the prefix argument), don't
clone the configuration of FRAME's windows.
If FRAME is a graphical frame and `frame-resize-pixelwise' is non-nil,
clone FRAME's pixel size.  Otherwise, use the number of FRAME's columns
and lines for the clone.

FRAME defaults to the selected frame.  The frame is created on the
same terminal as FRAME.  If the terminal is a text-only terminal then
also select the new frame."
  (interactive (list (selected-frame) current-prefix-arg))
  (let* ((frame (or frame (selected-frame)))
         (windows (unless no-windows
                    (window-state-get (frame-root-window frame))))
         (default-frame-alist
          (seq-remove (lambda (elem)
                        (memq (car elem) frame-internal-parameters))
                      (frame-parameters frame)))
         new-frame)
    (when (and frame-resize-pixelwise
               (display-graphic-p frame))
      (push (cons 'width (cons 'text-pixels (frame-text-width frame)))
            default-frame-alist)
      (push (cons 'height (cons 'text-pixels (frame-text-height frame)))
            default-frame-alist))
    (setq new-frame (make-frame))
    (when windows
      (window-state-put windows (frame-root-window new-frame) 'safe))
    (unless (display-graphic-p frame)
      (select-frame new-frame))
    new-frame))

(defvar before-make-frame-hook nil
  "Functions to run before `make-frame' creates a new frame.
Note that these functions are usually not run for the initial
frame, unless you add them to the hook in your early-init file.")

(defvar after-make-frame-functions nil
  "Functions to run after `make-frame' created a new frame.
The functions are run with one argument, the newly created
frame.
Note that these functions are usually not run for the initial
frame, unless you add them to the hook in your early-init file.")

(defvar after-setting-font-hook nil
  "Functions to run after a frame's font has been changed.")

(defvar frame-inherited-parameters '()
  "Parameters `make-frame' copies from the selected to the new frame.")

(defvar x-display-name)

(defun make-frame (&optional parameters)
  "Return a newly created frame displaying the current buffer.
Optional argument PARAMETERS is an alist of frame parameters for
the new frame.  Each element of PARAMETERS should have the
form (NAME . VALUE), for example:

 (name . STRING)	The frame should be named STRING.

 (width . NUMBER)	The frame should be NUMBER characters in width.
 (height . NUMBER)	The frame should be NUMBER text lines high.

 (minibuffer . t)	The frame should have a minibuffer.
 (minibuffer . nil)	The frame should have no minibuffer.
 (minibuffer . only)	The frame should contain only a minibuffer.
 (minibuffer . WINDOW)	The frame should use WINDOW as its minibuffer window.

 (window-system . nil)	The frame should be displayed on a terminal device.
 (window-system . x)	The frame should be displayed in an X window.

 (display . \":0\")     The frame should appear on display :0.

 (terminal . TERMINAL)  The frame should use the terminal object TERMINAL.

In addition, any parameter specified in `default-frame-alist',
but not present in PARAMETERS, is applied.

Before creating the frame (via `frame-creation-function'), this
function runs the hook `before-make-frame-hook'.  After creating
the frame, it runs the hook `after-make-frame-functions' with one
argument, the newly created frame.

If a display parameter is supplied and a window-system is not,
guess the window-system from the display.

On graphical displays, this function does not itself make the new
frame the selected frame.  However, the window system may select
the new frame according to its own rules.

By default do not display the current buffer in the new frame if the
buffer is hidden, that is, if the buffer's name starts with a space.
Display another buffer, one that could be returned by `other-buffer',
instead.  However, if `expose-hidden-buffer' is non-nil, display the
current buffer even if it is hidden."
  (interactive)
  (let* ((display (cdr (assq 'display parameters)))
         (w (cond
             ;; When running in a batch session, don't create a GUI
             ;; frame.  (Batch sessions don't set a SIGIO handler on
             ;; relevant platforms, so attempting this would terminate
             ;; Emacs.)
             (noninteractive nil)
             ((assq 'terminal parameters)
              (let ((type (terminal-live-p
                           (cdr (assq 'terminal parameters)))))
                (cond
                 ((eq t type) nil)
                 ((null type) (error "Terminal %s does not exist"
                                     (cdr (assq 'terminal parameters))))
                 (t type))))
             ((assq 'window-system parameters)
              (cdr (assq 'window-system parameters)))
             (display
              (or (window-system-for-display display)
                  (error "Don't know how to interpret display %S"
                         display)))
             (t window-system)))
	 (params parameters)
	 frame child-frame)

    (unless (get w 'window-system-initialized)
      (let ((window-system w))          ;Hack attack!
        (window-system-initialization display))
      (setq x-display-name display)
      (put w 'window-system-initialized t))

    ;; Add parameters from `window-system-default-frame-alist'.
    (dolist (p (cdr (assq w window-system-default-frame-alist)))
      (unless (assq (car p) params)
	(push p params)))
    ;; Add parameters from `default-frame-alist'.
    (dolist (p default-frame-alist)
      (unless (assq (car p) params)
	(push p params)))
    ;; Add parameters from `frame-inherited-parameters' unless they are
    ;; overridden by explicit parameters.
    (dolist (param frame-inherited-parameters)
      (unless (assq param parameters)
        (let ((val (frame-parameter nil param)))
          (when val (push (cons param val) params)))))

    (when (eq (cdr (or (assq 'minibuffer params) '(minibuffer . t)))
              'child-frame)
      ;; If the 'minibuffer' parameter equals 'child-frame' make a
      ;; frame without minibuffer first using the root window of
      ;; 'default-minibuffer-frame' as its minibuffer window
      (setq child-frame t)
      (setq params (cons '(minibuffer)
                         (delq (assq 'minibuffer params) params))))

    ;; Now make the frame.
    (run-hooks 'before-make-frame-hook)

    (setq frame (let ((window-system w)) ; Hack attack!
                  (frame-creation-function params)))

    (when child-frame
      ;; When we want to equip the new frame with a minibuffer-only
      ;; child frame, make that frame and reparent it immediately.
      (setq child-frame
            (make-frame
             (append
              `((display . ,display) (minibuffer . only)
                (parent-frame . ,frame))
              minibuffer-frame-alist)))
      (when (frame-live-p child-frame)
        ;; Have the 'minibuffer' parameter of our new frame refer to
        ;; its child frame's root window.
        (set-frame-parameter
         frame 'minibuffer (frame-root-window child-frame))))

    (normal-erase-is-backspace-setup-frame frame)

    ;; We can run `window-configuration-change-hook' for this frame now.
    (frame-after-make-frame frame t)
    (run-hook-with-args 'after-make-frame-functions frame)
    frame))

(defun filtered-frame-list (predicate)
  "Return a list of all live frames which satisfy PREDICATE."
  (let* ((frames (frame-list))
	 (list frames))
    (while (consp frames)
      (unless (funcall predicate (car frames))
	(setcar frames nil))
      (setq frames (cdr frames)))
    (delq nil list)))

(defun minibuffer-frame-list ()
  "Return a list of all frames with their own minibuffers."
  (filtered-frame-list
   (lambda (frame)
     (eq frame (window-frame (minibuffer-window frame))))))

;; Used to be called `terminal-id' in termdev.el.
(defun get-device-terminal (device)
  "Return the terminal corresponding to DEVICE.
DEVICE can be a terminal, a frame, nil (meaning the selected frame's terminal),
the name of an X display device (HOST.SERVER.SCREEN) or a tty device file."
  (cond
   ((or (null device) (framep device))
    (frame-terminal device))
   ((stringp device)
    (let ((f (car (filtered-frame-list
                   (lambda (frame)
                     (or (equal (frame-parameter frame 'display) device)
                         (equal (frame-parameter frame 'tty) device)))))))
      (or f (error "Display %s does not exist" device))
      (frame-terminal f)))
   ((terminal-live-p device) device)
   (t
    (error "Invalid argument %s in `get-device-terminal'" device))))

(defun frames-on-display-list (&optional device)
  "Return a list of all frames on DEVICE.

DEVICE should be a terminal, a frame,
or a name of an X display or tty (a string of the form
HOST:SERVER.SCREEN).

If DEVICE is omitted or nil, it defaults to the selected
frame's terminal device."
  (let* ((terminal (get-device-terminal device))
	 (func #'(lambda (frame)
		   (eq (frame-terminal frame) terminal))))
    (filtered-frame-list func)))

(defun framep-on-display (&optional terminal)
  "Return the type of frames on TERMINAL.
TERMINAL may be a terminal id, a display name or a frame.  If it
is a frame, its type is returned.  If TERMINAL is omitted or nil,
it defaults to the selected frame's terminal device.  All frames
on a given display are of the same type."
  (or (terminal-live-p terminal)
      (framep terminal)
      (framep (car (frames-on-display-list terminal)))))

(defun frame-remove-geometry-params (param-list)
  "Return the parameter list PARAM-LIST, but with geometry specs removed.
This deletes all bindings in PARAM-LIST for `top', `left', `width',
`height', `user-size' and `user-position' parameters.
Emacs uses this to avoid overriding explicit moves and resizings from
the user during startup."
  (setq param-list (cons nil param-list))
  (let ((tail param-list))
    (while (consp (cdr tail))
      (if (and (consp (car (cdr tail)))
	       (memq (car (car (cdr tail)))
		     '(height width top left user-position user-size)))
	  (progn
	    (setq frame-initial-geometry-arguments
		  (cons (car (cdr tail)) frame-initial-geometry-arguments))
	    (setcdr tail (cdr (cdr tail))))
	(setq tail (cdr tail)))))
  (setq frame-initial-geometry-arguments
	(nreverse frame-initial-geometry-arguments))
  (cdr param-list))

(declare-function x-focus-frame "frame.c" (frame &optional noactivate))

(defun select-frame-set-input-focus (frame &optional norecord)
  "Select FRAME, raise it, and set input focus, if possible.
If `mouse-autoselect-window' is non-nil, also move mouse pointer
to FRAME's selected window.  Otherwise, if `focus-follows-mouse'
is non-nil, move mouse cursor to FRAME.

Optional argument NORECORD means to neither change the order of
recently selected windows nor the buffer list."
  (select-frame frame norecord)
  (raise-frame frame)
  ;; Ensure, if possible, that FRAME gets input focus.
  (when (display-multi-frame-p frame)
    (x-focus-frame frame))
  ;; Move mouse cursor if necessary.
  (cond
   (mouse-autoselect-window
    (let ((edges (window-edges (frame-selected-window frame)
                               t nil t)))
      ;; Move mouse cursor into FRAME's selected window to avoid that
      ;; Emacs mouse-autoselects another window.
      (set-mouse-pixel-position frame (1- (nth 2 edges)) (nth 1 edges))))
   (focus-follows-mouse
    ;; Move mouse cursor into FRAME to avoid that another frame gets
    ;; selected by the window manager.
    (set-mouse-position frame (1- (frame-width frame)) 0))))

(defun other-frame (arg)
  "Select the ARGth visible frame on current display, and raise it.
All frames are arranged in a cyclic order.  This command selects the
frame ARG steps away from the selected frame in that order.  A negative
ARG moves in the opposite order.  It does not select a minibuffer-only
frame.

To make this command work properly, you must tell Emacs how the
system (or the window manager) generally handles focus-switching
between windows.  If moving the mouse onto a window selects
it (gives it focus), set `focus-follows-mouse' to t.  Otherwise,
that variable should be nil."
  (interactive "p")
  (let ((sframe (selected-frame))
        (frame (selected-frame)))
    (while (> arg 0)
      (setq frame (next-frame frame))
      (while (and (not (eq frame sframe))
                  (not (eq (frame-visible-p frame) t)))
	(setq frame (next-frame frame)))
      (setq arg (1- arg)))
    (while (< arg 0)
      (setq frame (previous-frame frame))
      (while (and (not (eq frame sframe))
                  (not (eq (frame-visible-p frame) t)))
	(setq frame (previous-frame frame)))
      (setq arg (1+ arg)))
    (select-frame-set-input-focus frame)))

(defun other-frame-prefix ()
  "Display the buffer of the next command in a new frame.
The next buffer is the buffer displayed by the next command invoked
immediately after this command (ignoring reading from the minibuffer).
In case of multiple consecutive mouse events such as <down-mouse-1>,
a mouse release event <mouse-1>, <double-mouse-1>, <triple-mouse-1>
all bound commands are handled until one of them displays a buffer.
Creates a new frame before displaying the buffer.
When `switch-to-buffer-obey-display-actions' is non-nil,
`switch-to-buffer' commands are also supported."
  (interactive)
  (display-buffer-override-next-command
   (lambda (buffer alist)
     (cons (display-buffer-pop-up-frame
            buffer (append '((inhibit-same-window . t))
                           alist))
           'frame))
   nil "[other-frame]")
  (message "Display next command buffer in a new frame..."))

(defun iconify-or-deiconify-frame ()
  "Iconify the selected frame, or deiconify if it's currently an icon."
  (interactive)
  (if (eq (cdr (assq 'visibility (frame-parameters))) t)
      (iconify-frame)
    (make-frame-visible)))

(defun suspend-frame ()
  "Do whatever is right to suspend the current frame.
Calls `suspend-emacs' if invoked from the controlling tty device,
`suspend-tty' from a secondary tty device, and
`iconify-or-deiconify-frame' from a graphical frame."
  (interactive)
  (cond
   ((display-multi-frame-p) (iconify-or-deiconify-frame))
   ((eq (framep (selected-frame)) t)
    (if (controlling-tty-p)
        (suspend-emacs)
      (suspend-tty)))
   (t (suspend-emacs))))

(defun frame-list-1 (&optional frame)
  "Return list of all live frames starting with FRAME.
The optional argument FRAME must specify a live frame and defaults to
the selected frame.  Tooltip frames are not included."
  (let* ((frame (window-normalize-frame frame))
	 (frames (frame-list)))
    (unless (eq (car frames) frame)
      (let ((tail frames))
	(while tail
	  (if (eq (cadr tail) frame)
	      (let ((head (cdr tail)))
		(setcdr tail nil)
		(setq frames (nconc head frames))
		(setq tail nil))
	    (setq tail (cdr tail))))))
    frames))

(defun make-frame-names-alist (&optional frame)
  "Return alist of frame names and frames starting with FRAME.
Only visible or iconified frames on the same terminal as FRAME are
listed.  Frames with a non-nil `no-other-frame' parameter are not
listed.  The optional argument FRAME must specify a live frame and
defaults to the selected frame."
  (let ((frames (frame-list-1 frame))
	(terminal (frame-parameter frame 'terminal))
	alist)
    (dolist (frame frames)
      (when (and (frame-visible-p frame)
		 (eq (frame-parameter frame 'terminal) terminal)
		 (not (frame-parameter frame 'no-other-frame)))
	(push (cons (frame-parameter frame 'name) frame) alist)))
    (nreverse alist)))

(defvar frame-name-history nil)
(defun select-frame-by-name (name)
  "Select the frame whose name is NAME and raise it.
Frames on the current terminal are checked first.
If there is no frame by that name, signal an error."
  (interactive
   (let* ((frame-names-alist (make-frame-names-alist))
	   (default (car (car frame-names-alist)))
	   (input (completing-read
		   (format-prompt "Select Frame" default)
		   frame-names-alist nil t nil 'frame-name-history)))
     (if (= (length input) 0)
	 (list default)
       (list input))))
  (select-frame-set-input-focus
   ;; Prefer frames on the current display.
   (or (cdr (assoc name (make-frame-names-alist)))
       (catch 'done
         (dolist (frame (frame-list))
           (when (equal (frame-parameter frame 'name) name)
             (throw 'done frame))))
       (error "There is no frame named `%s'" name))))


;;;; Background mode.

(defcustom frame-background-mode nil
  "The brightness of the background.
Set this to the symbol `dark' if your background color is dark,
`light' if your background is light, or nil (automatic by default)
if you want Emacs to examine the brightness for you.

If you change this without using customize, you should use
`frame-set-background-mode' to update existing frames;
e.g. (mapc \\='frame-set-background-mode (frame-list))."
  :group 'faces
  :set #'(lambda (var value)
	   (set-default var value)
	   (mapc #'frame-set-background-mode (frame-list)))
  :initialize #'custom-initialize-changed
  :type '(choice (const dark)
		 (const light)
		 (const :tag "automatic" nil)))

(declare-function x-get-resource "frame.c"
		  (attribute class &optional component subclass))

;; Only used if window-system is not null.
(declare-function x-display-grayscale-p "xfns.c" (&optional terminal))

(defvar inhibit-frame-set-background-mode nil)

(defun frame--current-background-mode (frame)
  (let* ((frame-default-bg-mode (frame-terminal-default-bg-mode frame))
         (bg-color (frame-parameter frame 'background-color))
         (tty-type (tty-type frame))
         (default-bg-mode
	   (if (or (window-system frame)
		   (and tty-type
			(string-match "^\\(xterm\\|rxvt\\|dtterm\\|eterm\\)"
				      tty-type)))
	       'light
	     'dark)))
    (cond (frame-default-bg-mode)
	  ((equal bg-color "unspecified-fg") ; inverted colors
	   (if (eq default-bg-mode 'light) 'dark 'light))
	  ((not (color-values bg-color frame))
	   default-bg-mode)
	  ((color-dark-p (mapcar (lambda (c) (/ c 65535.0))
	                         (color-values bg-color frame)))
	   'dark)
	  (t 'light))))

(defun frame-set-background-mode (frame &optional keep-face-specs)
  "Set up display-dependent faces on FRAME.
Display-dependent faces are those which have different definitions
according to the `background-mode' and `display-type' frame parameters.

If optional arg KEEP-FACE-SPECS is non-nil, don't recalculate
face specs for the new background mode."
  (unless inhibit-frame-set-background-mode
    (let* ((bg-mode
	    (frame--current-background-mode frame))
	   (display-type
	    (cond ((null (window-system frame))
		   (if (tty-display-color-p frame) 'color 'mono))
		  ((display-color-p frame)
		   'color)
		  ((x-display-grayscale-p frame)
		   'grayscale)
		  (t 'mono)))
	   (old-bg-mode
	    (frame-parameter frame 'background-mode))
	   (old-display-type
	    (frame-parameter frame 'display-type)))

      (unless (and (eq bg-mode old-bg-mode) (eq display-type old-display-type))
	(let ((locally-modified-faces nil)
	      ;; Prevent face-spec-recalc from calling this function
	      ;; again, resulting in a loop (bug#911).
	      (inhibit-frame-set-background-mode t)
	      (params (list (cons 'background-mode bg-mode)
			    (cons 'display-type display-type))))
	  (if keep-face-specs
	      (modify-frame-parameters frame params)
	    ;; If we are recomputing face specs, first collect a list
	    ;; of faces that don't match their face-specs.  These are
	    ;; the faces modified on FRAME, and we avoid changing them
	    ;; below.  Use a negative list to avoid consing (we assume
	    ;; most faces are unmodified).
	    (dolist (face (face-list))
	      (and (not (get face 'face-override-spec))
		   (not (and
                         ;; If the face was not yet realized for the
                         ;; frame, face-spec-match-p will signal an
                         ;; error, so treat such a missing face as
                         ;; having a mismatched spec; the call to
                         ;; face-spec-recalc below will then realize
                         ;; the face for the frame.  This happens
                         ;; during startup with -rv on the command
                         ;; line for the initial frame, because frames
                         ;; are not recorded in the pdump file.
                         (gethash face (frame--face-hash-table))
                         (face-spec-match-p face
                                            (face-user-default-spec face)
                                            frame)))
		   (push face locally-modified-faces)))
	    ;; Now change to the new frame parameters
	    (modify-frame-parameters frame params)
	    ;; For all unmodified named faces, choose face specs
	    ;; matching the new frame parameters.
	    (dolist (face (face-list))
	      (unless (memq face locally-modified-faces)
		(face-spec-recalc face frame)))))))))

(defun frame-terminal-default-bg-mode (frame)
  "Return the default background mode of FRAME.
This checks the `frame-background-mode' variable, the X resource
named \"backgroundMode\" (if FRAME is an X frame), and finally
the `background-mode' terminal parameter."
  (or frame-background-mode
      (let ((bg-resource
	     (and (window-system frame)
		  (x-get-resource "backgroundMode" "BackgroundMode"))))
	(if bg-resource
	    (intern (downcase bg-resource))))
      (terminal-parameter frame 'background-mode)))

;; FIXME: This needs to be significantly improved before we can use it:
;; - Fix the "scope" to be consistent: the code below is partly per-frame
;;   and partly all-frames :-(
;; - Make it interact correctly with color themes (e.g. modus-themes).
;;   Maybe automatically disabling color themes that disagree with the
;;   selected value of `dark-mode'.
;; - Check interaction with "(in|re)verse-video".
;;
;; (define-minor-mode dark-mode
;;   "Use light text on dark background."
;;   :global t
;;   :group 'faces
;;   (when (eq dark-mode
;;             (eq 'light (frame--current-background-mode (selected-frame))))
;;     ;; FIXME: Change the face's SPEC instead?
;;     (set-face-attribute 'default nil
;;                         :foreground (face-attribute 'default :background)
;;                         :background (face-attribute 'default :foreground))
;;     (frame-set-background-mode (selected-frame))))


;;;; Frame configurations

(defun current-frame-configuration ()
  "Return a list describing the positions and states of all frames.
Its car is `frame-configuration'.
Each element of the cdr is a list of the form (FRAME ALIST WINDOW-CONFIG),
where
  FRAME is a frame object,
  ALIST is an association list specifying some of FRAME's parameters, and
  WINDOW-CONFIG is a window configuration object for FRAME."
  (cons 'frame-configuration
	(mapcar (lambda (frame)
                  (list frame
                        (frame-parameters frame)
                        (current-window-configuration frame)))
		(frame-list))))

(defun set-frame-configuration (configuration &optional nodelete)
  "Restore the frames to the state described by CONFIGURATION.
Each frame listed in CONFIGURATION has its position, size, window
configuration, and other parameters set as specified in CONFIGURATION.
However, this function does not restore deleted frames.

Ordinarily, this function deletes all existing frames not
listed in CONFIGURATION.  But if optional second argument NODELETE
is given and non-nil, the unwanted frames are iconified instead."
  (or (frame-configuration-p configuration)
      (signal 'wrong-type-argument
	      (list 'frame-configuration-p configuration)))
  (let ((config-alist (cdr configuration))
	frames-to-delete)
    (dolist (frame (frame-list))
      (let ((parameters (assq frame config-alist)))
        (if parameters
            (progn
              (modify-frame-parameters
               frame
               ;; Since we can't set a frame's minibuffer status,
               ;; we might as well omit the parameter altogether.
               (let* ((parms (nth 1 parameters))
		      (mini (assq 'minibuffer parms))
		      (name (assq 'name parms))
		      (explicit-name (cdr (assq 'explicit-name parms))))
		 (when mini (setq parms (delq mini parms)))
		 ;; Leave name in iff it was set explicitly.
		 ;; This should fix the behavior reported in
		 ;; https://lists.gnu.org/r/emacs-devel/2007-08/msg01632.html
		 (when (and name (not explicit-name))
		   (setq parms (delq name parms)))
                 parms))
              (set-window-configuration (nth 2 parameters)))
          (setq frames-to-delete (cons frame frames-to-delete)))))
    (mapc (if nodelete
              ;; Note: making frames invisible here was tried
              ;; but led to some strange behavior--each time the frame
              ;; was made visible again, the window manager asked afresh
              ;; for where to put it.
              'iconify-frame
            'delete-frame)
          frames-to-delete)))

;;;; Convenience functions for accessing and interactively changing
;;;; frame parameters.

(defun frame-height (&optional frame)
  "Return number of lines available for display on FRAME.
If FRAME is omitted, describe the currently selected frame.
Exactly what is included in the return value depends on the
window-system and toolkit in use - see `frame-pixel-height' for
more details.  The lines are in units of the default font height.

The result is roughly related to the frame pixel height via
height in pixels = height in lines * `frame-char-height'.
However, this is only approximate, and is complicated e.g. by the
fact that individual window lines and menu bar lines can have
differing font heights."
  (cdr (assq 'height (frame-parameters frame))))

(defun frame-width (&optional frame)
  "Return number of columns available for display on FRAME.
If FRAME is omitted, describe the currently selected frame."
  (cdr (assq 'width (frame-parameters frame))))

(defalias 'frame-border-width #'frame-internal-border-width)
(defalias 'frame-pixel-width #'frame-native-width)
(defalias 'frame-pixel-height #'frame-native-height)

(defun frame-inner-width (&optional frame)
  "Return inner width of FRAME in pixels.
FRAME defaults to the selected frame."
  (setq frame (window-normalize-frame frame))
  (- (frame-native-width frame)
     (* 2 (frame-internal-border-width frame))))

(defun frame-inner-height (&optional frame)
  "Return inner height of FRAME in pixels.
FRAME defaults to the selected frame."
  (setq frame (window-normalize-frame frame))
  (- (frame-native-height frame)
     (if (fboundp 'tab-bar-height) (tab-bar-height frame t) 0)
     (* 2 (frame-internal-border-width frame))))

(defun frame-outer-width (&optional frame)
  "Return outer width of FRAME in pixels.
FRAME defaults to the selected frame."
  (setq frame (window-normalize-frame frame))
  (let ((edges (frame-edges frame 'outer-edges)))
    (- (nth 2 edges) (nth 0 edges))))

(defun frame-outer-height (&optional frame)
  "Return outer height of FRAME in pixels.
FRAME defaults to the selected frame."
  (setq frame (window-normalize-frame frame))
  (let ((edges (frame-edges frame 'outer-edges)))
    (- (nth 3 edges) (nth 1 edges))))

(declare-function x-list-fonts "xfaces.c"
                  (pattern &optional face frame maximum width))

(defun set-frame-font (font &optional keep-size frames inhibit-customize)
  "Set the default font to FONT.
When called interactively, prompt for the name of a font, and use
that font on the selected frame.  When called from Lisp, FONT
should be a font name (a string), a font object, font entity, or
font spec.

If KEEP-SIZE is nil, keep the number of frame lines and columns
fixed.  If KEEP-SIZE is non-nil (or with a prefix argument), try
to keep the current frame size fixed (in pixels) by adjusting the
number of lines and columns.

If FRAMES is nil, apply the font to the selected frame only.
If FRAMES is non-nil, it should be a list of frames to act upon,
or t meaning all existing graphical frames.
Also, if FRAMES is non-nil, alter the user's Customization settings
as though the font-related attributes of the `default' face had been
\"set in this session\", so that the font is applied to future frames.

If INHIBIT-CUSTOMIZE is non-nil, don't update the user's
Customization settings."
  (interactive
   (let* ((completion-ignore-case t)
          (default (frame-parameter nil 'font))
	  (font (completing-read (format-prompt "Font name" default)
				 ;; x-list-fonts will fail with an error
				 ;; if this frame doesn't support fonts.
				 (x-list-fonts "*" nil (selected-frame))
                                 nil nil nil nil default)))
     (list font current-prefix-arg nil)))
  (when (or (stringp font) (fontp font))
    (let* ((this-frame (selected-frame))
	   ;; FRAMES nil means affect the selected frame.
	   (frame-list (cond ((null frames)
			      (list this-frame))
			     ((eq frames t)
			      (frame-list))
			     (t frames)))
	   height width)
      (dolist (f frame-list)
	(when (display-multi-font-p f)
	  (if keep-size
	      (setq height (* (frame-parameter f 'height)
			      (frame-char-height f))
		    width  (* (frame-parameter f 'width)
			      (frame-char-width f))))
	  ;; When set-face-attribute is called for :font, Emacs
	  ;; guesses the best font according to other face attributes
	  ;; (:width, :weight, etc.) so reset them too (Bug#2476).
	  (set-face-attribute 'default f
			      :width 'normal :weight 'normal
			      :slant 'normal :font font)
	  (if keep-size
	      (modify-frame-parameters
	       f
	       (list (cons 'height (round height (frame-char-height f)))
		     (cons 'width  (round width  (frame-char-width f))))))))
      (when (and frames
                 (not inhibit-customize))
	;; Alter the user's Custom setting of the `default' face, but
	;; only for font-related attributes.
	(let ((specs (cadr (assq 'user (get 'default 'theme-face))))
	      (attrs '(:family :foundry :slant :weight :height :width))
	      (new-specs nil))
	  (if (null specs) (setq specs '((t nil))))
	  (dolist (spec specs)
	    ;; Each SPEC has the form (DISPLAY ATTRIBUTE-PLIST)
	    (let ((display (nth 0 spec))
		  (plist   (copy-tree (nth 1 spec))))
	      ;; Alter only DISPLAY conditions matching this frame.
	      (when (or (memq display '(t default))
			(face-spec-set-match-display display this-frame))
		(dolist (attr attrs)
		  (setq plist (plist-put plist attr
					 (face-attribute 'default attr)))))
	      (push (list display plist) new-specs)))
	  (setq new-specs (nreverse new-specs))
	  (put 'default 'customized-face new-specs)
	  (custom-push-theme 'theme-face 'default 'user 'set new-specs)
	  (put 'default 'face-modified nil))))
    (run-hooks 'after-setting-font-hook 'after-setting-font-hooks)))

(defun set-frame-parameter (frame parameter value)
  "Set frame parameter PARAMETER to VALUE on FRAME.
If FRAME is nil, it defaults to the selected frame.
See `modify-frame-parameters'."
  (modify-frame-parameters frame (list (cons parameter value))))

(defun set-background-color (color-name)
  "Set the background color of the selected frame to COLOR-NAME.
When called interactively, prompt for the name of the color to use.
To get the frame's current background color, use `frame-parameters'."
  (interactive (list (read-color "Background color: ")))
  (modify-frame-parameters (selected-frame)
			   (list (cons 'background-color color-name)))
  (or window-system
      (face-set-after-frame-default (selected-frame)
				    (list
				     (cons 'background-color color-name)
				     ;; Pass the foreground-color as
				     ;; well, if defined, to avoid
				     ;; losing it when faces are reset
				     ;; to their defaults.
				     (assq 'foreground-color
					   (frame-parameters))))))

(defun set-foreground-color (color-name)
  "Set the foreground color of the selected frame to COLOR-NAME.
When called interactively, prompt for the name of the color to use.
To get the frame's current foreground color, use `frame-parameters'."
  (interactive (list (read-color "Foreground color: " nil nil nil t)))
  (modify-frame-parameters (selected-frame)
			   (list (cons 'foreground-color color-name)))
  (or window-system
      (face-set-after-frame-default (selected-frame)
				    (list
				     (cons 'foreground-color color-name)
				     ;; Pass the background-color as
				     ;; well, if defined, to avoid
				     ;; losing it when faces are reset
				     ;; to their defaults.
				     (assq 'background-color
					   (frame-parameters))))))

(defun set-cursor-color (color-name)
  "Set the text cursor color of the selected frame to COLOR-NAME.
When called interactively, prompt for the name of the color to use.
This works by setting the `cursor-color' frame parameter on the
selected frame.

You can also set the text cursor color, for all frames, by
customizing the `cursor' face."
  (interactive (list (read-color "Cursor color: ")))
  (modify-frame-parameters (selected-frame)
			   (list (cons 'cursor-color color-name))))

(defun set-mouse-color (color-name)
  "Set the color of the mouse pointer of the selected frame to COLOR-NAME.
When called interactively, prompt for the name of the color to use.
To get the frame's current mouse color, use `frame-parameters'."
  (interactive (list (read-color "Mouse color: ")))
  (modify-frame-parameters (selected-frame)
			   (list (cons 'mouse-color
				       (or color-name
					   (cdr (assq 'mouse-color
						      (frame-parameters))))))))

(defun set-border-color (color-name)
  "Set the color of the border of the selected frame to COLOR-NAME.
When called interactively, prompt for the name of the color to use.
To get the frame's current border color, use `frame-parameters'."
  (interactive (list (read-color "Border color: ")))
  (modify-frame-parameters (selected-frame)
			   (list (cons 'border-color color-name))))

(define-minor-mode auto-raise-mode
  "Toggle whether or not selected frames should auto-raise.

Auto Raise mode does nothing under most window managers, which
switch focus on mouse clicks.  It only has an effect if your
window manager switches focus on mouse movement (in which case
you should also change `focus-follows-mouse' to t).  Then,
enabling Auto Raise mode causes any graphical Emacs frame which
acquires focus to be automatically raised.

Note that this minor mode controls Emacs's own auto-raise
feature.  Window managers that switch focus on mouse movement
often have their own auto-raise feature."
  ;; This isn't really a global minor mode; rather, it's local to the
  ;; selected frame, but declaring it as global prevents a misleading
  ;; "Auto-Raise mode enabled in current buffer" message from being
  ;; displayed when it is turned on.
  :global t
  :variable (frame-parameter nil 'auto-raise)
  (if (frame-parameter nil 'auto-raise)
      (raise-frame)))

(define-minor-mode auto-lower-mode
  "Toggle whether or not the selected frame should auto-lower.

Auto Lower mode does nothing under most window managers, which
switch focus on mouse clicks.  It only has an effect if your
window manager switches focus on mouse movement (in which case
you should also change `focus-follows-mouse' to t).  Then,
enabling Auto Lower Mode causes any graphical Emacs frame which
loses focus to be automatically lowered.

Note that this minor mode controls Emacs's own auto-lower
feature.  Window managers that switch focus on mouse movement
often have their own features for raising or lowering frames."
  :variable (frame-parameter nil 'auto-lower))

(defun set-frame-name (name)
  "Set the name of the selected frame to NAME.
When called interactively, prompt for the name of the frame.
On text terminals, the frame name is displayed on the mode line.
On graphical displays, it is displayed on the frame's title bar."
  (interactive
   (let ((default (cdr (assq 'name (frame-parameters)))))
     (list (read-string (format-prompt "Frame name" default) nil nil
                        default))))
  (modify-frame-parameters (selected-frame)
			   (list (cons 'name name))))

(defun frame-current-scroll-bars (&optional frame)
  "Return the current scroll-bar types for frame FRAME.
Value is a cons (VERTICAL . HORIZ0NTAL) where VERTICAL specifies
the current location of the vertical scroll-bars (`left', `right'
or nil), and HORIZONTAL specifies the current location of the
horizontal scroll bars (`bottom' or nil).  FRAME must specify a
live frame and defaults to the selected one."
  (let* ((frame (window-normalize-frame frame))
	 (vertical (frame-parameter frame 'vertical-scroll-bars))
	 (horizontal (frame-parameter frame 'horizontal-scroll-bars)))
    (unless (memq vertical '(left right nil))
      (setq vertical default-frame-scroll-bars))
    (cons vertical (and horizontal 'bottom))))

(declare-function x-frame-geometry "xfns.c" (&optional frame))
(declare-function w32-frame-geometry "w32fns.c" (&optional frame))
(declare-function ns-frame-geometry "nsfns.m" (&optional frame))
(declare-function pgtk-frame-geometry "pgtkfns.c" (&optional frame))
(declare-function haiku-frame-geometry "haikufns.c" (&optional frame))
(declare-function android-frame-geometry "androidfns.c" (&optional frame))
(declare-function tty-frame-geometry "term.c" (&optional frame))

(defun frame-geometry (&optional frame)
  "Return geometric attributes of FRAME.
FRAME must be a live frame and defaults to the selected one.  The return
value is an association list of the attributes listed below.  All height
and width values are in pixels.

`outer-position' is a cons of the outer left and top edges of FRAME
  relative to the origin - the position (0, 0) - of FRAME's display.

`outer-size' is a cons of the outer width and height of FRAME.  The
  outer size includes the title bar and the external borders as well as
  any menu and/or tool bar of frame.

`external-border-size' is a cons of the horizontal and vertical width of
  FRAME's external borders as supplied by the window manager.

`title-bar-size' is a cons of the width and height of the title bar of
  FRAME as supplied by the window manager.  If both of them are zero,
  FRAME has no title bar.  If only the width is zero, Emacs was not
  able to retrieve the width information.

`menu-bar-external', if non-nil, means the menu bar is external (never
  included in the inner edges of FRAME).

`menu-bar-size' is a cons of the width and height of the menu bar of
  FRAME.

`tool-bar-external', if non-nil, means the tool bar is external (never
  included in the inner edges of FRAME).

`tool-bar-position' tells on which side the tool bar on FRAME is and can
  be one of `left', `top', `right' or `bottom'.  If this is nil, FRAME
  has no tool bar.

`tool-bar-size' is a cons of the width and height of the tool bar of
  FRAME.

`internal-border-width' is the width of the internal border of
  FRAME."
  (let* ((frame (window-normalize-frame frame))
	 (frame-type (framep-on-display frame)))
    (cond
     ((eq frame-type 'x)
      (x-frame-geometry frame))
     ((eq frame-type 'w32)
      (w32-frame-geometry frame))
     ((eq frame-type 'ns)
      (ns-frame-geometry frame))
     ((eq frame-type 'pgtk)
      (pgtk-frame-geometry frame))
     ((eq frame-type 'haiku)
      (haiku-frame-geometry frame))
     ((eq frame-type 'android)
      (android-frame-geometry frame))
     (t
      (tty-frame-geometry frame)))))

(defun frame--size-history (&optional frame)
  "Print history of resize operations for FRAME.
This function dumps a prettified version of `frame-size-history'
into a buffer called *frame-size-history*.  The optional argument
FRAME denotes the frame whose history will be dumped; it defaults
to the selected frame.

Storing information about resize operations is off by default.
If you set the variable `frame-size-history' like this

(setq frame-size-history \\='(100))

then Emacs will save information about the next 100 significant
operations affecting any frame's size in that variable.  This
function prints the entries for FRAME stored in that variable in
a more legible way.

All lines start with an indication of the requested action.  An
entry like `menu-bar-lines' or `scroll-bar-width' indicates that
a change of the corresponding frame parameter or Lisp variable
was requested.  An entry like gui_figure_window_size indicates
that that C function was executed, an entry like ConfigureNotify
indicates that that event was received.

In long entries, a number in parentheses displays the INHIBIT
parameter passed to the C function adjust_frame_size.  Such
entries may also display changes of frame rectangles in a form
like R=n1xn2~>n3xn4 where R denotes the rectangle type (TS for
text, NS for native and IS for inner frame rectangle sizes, all
in pixels, TC for text rectangle sizes in frame columns and
lines), n1 and n2 denote the old width and height and n3 and n4
the new width and height in the according units.  MS stands for
the minimum inner frame size in pixels, IH and IV, if present,
indicate that resizing horizontally and/or vertically was
inhibited (either by `frame-inhibit-implied-resize' or because of
the frame's fullscreen state).

Shorter entries represent C functions that process width and
height changes of the native rectangle where PS stands for the
frame's present pixel width and height, XS for a requested pixel
width and height and DS for some earlier requested but so far
delayed pixel width and height.

Very short entries represent calls of C functions that do not
directly ask for size changes but may indirectly affect the size
of frames like calls to map a frame or change its visibility."
  (let ((history (reverse frame-size-history))
	entry item)
    (setq frame (window-normalize-frame frame))
    (with-current-buffer (get-buffer-create "*frame-size-history*")
      (erase-buffer)
      (insert (format "Frame size history of %s\n" frame))
      (while (consp (setq entry (pop history)))
        (setq item (car entry))
	(cond
         ((not (consp item))
          ;; An item added quickly for debugging purposes.
          (insert (format "%s\n" entry)))
         ((and (eq (nth 0 item) frame) (= (nth 1 item) 1))
          ;; Length 1 is a "plain event".
          (insert (format "%s\n" (nth 2 item))))
         ((and (eq (nth 0 item) frame) (= (nth 1 item) 2))
          ;; Length 2 is an "extra" item.
          (insert (format "%s" (nth 2 item)))
          (setq item (nth 0 (cdr entry)))
          (insert (format ", PS=%sx%s" (nth 0 item) (nth 1 item)))
          (when (or (>= (nth 2 item) 0) (>= (nth 3 item) 0))
            (insert (format ", XS=%sx%s" (nth 2 item) (nth 3 item))))
          (setq item (nth 1 (cdr entry)))
          (when (or (>= (nth 0 item) 0) (>= (nth 1 item) 0))
            (insert (format ", DS=%sx%s" (nth 0 item) (nth 1 item))))
          (insert "\n"))
         ((and (eq (nth 0 item) frame) (= (nth 1 item) 5))
          ;; Length 5 is an 'adjust_frame_size' item.
          (insert (format "%s (%s)" (nth 3 item) (nth 2 item)))
          (setq item (nth 0 (cdr entry)))
          (unless (and (= (nth 0 item) (nth 2 item))
                       (= (nth 1 item) (nth 3 item)))
            (insert (format ", TS=%sx%s~>%sx%s"
                            (nth 0 item) (nth 1 item) (nth 2 item) (nth 3 item))))
          (setq item (nth 1 (cdr entry)))
          (unless (and (= (nth 0 item) (nth 2 item))
                       (= (nth 1 item) (nth 3 item)))
            (insert (format ", TC=%sx%s~>%sx%s"
                            (nth 0 item) (nth 1 item) (nth 2 item) (nth 3 item))))
          (setq item (nth 2 (cdr entry)))
          (unless (and (= (nth 0 item) (nth 2 item))
                       (= (nth 1 item) (nth 3 item)))
            (insert (format ", NS=%sx%s~>%sx%s"
                            (nth 0 item) (nth 1 item) (nth 2 item) (nth 3 item))))
          (setq item (nth 3 (cdr entry)))
          (unless (and (= (nth 0 item) (nth 2 item))
                       (= (nth 1 item) (nth 3 item)))
            (insert (format ", IS=%sx%s~>%sx%s"
                            (nth 0 item) (nth 1 item) (nth 2 item) (nth 3 item))))
          (setq item (nth 4 (cdr entry)))
          (insert (format ", MS=%sx%s" (nth 0 item) (nth 1 item)))
          (when (nth 2 item) (insert " IH"))
          (when (nth 3 item) (insert " IV"))
          (insert "\n")))))))

(declare-function x-frame-edges "xfns.c" (&optional frame type))
(declare-function w32-frame-edges "w32fns.c" (&optional frame type))
(declare-function ns-frame-edges "nsfns.m" (&optional frame type))
(declare-function pgtk-frame-edges "pgtkfns.c" (&optional frame type))
(declare-function haiku-frame-edges "haikufns.c" (&optional frame type))
(declare-function android-frame-edges "androidfns.c" (&optional frame type))
(declare-function tty-frame-edges "term.c" (&optional frame type))

(defun frame-edges (&optional frame type)
  "Return coordinates of FRAME's edges.
FRAME must be a live frame and defaults to the selected one.  The
list returned has the form (LEFT TOP RIGHT BOTTOM) where all
values are in pixels relative to the origin - the position (0, 0)
- of FRAME's display.  For terminal frames all values are
relative to LEFT and TOP which are both zero.

Optional argument TYPE specifies the type of the edges.  TYPE
`outer-edges' means to return the outer edges of FRAME.  TYPE
`native-edges' (or nil) means to return the native edges of
FRAME.  TYPE `inner-edges' means to return the inner edges of
FRAME."
  (let* ((frame (window-normalize-frame frame))
	 (frame-type (framep-on-display frame)))
    (cond
     ((eq frame-type 'x)
      (x-frame-edges frame type))
     ((eq frame-type 'w32)
      (w32-frame-edges frame type))
     ((eq frame-type 'ns)
      (ns-frame-edges frame type))
     ((eq frame-type 'pgtk)
      (pgtk-frame-edges frame type))
     ((eq frame-type 'haiku)
      (haiku-frame-edges frame type))
     ((eq frame-type 'android)
      (android-frame-edges frame type))
     (t
      (tty-frame-edges frame type)))))

(declare-function w32-mouse-absolute-pixel-position "w32fns.c")
(declare-function x-mouse-absolute-pixel-position "xfns.c")
(declare-function ns-mouse-absolute-pixel-position "nsfns.m")
(declare-function pgtk-mouse-absolute-pixel-position "pgtkfns.c")
(declare-function haiku-mouse-absolute-pixel-position "haikufns.c")
(declare-function android-mouse-absolute-pixel-position "androidfns.c")

(defun mouse-absolute-pixel-position ()
  "Return absolute position of mouse cursor in pixels.
The position is returned as a cons cell (X . Y) of the
coordinates of the mouse cursor position in pixels relative to a
position (0, 0) of the selected frame's terminal."
  (let ((frame-type (framep-on-display)))
    (cond
     ((eq frame-type 'x)
      (x-mouse-absolute-pixel-position))
     ((eq frame-type 'w32)
      (w32-mouse-absolute-pixel-position))
     ((eq frame-type 'ns)
      (ns-mouse-absolute-pixel-position))
     ((eq frame-type 'pgtk)
      (pgtk-mouse-absolute-pixel-position))
     ((eq frame-type 'haiku)
      (haiku-mouse-absolute-pixel-position))
     ((eq frame-type 'android)
      (android-mouse-absolute-pixel-position))
     (t
      (cons 0 0)))))

(declare-function pgtk-set-mouse-absolute-pixel-position "pgtkfns.c" (x y))
(declare-function ns-set-mouse-absolute-pixel-position "nsfns.m" (x y))
(declare-function w32-set-mouse-absolute-pixel-position "w32fns.c" (x y))
(declare-function x-set-mouse-absolute-pixel-position "xfns.c" (x y))
(declare-function haiku-set-mouse-absolute-pixel-position "haikufns.c" (x y))
(declare-function android-set-mouse-absolute-pixel-position
                  "androidfns.c" (x y))

(defun set-mouse-absolute-pixel-position (x y)
  "Move mouse pointer to absolute pixel position (X, Y).
The coordinates X and Y are interpreted in pixels relative to a
position (0, 0) of the selected frame's terminal."
  (let ((frame-type (framep-on-display)))
    (cond
     ((eq frame-type 'pgtk)
      (pgtk-set-mouse-absolute-pixel-position x y))
     ((eq frame-type 'ns)
      (ns-set-mouse-absolute-pixel-position x y))
     ((eq frame-type 'x)
      (x-set-mouse-absolute-pixel-position x y))
     ((eq frame-type 'w32)
      (w32-set-mouse-absolute-pixel-position x y))
     ((eq frame-type 'haiku)
      (haiku-set-mouse-absolute-pixel-position x y))
     ((eq frame-type 'android)
      (android-set-mouse-absolute-pixel-position x y)))))

(defun frame-monitor-attributes (&optional frame)
  "Return the attributes of the physical monitor dominating FRAME.
If FRAME is omitted or nil, describe the currently selected frame.

A frame is dominated by a physical monitor when either the
largest area of the frame resides in the monitor, or the monitor
is the closest to the frame if the frame does not intersect any
physical monitors.

See `display-monitor-attributes-list' for the list of attribute
keys and their meanings."
  (or frame (setq frame (selected-frame)))
  (cl-loop for attributes in (display-monitor-attributes-list frame)
	   for frames = (cdr (assq 'frames attributes))
	   if (memq frame frames) return attributes
	   ;; On broken frames monitor attributes,
	   ;; fall back to the last monitor.
	   finally return attributes))

(defun frame-monitor-attribute (attribute &optional frame x y)
  "Return the value of ATTRIBUTE on FRAME's monitor.
If FRAME is omitted or nil, use currently selected frame.

By default, the current monitor is the physical monitor
dominating the selected frame.  A frame is dominated by a
physical monitor when either the largest area of the frame
resides in the monitor, or the monitor is the closest to the
frame if the frame does not intersect any physical monitors.

If X and Y are both numbers, then ignore the value of FRAME; the
monitor is determined to be the physical monitor that contains
the pixel coordinate (X, Y).

See `display-monitor-attributes-list' for the list of attribute
keys and their meanings."
  (if (and (numberp x)
           (numberp y))
      (cl-loop for monitor in (display-monitor-attributes-list)
               for geometry = (alist-get 'geometry monitor)
               for min-x = (pop geometry)
               for min-y = (pop geometry)
               for max-x = (+ min-x (pop geometry))
               for max-y = (+ min-y (car geometry))
               when (and (<= min-x x)
                         (< x max-x)
                         (<= min-y y)
                         (< y max-y))
               return (alist-get attribute monitor))
    (alist-get attribute (frame-monitor-attributes frame))))

(defun frame-monitor-geometry (&optional frame x y)
    "Return the geometry of FRAME's monitor.
FRAME can be a frame name, a terminal name, or a frame.
If FRAME is omitted or nil, use the currently selected frame.

By default, the current monitor is said to be the physical
monitor dominating the selected frame.  A frame is dominated by
a physical monitor when either the largest area of the frame resides
in the monitor, or the monitor is the closest to the frame if the
frame does not intersect any physical monitors.

If X and Y are both numbers, then ignore the value of FRAME; the
monitor is determined to be the physical monitor that contains
the pixel coordinate (X, Y).

See `display-monitor-attributes-list' for information on the
geometry attribute."
  (frame-monitor-attribute 'geometry frame x y))

(defun frame-monitor-workarea (&optional frame x y)
  "Return the workarea of FRAME's monitor.
FRAME can be a frame name, a terminal name, or a frame.
If FRAME is omitted or nil, use currently selected frame.

By default, the current monitor is said to be the physical
monitor dominating the selected frame.  A frame is dominated by
a physical monitor when either the largest area of the frame resides
in the monitor, or the monitor is the closest to the frame if the
frame does not intersect any physical monitors.

If X and Y are both numbers, then ignore the value of FRAME; the
monitor is determined to be the physical monitor that contains
the pixel coordinate (X, Y).

See `display-monitor-attributes-list' for information on the
workarea attribute."
  (frame-monitor-attribute 'workarea frame x y))

(declare-function x-frame-list-z-order "xfns.c" (&optional display))
(declare-function w32-frame-list-z-order "w32fns.c" (&optional display))
(declare-function ns-frame-list-z-order "nsfns.m" (&optional display))
;; TODO: implement this on PGTK.
;; (declare-function pgtk-frame-list-z-order "pgtkfns.c" (&optional display))
(declare-function haiku-frame-list-z-order "haikufns.c" (&optional display))
(declare-function android-frame-list-z-order "androidfns.c" (&optional display))
(declare-function tty-frame-list-z-order "term.c" (&optional display))

(defun frame-list-z-order (&optional display)
  "Return list of Emacs's frames, in Z (stacking) order.
The optional argument DISPLAY specifies which display to poll.
DISPLAY should be either a frame or a display name (a string).
If omitted or nil, that stands for the selected frame's display.

Frames are listed from topmost (first) to bottommost (last).  As
a special case, if DISPLAY is non-nil and specifies a live frame,
return the child frames of that frame in Z (stacking) order.

Return nil if DISPLAY contains no Emacs frame."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((eq frame-type 'x)
      (x-frame-list-z-order display))
     ((eq frame-type 'w32)
      (w32-frame-list-z-order display))
     ((eq frame-type 'ns)
      (ns-frame-list-z-order display))
     ((eq frame-type 'pgtk)
      ;; This is currently not supported on PGTK.
      ;; (pgtk-frame-list-z-order display)
      nil)
     ((eq frame-type 'haiku)
      (haiku-frame-list-z-order display))
     ((eq frame-type 'android)
      (android-frame-list-z-order display))
     (t
      (tty-frame-list-z-order display)))))

(declare-function x-frame-restack "xfns.c" (frame1 frame2 &optional above))
(declare-function w32-frame-restack "w32fns.c" (frame1 frame2 &optional above))
(declare-function ns-frame-restack "nsfns.m" (frame1 frame2 &optional above))
(declare-function pgtk-frame-restack "pgtkfns.c" (frame1 frame2 &optional above))
(declare-function haiku-frame-restack "haikufns.c" (frame1 frame2 &optional above))
(declare-function android-frame-restack "androidfns.c" (frame1 frame2
                                                               &optional above))
(declare-function tty-frame-restack "term.c" (frame1 frame2 &optional above))

(defun frame-restack (frame1 frame2 &optional above)
  "Restack FRAME1 below FRAME2.
This implies that if both frames are visible and the display
areas of these frames overlap, FRAME2 will (partially) obscure
FRAME1.  If the optional third argument ABOVE is non-nil, restack
FRAME1 above FRAME2.  This means that if both frames are visible
and the display areas of these frames overlap, FRAME1 will
\(partially) obscure FRAME2.

This may be thought of as an atomic action performed in two
steps: The first step removes FRAME1's window-system window from
the display.  The second step reinserts FRAME1's window
below (above if ABOVE is true) that of FRAME2.  Hence the
position of FRAME2 in its display's Z (stacking) order relative
to all other frames excluding FRAME1 remains unaltered.

Some window managers may refuse to restack windows."
  (if (and (frame-live-p frame1)
           (frame-live-p frame2)
           (equal (frame-parameter frame1 'display)
                  (frame-parameter frame2 'display)))
      (let ((frame-type (framep-on-display frame1)))
        (cond
         ((eq frame-type 'x)
          (x-frame-restack frame1 frame2 above))
         ((eq frame-type 'w32)
          (w32-frame-restack frame1 frame2 above))
         ((eq frame-type 'ns)
          (ns-frame-restack frame1 frame2 above))
         ((eq frame-type 'haiku)
          (haiku-frame-restack frame1 frame2 above))
         ((eq frame-type 'pgtk)
          (pgtk-frame-restack frame1 frame2 above))
         ((eq frame-type 'android)
          (android-frame-restack frame1 frame2 above))
         (t
          (tty-frame-restack frame1 frame2 above))))
    (error "Cannot restack frames")))

(defun frame-size-changed-p (&optional frame)
  "Return non-nil when the size of FRAME has changed.
More precisely, return non-nil when the inner width or height of
FRAME has changed since `window-size-change-functions' was run
for FRAME."
  (let* ((frame (window-normalize-frame frame))
         (root (frame-root-window frame))
         (mini (minibuffer-window frame))
         (mini-old-height 0)
         (mini-height 0))
    ;; FRAME's minibuffer window counts iff it's on FRAME and FRAME is
    ;; not a minibuffer-only frame.
    (when (and (eq (window-frame mini) frame) (not (eq mini root)))
      (setq mini-old-height (window-old-pixel-height mini))
      (setq mini-height (window-pixel-height mini)))
    ;; Return non-nil when either the width of the root or the sum of
    ;; the heights of root and minibuffer window changed.
    (or (/= (window-old-pixel-width root) (window-pixel-width root))
        (/= (+ (window-old-pixel-height root) mini-old-height)
            (+ (window-pixel-height root) mini-height)))))

;;;; Frame/display capabilities.

;; These functions should make the features they test explicit in
;; their names, so that when capabilities or the corresponding Emacs
;; features change, it will be easy to find all the tests for such
;; capabilities by a simple text search.  See more about the history
;; and the intent of these functions in
;; https://lists.gnu.org/archive/html/bug-gnu-emacs/2019-04/msg00004.html
;; or in https://debbugs.gnu.org/cgi/bugreport.cgi?bug=35058#17.

(declare-function msdos-mouse-p "dosfns.c")
(declare-function android-detect-mouse "androidfns.c")

(defun display-mouse-p (&optional display)
  "Return non-nil if DISPLAY has a mouse available.
DISPLAY can be a display name, a frame, or nil (meaning the selected
frame's display)."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((eq frame-type 'pc)
      (msdos-mouse-p))
     ((eq frame-type 'w32)
      (with-no-warnings
       (> w32-num-mouse-buttons 0)))
     ((memq frame-type '(x ns haiku pgtk))
      t)    ;; We assume X, NeXTstep, GTK, and Haiku *always* have a pointing device
     ((eq frame-type 'android)
      (android-detect-mouse))
     (t
      (or (and (featurep 'xt-mouse)
	       xterm-mouse-mode)
	  ;; t-mouse is distributed with the GPM package.  It doesn't have
	  ;; a toggle.
	  (featurep 't-mouse)
	  ;; No way to check whether a w32 console has a mouse, assume
	  ;; it always does, except in batch invocations.
          (and (not noninteractive)
	       (boundp 'w32-use-full-screen-buffer)))))))

(defun display-popup-menus-p (&optional display)
  "Return non-nil if popup menus are supported on DISPLAY.
DISPLAY can be a display name, a frame, or nil (meaning the selected
frame's display).
Support for popup menus requires that a suitable pointing device
be available."
  ;; Android menus work fine with touch screens as well, and one must
  ;; be present.
  (or (eq (framep-on-display display) 'android)
      (display-mouse-p display)))

(defun display-graphic-p (&optional display)
  "Return non-nil if DISPLAY is a graphic display.
Graphical displays are those which are capable of displaying several
frames and several different fonts at once.  This is true for displays
that use a window system such as X, and false for text-only terminals.
DISPLAY can be a display name, a frame, or nil (meaning the selected
frame's display)."
  (not (null (memq (framep-on-display display) '(x w32 ns pgtk haiku
                                                   android)))))

(defun display-images-p (&optional display)
  "Return non-nil if DISPLAY can display images.

DISPLAY can be a display name, a frame, or nil (meaning the selected
frame's display)."
  (and (display-graphic-p display)
       (fboundp 'image-mask-p)
       (fboundp 'image-size)))

(defalias 'display-blink-cursor-p #'display-graphic-p)
(defalias 'display-multi-frame-p #'display-graphic-p)
(defalias 'display-multi-font-p #'display-graphic-p)

(defcustom tty-select-active-regions nil
  "If non-nil, update PRIMARY window-system selection on text-mode frames.
On a text-mode terminal that supports setSelection command, if
this variable is non-nil, Emacs will set the PRIMARY selection
from the active region, according to `select-active-regions'.
This is currently supported only on xterm."
  :group 'frames
  :group 'killing
  :version "29.1"
  :type 'boolean)

(defun display-selections-p (&optional display)
  "Return non-nil if DISPLAY supports selections.
A selection is a way to transfer text or other data between programs
via special system buffers called `selection' or `clipboard'.
DISPLAY can be a display name, a frame, or nil (meaning the selected
frame's display)."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((eq frame-type 'pc)
      ;; MS-DOS frames support selections when Emacs runs inside
      ;; a Windows DOS Box.
      (with-no-warnings
       (not (null dos-windows-version))))
     ((memq frame-type '(x w32 ns pgtk))
      t)
     ((and tty-select-active-regions
           (terminal-parameter nil 'xterm--set-selection))
      t)
     (t
      nil))))

(defun display-symbol-keys-p (&optional display)
  "Return non-nil if DISPLAY supports symbol names as keys.
This means that, for example, DISPLAY can differentiate between
the keybinding RET and [return]."
  (let ((frame-type (framep-on-display display)))
    (or (memq frame-type '(x w32 ns pc pgtk haiku android))
        ;; MS-DOS and MS-Windows terminals have built-in support for
        ;; function (symbol) keys
        (memq system-type '(ms-dos windows-nt)))))

(declare-function x-display-screens "xfns.c" (&optional terminal))

(defun display-screens (&optional display)
  "Return the number of screens associated with DISPLAY.
DISPLAY should be either a frame or a display name (a string).
If DISPLAY is omitted or nil, it defaults to the selected frame's display."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((memq frame-type '(x w32 ns haiku pgtk android))
      (x-display-screens display))
     (t
      1))))

(declare-function x-display-pixel-height "xfns.c" (&optional terminal))
(declare-function tty-display-pixel-height "term.c" (&optional terminal))

(defun display-pixel-height (&optional display)
  "Return the height of DISPLAY's screen in pixels.
DISPLAY can be a display name or a frame.
If DISPLAY is omitted or nil, it defaults to the selected frame's display.

For character terminals, each character counts as a single pixel.

For graphical terminals, note that on \"multi-monitor\" setups this
refers to the pixel height for all physical monitors associated
with DISPLAY.  To get information for each physical monitor, use
`display-monitor-attributes-list'."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((memq frame-type '(x w32 ns haiku pgtk android))
      (x-display-pixel-height display))
     (t
      (tty-display-pixel-height display)))))

(declare-function x-display-pixel-width "xfns.c" (&optional terminal))
(declare-function tty-display-pixel-width "term.c" (&optional terminal))

(defun display-pixel-width (&optional display)
  "Return the width of DISPLAY's screen in pixels.
DISPLAY can be a display name or a frame.
If DISPLAY is omitted or nil, it defaults to the selected frame's display.

For character terminals, each character counts as a single pixel.

For graphical terminals, note that on \"multi-monitor\" setups this
refers to the pixel width for all physical monitors associated
with DISPLAY.  To get information for each physical monitor, use
`display-monitor-attributes-list'."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((memq frame-type '(x w32 ns haiku pgtk android))
      (x-display-pixel-width display))
     (t
      (tty-display-pixel-width display)))))

(defcustom display-mm-dimensions-alist nil
  "Alist for specifying screen dimensions in millimeters.
The functions `display-mm-height' and `display-mm-width' consult
this list before asking the system.

Each element has the form (DISPLAY . (WIDTH . HEIGHT)), e.g.
\(\":0.0\" . (287 . 215)).

If `display' is t, it specifies dimensions for all graphical displays
not explicitly specified."
  :version "22.1"
  :type '(alist :key-type (choice (string :tag "Display name")
				  (const :tag "Default" t))
		:value-type (cons :tag "Dimensions"
				  (integer :tag "Width")
				  (integer :tag "Height")))
  :group 'frames)

(declare-function x-display-mm-height "xfns.c" (&optional terminal))

(defun display-mm-height (&optional display)
  "Return the height of DISPLAY's screen in millimeters.
If the information is unavailable, this function returns nil.
DISPLAY can be a display name or a frame.
If DISPLAY is omitted or nil, it defaults to the selected frame's display.

You can override what the system thinks the result should be by
adding an entry to `display-mm-dimensions-alist'.

For graphical terminals, note that on \"multi-monitor\" setups this
refers to the height in millimeters for all physical monitors
associated with DISPLAY.  To get information for each physical
monitor, use `display-monitor-attributes-list'."
  (and (memq (framep-on-display display) '(x w32 ns haiku pgtk android))
       (or (cddr (assoc (or display (frame-parameter nil 'display))
			display-mm-dimensions-alist))
	   (cddr (assoc t display-mm-dimensions-alist))
	   (x-display-mm-height display))))

(declare-function x-display-mm-width "xfns.c" (&optional terminal))

(defun display-mm-width (&optional display)
  "Return the width of DISPLAY's screen in millimeters.
If the information is unavailable, this function returns nil.
DISPLAY can be a display name or a frame.
If DISPLAY is omitted or nil, it defaults to the selected frame's display.

You can override what the system thinks the result should be by
adding an entry to `display-mm-dimensions-alist'.

For graphical terminals, note that on \"multi-monitor\" setups this
refers to the width in millimeters for all physical monitors
associated with DISPLAY.  To get information for each physical
monitor, use `display-monitor-attributes-list'."
  (and (memq (framep-on-display display) '(x w32 ns haiku pgtk android))
       (or (cadr (assoc (or display (frame-parameter nil 'display))
			display-mm-dimensions-alist))
	   (cadr (assoc t display-mm-dimensions-alist))
	   (x-display-mm-width display))))

(declare-function x-display-backing-store "xfns.c" (&optional terminal))

;; In NS port, the return value may be `buffered', `retained', or
;; `non-retained'.  See src/nsfns.m.
(defun display-backing-store (&optional display)
  "Return the backing store capability of DISPLAY's screen.
The value may be `always', `when-mapped', `not-useful', or nil if
the question is inapplicable to a certain kind of display.
DISPLAY can be a display name or a frame.
If DISPLAY is omitted or nil, it defaults to the selected frame's display."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((memq frame-type '(x w32 ns haiku pgtk android))
      (x-display-backing-store display))
     (t
      'not-useful))))

(declare-function x-display-save-under "xfns.c" (&optional terminal))

(defun display-save-under (&optional display)
  "Return non-nil if DISPLAY's screen supports the SaveUnder feature.
DISPLAY can be a display name or a frame.
If DISPLAY is omitted or nil, it defaults to the selected frame's display."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((memq frame-type '(x w32 ns haiku pgtk android))
      (x-display-save-under display))
     (t
      'not-useful))))

(declare-function x-display-planes "xfns.c" (&optional terminal))

(defun display-planes (&optional display)
  "Return the number of planes supported by DISPLAY.
DISPLAY can be a display name or a frame.
If DISPLAY is omitted or nil, it defaults to the selected frame's display."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((memq frame-type '(x w32 ns haiku pgtk android))
      (x-display-planes display))
     ((eq frame-type 'pc)
      4)
     (t
      (logb (length (tty-color-alist)))))))

(declare-function x-display-color-cells "xfns.c" (&optional terminal))

(defun display-color-cells (&optional display)
  "Return the number of color cells supported by DISPLAY.
DISPLAY can be a display name or a frame.
If DISPLAY is omitted or nil, it defaults to the selected frame's display."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((memq frame-type '(x w32 ns haiku pgtk android))
      (x-display-color-cells display))
     ((eq frame-type 'pc)
      16)
     (t
      (tty-display-color-cells display)))))

(declare-function x-display-visual-class "xfns.c" (&optional terminal))

(defun display-visual-class (&optional display)
  "Return the visual class of DISPLAY.
The value is one of the symbols `static-gray', `gray-scale',
`static-color', `pseudo-color', `true-color', or `direct-color'.
DISPLAY can be a display name or a frame.
If DISPLAY is omitted or nil, it defaults to the selected frame's display."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((memq frame-type '(x w32 ns haiku pgtk android))
      (x-display-visual-class display))
     ((and (memq frame-type '(pc t))
	   (tty-display-color-p display))
      'static-color)
     (t
      'static-gray))))

(declare-function x-display-monitor-attributes-list "xfns.c"
		  (&optional terminal))
(declare-function w32-display-monitor-attributes-list "w32fns.c"
		  (&optional display))
(declare-function ns-display-monitor-attributes-list "nsfns.m"
		  (&optional terminal))
(declare-function pgtk-display-monitor-attributes-list "pgtkfns.c"
		  (&optional terminal))
(declare-function haiku-display-monitor-attributes-list "haikufns.c"
		  (&optional terminal))
(declare-function android-display-monitor-attributes-list "androidfns.c"
                  (&optional terminal))

(defun display-monitor-attributes-list (&optional display)
  "Return a list of physical monitor attributes on DISPLAY.
DISPLAY can be a display name, a terminal name, or a frame.
If DISPLAY is omitted or nil, it defaults to the selected frame's display.
Each element of the list represents the attributes of a physical
monitor.  The first element corresponds to the primary monitor.

The attributes for a physical monitor are represented as an alist
of attribute keys and values as follows:

 geometry -- Position and size in pixels in the form of (X Y WIDTH HEIGHT)
 workarea -- Position and size of the work area in pixels in the
	     form of (X Y WIDTH HEIGHT)
 mm-size  -- Width and height in millimeters in the form of
 	     (WIDTH HEIGHT)
 frames   -- List of frames dominated by the physical monitor
 scale-factor (*) -- Scale factor (float)
 name (*) -- Name of the physical monitor as a string
 source (*) -- Source of multi-monitor information as a string

where X, Y, WIDTH, and HEIGHT are integers.  X and Y are coordinates
of the top-left corner, and might be negative for monitors other than
the primary one.  Keys labeled with (*) are optional.

The \"work area\" is a measure of the \"usable\" display space.
It may be less than the total screen size, owing to space taken up
by window manager features (docks, taskbars, etc.).  The precise
details depend on the platform and environment.

The `source' attribute describes the source from which the
information was obtained.  On X, this may be one of: \"Gdk\",
\"XRandR 1.5\", \"XRandr\", \"Xinerama\", or \"fallback\".
If it is \"fallback\", it means Emacs was built without GTK
and without XrandR or Xinerama extensions, in which case the
information about multiple physical monitors will be provided
as if they all as a whole formed a single monitor.

A frame is dominated by a physical monitor when either the
largest area of the frame resides in the monitor, or the monitor
is the closest to the frame if the frame does not intersect any
physical monitors.  Every (non-tooltip) frame (including invisible ones)
in a graphical display is dominated by exactly one physical
monitor at a time, though it can span multiple (or no) physical
monitors."
  (let ((frame-type (framep-on-display display)))
    (cond
     ((eq frame-type 'x)
      (x-display-monitor-attributes-list display))
     ((eq frame-type 'w32)
      (w32-display-monitor-attributes-list display))
     ((eq frame-type 'ns)
      (ns-display-monitor-attributes-list display))
     ((eq frame-type 'pgtk)
      (pgtk-display-monitor-attributes-list display))
     ((eq frame-type 'haiku)
      (haiku-display-monitor-attributes-list display))
     ((eq frame-type 'android)
      (android-display-monitor-attributes-list display))
     (t
      (let ((geometry (list 0 0 (display-pixel-width display)
			    (display-pixel-height display))))
	`(((geometry . ,geometry)
	   (workarea . ,geometry)
	   (mm-size . (,(display-mm-width display)
		       ,(display-mm-height display)))
	   (frames . ,(frames-on-display-list display)))))))))

(declare-function x-device-class "term/x-win.el" (name))
(declare-function pgtk-device-class "term/pgtk-win.el" (name))

(defun device-class (frame name)
  "Return the class of the device NAME for an event generated on FRAME.
NAME is a string that can be the value of `last-event-device', or
nil.  FRAME is a window system frame, typically the value of
`last-event-frame' when `last-event-device' was set.  On some
window systems, it can also be a display name or a terminal.

The class of a device is one of the following symbols:

  `core-keyboard' means the device is a keyboard-like device, but
  any other characteristics are unknown.

  `core-pointer' means the device is a pointing device, but any
  other characteristics are unknown.

  `mouse' means the device is a computer mouse.

  `trackpoint' means the device is a joystick or trackpoint.

  `eraser' means the device is an eraser, which is typically the
  other end of a stylus on a graphics tablet.

  `pen' means the device is a stylus or some other similar
  device.

  `puck' means the device is a device similar to a mouse, but
  reports absolute coordinates.

  `power-button' means the device is a power button, volume
  button, or some similar control.

  `keyboard' means the device is a keyboard.

  `touchscreen' means the device is a touchscreen.

  `pad' means the device is a collection of buttons and rings and
  strips commonly found in drawing tablets.

  `touchpad' means the device is an indirect touch device, such
  as a touchpad.

  `piano' means the device is a piano, or some other kind of
  musical instrument.

  `test' means the device is used by the XTEST extension to
  report input.

It can also be nil, which means the class of the device could not
be determined.  Individual window systems may also return other
symbols."
  (let ((frame-type (framep-on-display frame)))
    (cond ((eq frame-type 'x)
           (x-device-class name))
          ((eq frame-type 'pgtk)
           (pgtk-device-class name))
          (t (cond
              ((not name) nil)
              ((string= name "Virtual core pointer")
               'core-pointer)
              ((string= name "Virtual core keyboard")
               'core-keyboard))))))


;;;; On-screen keyboard management.

(declare-function android-toggle-on-screen-keyboard "androidfns.c")

(defun frame-toggle-on-screen-keyboard (frame hide)
  "Display or hide the on-screen keyboard.
On systems with an on-screen keyboard, display the on screen
keyboard on behalf of the frame FRAME if HIDE is nil.  Else, hide
the on screen keyboard.

Return whether or not the on screen keyboard may have been
displayed; that is, return t on systems with an on screen
keyboard, and nil on those without.

FRAME must already have the input focus for this to work
 reliably."
  (let ((frame-type (framep-on-display frame)))
    (cond ((eq frame-type 'android)
           (android-toggle-on-screen-keyboard frame hide) t)
          (t nil))))


;;;; Frame geometry values

(defun frame-geom-value-cons (type value &optional frame)
  "Return equivalent geometry value for FRAME as a cons with car `+'.
A geometry value equivalent to VALUE for FRAME is returned,
where the value is a cons with car `+', not numeric.
TYPE is the car of the original geometry spec (TYPE . VALUE).
   It is `top' or `left', depending on which edge VALUE is related to.
VALUE is the cdr of a frame geometry spec: (left/top . VALUE).
If VALUE is a number, then it is converted to a cons value, perhaps
   relative to the opposite frame edge from that in the original spec.
FRAME defaults to the selected frame.

Examples (measures in pixels) -
 Assuming display height/width=1024, frame height/width=600:
 300 inside display edge:                   300  => (+  300)
                                        (+  300) => (+  300)
 300 inside opposite display edge:      (-  300) => (+  124)
                                           -300  => (+  124)
 300 beyond display edge
  (= 724 inside opposite display edge): (+ -300) => (+ -300)
 300 beyond display edge
  (= 724 inside opposite display edge): (- -300) => (+  724)

In the 3rd, 4th, and 6th examples, the returned value is relative to
the opposite frame edge from the edge indicated in the input spec."
  (cond ((and (consp value) (eq '+ (car value))) ; e.g. (+ 300), (+ -300)
         value)
        ((natnump value) (list '+ value)) ; e.g. 300 => (+ 300)
        (t                              ; e.g. -300, (- 300), (- -300)
         (list '+ (- (if (eq 'left type) ; => (+ 124), (+ 124), (+ 724)
                         (x-display-pixel-width)
                       (x-display-pixel-height))
                     (if (integerp value) (- value) (cadr value))
                     (if (eq 'left type)
                         (frame-pixel-width frame)
                       (frame-pixel-height frame)))))))

(defun frame-geom-spec-cons (spec &optional frame)
  "Return equivalent geometry spec for FRAME as a cons with car `+'.
A geometry specification equivalent to SPEC for FRAME is returned,
where the value is a cons with car `+', not numeric.
SPEC is a frame geometry spec: (left . VALUE) or (top . VALUE).
If VALUE is a number, then it is converted to a cons value, perhaps
relative to the opposite frame edge from that in the original spec.
FRAME defaults to the selected frame.

Examples (measures in pixels) -
 Assuming display height=1024, frame height=600:
 top 300 below display top:               (top .  300) => (top +  300)
                                          (top +  300) => (top +  300)
 bottom 300 above display bottom:         (top -  300) => (top +  124)
                                          (top . -300) => (top +  124)
 top 300 above display top
  (= bottom 724 above display bottom):    (top + -300) => (top + -300)
 bottom 300 below display bottom
  (= top 724 below display top):          (top - -300) => (top +  724)

In the 3rd, 4th, and 6th examples, the returned value is relative to
the opposite frame edge from the edge indicated in the input spec."
  (cons (car spec) (frame-geom-value-cons (car spec) (cdr spec) frame)))

(defun delete-other-frames (&optional frame iconify)
  "Delete all frames on FRAME's terminal, except FRAME.
If FRAME uses another frame's minibuffer, the minibuffer frame is
left untouched.  Do not delete any of FRAME's child frames.  If
FRAME is a child frame, delete its siblings only.  FRAME must be
a live frame and defaults to the selected one.
If the prefix arg ICONIFY is non-nil, just iconify the frames rather than
deleting them."
  (interactive "i\nP")
  (setq frame (window-normalize-frame frame))
  (let ((minibuffer-frame (window-frame (minibuffer-window frame)))
	(terminal (frame-terminal frame))
        (parent (frame-parent frame))
	(frames (frame-list)))
    ;; In a first round consider minibuffer-less frames only.
    (dolist (this frames)
      (unless (or (eq this frame)
		  (eq this minibuffer-frame)
		  (not (eq (frame-terminal this) terminal))
		  (eq (window-frame (minibuffer-window this)) this)
                  ;; When FRAME is a child frame, delete its siblings
                  ;; only.
                  (and parent (not (eq (frame-parent this) parent)))
                  ;; Do not delete frame descending from FRAME.
                  (frame-ancestor-p frame this))
        (if iconify (iconify-frame this) (delete-frame this))))
    ;; In a second round consider all remaining frames.
    (dolist (this frames)
      (unless (or (eq this frame)
		  (eq this minibuffer-frame)
		  (not (eq (frame-terminal this) terminal))
                  ;; When FRAME is a child frame, delete its siblings
                  ;; only.
                  (and parent (not (eq (frame-parent this) parent)))
                  ;; Do not delete frame descending from FRAME.
                  (frame-ancestor-p frame this))
        (if iconify (iconify-frame this) (delete-frame this))))))

(defvar undelete-frame--deleted-frames nil
  "Internal variable used by `undelete-frame--save-deleted-frame'.")

(defun undelete-frame--save-deleted-frame (frame)
  "Save the configuration of frames deleted with `delete-frame'.
Only the 16 most recently deleted frames are saved."
  (when (and after-init-time (frame-live-p frame))
    (setq undelete-frame--deleted-frames
          (cons
           (list
            (display-graphic-p)
            (seq-remove
             (lambda (elem)
               (or (memq (car elem) frame-internal-parameters)
                   ;; When the daemon is started from a graphical
                   ;; environment, TTY frames have a 'display' parameter set
                   ;; to the value of $DISPLAY (see the note in
                   ;; `server--on-display-p').  Do not store that parameter
                   ;; in the frame data, otherwise `undelete-frame' attempts
                   ;; to restore a graphical frame.
                   (and (eq (car elem) 'display) (not (display-graphic-p)))))
             (frame-parameters frame))
            (window-state-get (frame-root-window frame)))
           undelete-frame--deleted-frames))
    (if (> (length undelete-frame--deleted-frames) 16)
        (setq undelete-frame--deleted-frames
              (butlast undelete-frame--deleted-frames)))))

(define-minor-mode undelete-frame-mode
  "Enable the `undelete-frame' command."
  :group 'frames
  :global t
  (if undelete-frame-mode
      (add-hook 'delete-frame-functions
                #'undelete-frame--save-deleted-frame -75)
    (remove-hook 'delete-frame-functions
                 #'undelete-frame--save-deleted-frame)
    (setq undelete-frame--deleted-frames nil)))

(defun undelete-frame (&optional arg)
  "Undelete a frame deleted with `delete-frame'.
Without a prefix argument, undelete the most recently deleted
frame.
With a numerical prefix argument ARG between 1 and 16, where 1 is
most recently deleted frame, undelete the ARGth deleted frame.
When called from Lisp, returns the new frame."
  (interactive "P")
  (if (not undelete-frame-mode)
      (user-error "Undelete-Frame mode is disabled")
    (if (consp arg)
        (user-error "Missing deleted frame number argument")
      (let* ((number (pcase arg ('nil 1) ('- -1) (_ arg)))
             (frame-data (nth (1- number) undelete-frame--deleted-frames))
             (graphic (display-graphic-p)))
        (if (not (<= 1 number 16))
            (user-error "%d is not a valid deleted frame number argument"
                        number)
          (if (not frame-data)
              (user-error "No deleted frame with number %d" number)
            (if (not (eq graphic (nth 0 frame-data)))
                (user-error
                 "Cannot undelete a %s display frame on a %s display"
                 (if graphic "non-graphic" "graphic")
                 (if graphic "graphic" "non-graphic"))
              (setq undelete-frame--deleted-frames
                    (delq frame-data undelete-frame--deleted-frames))
              (let* ((default-frame-alist (nth 1 frame-data))
                     (frame (make-frame)))
                (window-state-put (nth 2 frame-data) (frame-root-window frame) 'safe)
                (select-frame-set-input-focus frame)
                frame))))))))

;;; Window dividers.
(defgroup window-divider nil
  "Window dividers."
  :version "25.1"
  :group 'frames
  :group 'windows)

(defcustom window-divider-default-places 'right-only
  "Default positions of window dividers.
Possible values are `bottom-only' (dividers on the bottom of each
window only), `right-only' (dividers on the right of each window
only), and t (dividers on the bottom and on the right of each
window).  The default is `right-only'.

The value takes effect if and only if dividers are enabled by
`window-divider-mode'.

To position dividers on frames individually, use the frame
parameters `bottom-divider-width' and `right-divider-width'."
  :type '(choice (const :tag "Bottom only" bottom-only)
		 (const :tag "Right only" right-only)
		 (const :tag "Bottom and right" t))
  :initialize #'custom-initialize-default
  :set (lambda (symbol value)
	 (set-default symbol value)
         (when window-divider-mode
           (window-divider-mode-apply t)))
  :version "25.1")

(defun window-divider-width-valid-p (value)
  "Return non-nil if VALUE is a positive number."
  (and (numberp value) (> value 0)))

(defcustom window-divider-default-bottom-width 6
  "Default width of dividers on bottom of windows.
The value must be a positive integer and takes effect when bottom
dividers are displayed by `window-divider-mode'.

To adjust bottom dividers for frames individually, use the frame
parameter `bottom-divider-width'."
  :type '(restricted-sexp
          :tag "Default width of bottom dividers"
          :match-alternatives (window-divider-width-valid-p))
  :initialize #'custom-initialize-default
  :set (lambda (symbol value)
	 (set-default symbol value)
         (when window-divider-mode
           (window-divider-mode-apply t)))
  :version "25.1")

(defcustom window-divider-default-right-width 6
  "Default width of dividers on the right of windows.
The value must be a positive integer and takes effect when right
dividers are displayed by `window-divider-mode'.

To adjust right dividers for frames individually, use the frame
parameter `right-divider-width'."
  :type '(restricted-sexp
          :tag "Default width of right dividers"
          :match-alternatives (window-divider-width-valid-p))
  :initialize #'custom-initialize-default
  :set (lambda (symbol value)
	 (set-default symbol value)
         (when window-divider-mode
	   (window-divider-mode-apply t)))
  :version "25.1")

(defun window-divider-mode-apply (enable)
  "Apply window divider places and widths to all frames.
If ENABLE is nil, apply default places and widths.  Else reset
all divider widths to zero."
  (let ((bottom (if (and enable
                         (memq window-divider-default-places
                               '(bottom-only t)))
                    window-divider-default-bottom-width
                  0))
        (right (if (and enable
                        (memq window-divider-default-places
                              '(right-only t)))
                   window-divider-default-right-width
                 0)))
    (modify-all-frames-parameters
     (list (cons 'bottom-divider-width bottom)
           (cons 'right-divider-width right)))
    (setq default-frame-alist
          (assq-delete-all
           'bottom-divider-width default-frame-alist))
    (setq default-frame-alist
          (assq-delete-all
           'right-divider-width default-frame-alist))
    (when (> bottom 0)
      (setq default-frame-alist
            (cons
             (cons 'bottom-divider-width bottom)
             default-frame-alist)))
    (when (> right 0)
      (setq default-frame-alist
            (cons
             (cons 'right-divider-width right)
             default-frame-alist)))))

(define-minor-mode window-divider-mode
  "Display dividers between windows (Window Divider mode).

The option `window-divider-default-places' specifies on which
side of a window dividers are displayed.  The options
`window-divider-default-bottom-width' and
`window-divider-default-right-width' specify their respective
widths."
  :group 'window-divider
  :global t
  (window-divider-mode-apply window-divider-mode))

;; Blinking cursor

(defvar blink-cursor-idle-timer nil
  "Timer started after `blink-cursor-delay' seconds of Emacs idle time.
The function `blink-cursor-start' is called when the timer fires.")

(defvar blink-cursor-timer nil
  "Timer started from `blink-cursor-start'.
This timer calls `blink-cursor-timer-function' every
`blink-cursor-interval' seconds.")

(defgroup cursor nil
  "Displaying text cursors."
  :version "21.1"
  :group 'frames)

(defcustom blink-cursor-delay 0.5
  "Seconds of idle time before the first blink of the cursor.
Values smaller than 0.2 sec are treated as 0.2 sec."
  :type 'number
  :group 'cursor
  :set (lambda (symbol value)
         (set-default symbol value)
         (when blink-cursor-idle-timer (blink-cursor--start-idle-timer))))

(defcustom blink-cursor-interval 0.5
  "Length of cursor blink interval in seconds."
  :type 'number
  :group 'cursor
  :set (lambda (symbol value)
         (set-default symbol value)
         (when blink-cursor-timer (blink-cursor--start-timer))))

(defcustom blink-cursor-blinks 10
  "How many times to blink before using a solid cursor on NS, X, and MS-Windows.
Use 0 or negative value to blink forever."
  :version "24.4"
  :type 'integer
  :group 'cursor)

(defvar blink-cursor-blinks-done 1
  "Number of blinks done since we started blinking on NS, X, and MS-Windows.")

(defun blink-cursor--start-idle-timer ()
  "Start the `blink-cursor-idle-timer'."
  (when blink-cursor-idle-timer (cancel-timer blink-cursor-idle-timer))
  (setq blink-cursor-idle-timer
        ;; The 0.2 sec limitation from below is to avoid erratic
        ;; behavior (or downright failure to display the cursor
        ;; during command execution) if they set blink-cursor-delay
        ;; to a very small or even zero value.
        (run-with-idle-timer (max 0.2 blink-cursor-delay)
                             :repeat #'blink-cursor-start)))

(defun blink-cursor--start-timer ()
  "Start the `blink-cursor-timer'."
  (when blink-cursor-timer (cancel-timer blink-cursor-timer))
  (setq blink-cursor-timer
        (run-with-timer blink-cursor-interval blink-cursor-interval
                        #'blink-cursor-timer-function)))

(defun blink-cursor-start ()
  "Timer function called from the timer `blink-cursor-idle-timer'.
This starts the timer `blink-cursor-timer', which makes the cursor blink
if appropriate.  It also arranges to cancel that timer when the next
command starts, by installing a pre-command hook."
  (cond
   ((null blink-cursor-mode) (blink-cursor-mode -1))
   ((null blink-cursor-timer)
    ;; Set up the timer first, so that if this signals an error,
    ;; blink-cursor-end is not added to pre-command-hook.
    (setq blink-cursor-blinks-done 1)
    (blink-cursor--start-timer)
    (add-hook 'pre-command-hook #'blink-cursor-end)
    (internal-show-cursor nil nil))))

(defun blink-cursor-timer-function ()
  "Timer function of timer `blink-cursor-timer'."
  (internal-show-cursor nil (not (internal-show-cursor-p)))
  ;; Suspend counting blinks when the w32 menu-bar menu is displayed,
  ;; since otherwise menu tooltips will behave erratically.
  (or (and (fboundp 'w32--menu-bar-in-use)
	   (w32--menu-bar-in-use))
      (setq blink-cursor-blinks-done (1+ blink-cursor-blinks-done)))
  ;; Each blink is two calls to this function.
  (when (and (> blink-cursor-blinks 0)
             (<= (* 2 blink-cursor-blinks) blink-cursor-blinks-done))
    (blink-cursor-suspend)
    (add-hook 'post-command-hook #'blink-cursor-check)))

(defun blink-cursor-end ()
  "Stop cursor blinking.
This is installed as a pre-command hook by `blink-cursor-start'.
When run, it cancels the timer `blink-cursor-timer' and removes
itself as a pre-command hook."
  (remove-hook 'pre-command-hook #'blink-cursor-end)
  (internal-show-cursor nil t)
  (when blink-cursor-timer
    (cancel-timer blink-cursor-timer)
    (setq blink-cursor-timer nil)))

(defun blink-cursor-suspend ()
  "Suspend cursor blinking.
This is called when no frame has focus and timers can be suspended.
Timers are restarted by `blink-cursor-check', which is called when a
frame receives focus."
  (blink-cursor-end)
  (when blink-cursor-idle-timer
    (cancel-timer blink-cursor-idle-timer)
    (setq blink-cursor-idle-timer nil)))

(defun blink-cursor--should-blink ()
  "Determine whether we should be blinking.
Returns whether we have any focused non-TTY frame."
  (and blink-cursor-mode
       (let ((frame-list (frame-list))
             (any-graphical-focused nil))
         (while frame-list
           (let ((frame (pop frame-list)))
             (when (and (display-graphic-p frame) (frame-focus-state frame))
               (setf any-graphical-focused t)
               (setf frame-list nil))))
         any-graphical-focused)))

(defun blink-cursor-check ()
  "Check if cursor blinking shall be restarted.
This is done when a frame gets focus.  Blink timers may be
stopped by `blink-cursor-suspend'.  Internally calls
`blink-cursor--should-blink' and returns its result."
  (let ((should-blink (blink-cursor--should-blink)))
    (when (and should-blink (not blink-cursor-idle-timer))
      (remove-hook 'post-command-hook #'blink-cursor-check)
      (blink-cursor--start-idle-timer))
    should-blink))

(defun blink-cursor--rescan-frames (&optional _ign)
  "Called when the set of focused frames changes or when we delete a frame."
  (unless (blink-cursor-check)
    (blink-cursor-suspend)))

(define-minor-mode blink-cursor-mode
  "Toggle cursor blinking (Blink Cursor mode).

If the value of `blink-cursor-blinks' is positive (10 by default),
the cursor stops blinking after that number of blinks, if Emacs
gets no input during that time.

See also `blink-cursor-interval' and `blink-cursor-delay'.

This command is effective only on graphical frames.  On text-only
terminals, cursor blinking is controlled by the terminal."
  :init-value (not (or noninteractive
		       no-blinking-cursor
		       (eq system-type 'ms-dos)))
  :initialize #'custom-initialize-delay
  :group 'cursor
  :global t
  (blink-cursor-suspend)
  (remove-hook 'after-delete-frame-functions #'blink-cursor--rescan-frames)
  (remove-function after-focus-change-function #'blink-cursor--rescan-frames)
  (when blink-cursor-mode
    (add-function :after after-focus-change-function
                  #'blink-cursor--rescan-frames)
    (add-hook 'after-delete-frame-functions #'blink-cursor--rescan-frames)
    (blink-cursor-check)))


;; Frame maximization/fullscreen

(defun toggle-frame-maximized (&optional frame)
  "Toggle maximization state of FRAME.
Maximize selected frame or un-maximize if it is already maximized.

If the frame is in fullscreen state, don't change its state, but
set the frame's `fullscreen-restore' parameter to `maximized', so
the frame will be maximized after disabling fullscreen state.

If you wish to hide the title bar when the frame is maximized, you
can add something like the following to your init file:

  (add-hook \\='window-size-change-functions
            #\\='frame-hide-title-bar-when-maximized)

Note that with some window managers you may have to set
`frame-resize-pixelwise' to non-nil in order to make a frame
appear truly maximized.  In addition, you may have to set
`x-frame-normalize-before-maximize' in order to enable
transitions from one fullscreen state to another.

See also `toggle-frame-fullscreen'."
  (interactive)
  (let ((fullscreen (frame-parameter frame 'fullscreen)))
    (cond
     ((memq fullscreen '(fullscreen fullboth))
      (set-frame-parameter frame 'fullscreen-restore 'maximized))
     ((eq fullscreen 'maximized)
      (set-frame-parameter frame 'fullscreen nil))
     (t
      (set-frame-parameter frame 'fullscreen 'maximized)))))

(defun toggle-frame-fullscreen (&optional frame)
  "Toggle fullscreen state of FRAME.
Make selected frame fullscreen or restore its previous size
if it is already fullscreen.

Before making the frame fullscreen remember the current value of
the frame's `fullscreen' parameter in the `fullscreen-restore'
parameter of the frame.  That value is used to restore the
frame's fullscreen state when toggling fullscreen the next time.

Note that with some window managers you may have to set
`frame-resize-pixelwise' to non-nil in order to make a frame
appear truly fullscreen.  In addition, you may have to set
`x-frame-normalize-before-maximize' in order to enable
transitions from one fullscreen state to another.

See also `toggle-frame-maximized'."
  (interactive)
  (let ((fullscreen (frame-parameter frame 'fullscreen)))
    (if (memq fullscreen '(fullscreen fullboth))
	(let ((fullscreen-restore (frame-parameter frame 'fullscreen-restore)))
	  (if (memq fullscreen-restore '(maximized fullheight fullwidth))
	      (set-frame-parameter frame 'fullscreen fullscreen-restore)
	    (set-frame-parameter frame 'fullscreen nil)))
      (modify-frame-parameters
       frame `((fullscreen . fullboth) (fullscreen-restore . ,fullscreen))))))


;;;; Key bindings

(define-key ctl-x-5-map "2" #'make-frame-command)
(define-key ctl-x-5-map "1" #'delete-other-frames)
(define-key ctl-x-5-map "0" #'delete-frame)
(define-key ctl-x-5-map "o" #'other-frame)
(define-key ctl-x-5-map "5" #'other-frame-prefix)
(define-key ctl-x-5-map "c" #'clone-frame)
(define-key ctl-x-5-map "u" #'undelete-frame)
(define-key global-map [f11] #'toggle-frame-fullscreen)
(define-key global-map [(meta f10)] #'toggle-frame-maximized)
(define-key esc-map    [f10]        #'toggle-frame-maximized)


;; Misc.

(make-variable-buffer-local 'show-trailing-whitespace)

(defun set-frame-property--interactive (prompt number)
  "Get a value for `set-frame-width' or `set-frame-height', prompting with PROMPT.
Offer NUMBER as default value, if it is a natural number."
  (if (and current-prefix-arg (not (consp current-prefix-arg)))
      (list (selected-frame) (prefix-numeric-value current-prefix-arg))
    (let ((default (and (natnump number) number)))
      (list (selected-frame) (read-number prompt default)))))

;; Variables whose change of value should trigger redisplay of the
;; current buffer.
;; To test whether a given variable needs to be added to this list,
;; write a simple interactive function that changes the variable's
;; value and bind that function to a simple key, like F5.  If typing
;; F5 then produces the correct effect, the variable doesn't need
;; to be in this list; otherwise, it does.
(mapc (lambda (var)
        ;; Using symbol-function here tells the watcher machinery to
        ;; call the C function set-buffer-redisplay directly, thus
        ;; avoiding a potential GC.  This isn't strictly necessary,
        ;; but it's a nice way to exercise the direct subr-calling
        ;; machinery.
        (add-variable-watcher var (symbol-function 'set-buffer-redisplay)))
      '(line-spacing
        overline-margin
        line-prefix
        wrap-prefix
        truncate-lines
        mode-line-format
        header-line-format
        tab-line-format
        display-line-numbers
        display-line-numbers-width
        display-line-numbers-current-absolute
        display-line-numbers-widen
        display-line-numbers-major-tick
        display-line-numbers-minor-tick
        display-line-numbers-offset
        display-fill-column-indicator
        display-fill-column-indicator-column
        display-fill-column-indicator-character
        bidi-paragraph-direction
        bidi-display-reordering
        bidi-inhibit-bpa))

(defun frame-hide-title-bar-when-maximized (frame)
  "Hide the title bar if FRAME is maximized.
If FRAME isn't maximized, show the title bar."
  (set-frame-parameter
   frame 'undecorated
   (eq (alist-get 'fullscreen (frame-parameters frame)) 'maximized)))

(define-obsolete-function-alias 'frame--current-backround-mode
  #'frame--current-background-mode "30.1")

(provide 'frame)

;;; frame.el ends here
