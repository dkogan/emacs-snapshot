;;; userlock.el --- handle file access contention between multiple users  -*- lexical-binding: t -*-

;; Copyright (C) 1985-1986, 2001-2025 Free Software Foundation, Inc.

;; Author: Richard King
;; (according to authors.el)
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

;; This file is autoloaded to handle certain conditions
;; detected by the file-locking code within Emacs.
;; The two entry points are `ask-user-about-lock' and
;; `ask-user-about-supersession-threat'.

;;; Code:

(eval-when-compile (require 'cl-lib))

;;;###autoload
(put 'create-lockfiles 'safe-local-variable 'booleanp)

(define-error 'file-locked "File is locked" 'file-error)

;;;###autoload
(defun ask-user-about-lock (file opponent)
  "Ask user what to do when he wants to edit FILE but it is locked by OPPONENT.
This function has a choice of three things to do:
  do (signal \\='file-locked (list FILE OPPONENT))
    to refrain from editing the file
  return t (grab the lock on the file)
  return nil (edit the file even though it is locked).
You can redefine this function to choose among those three alternatives
in any way you like."
  (discard-input)
  (save-window-excursion
    (let (answer short-opponent short-file)
      (setq short-file
	    (if (> (length file) 22)
		(concat "..." (substring file (- (length file) 22)))
	      file))
      (setq short-opponent
	    (if (> (length opponent) 25)
		(save-match-data
		  (string-match " (pid [0-9]+)" opponent)
		  (concat (substring opponent 0 13) "..."
			  (match-string 0 opponent)))
	      opponent))
      (while (null answer)
	(when noninteractive
          (signal 'file-locked (list file opponent "Cannot resolve lock conflict in batch mode")))
        (message (substitute-command-keys
                  "%s locked by %s: (\\`s', \\`q', \\`p', \\`?')? ")
                 short-file short-opponent)
	(let ((tem (let ((inhibit-quit t)
			 (cursor-in-echo-area t))
		     (prog1 (downcase (read-char))
		            (setq quit-flag nil)))))
	  (if (= tem help-char)
	      (ask-user-about-lock-help)
	    (setq answer (assoc tem '((?s . t)
				      (?q . yield)
				      (?\C-g . yield)
				      (?p . nil)
				      (?? . help))))
	    (cond ((null answer)
		   (beep)
                   ;; FIXME: Why do we use "?" here and "C-h" below?
                   (message (substitute-command-keys
                             "Please type \\`q', \\`s', or \\`p'; or \\`?' for help"))
		   (sit-for 3))
		  ((eq (cdr answer) 'help)
		   (ask-user-about-lock-help)
		   (setq answer nil))
		  ((eq (cdr answer) 'yield)
		   (signal 'file-locked (list file opponent)))))))
      (cdr answer))))

(defun ask-user-about-lock-help ()
  (with-output-to-temp-buffer "*Help*"
    (with-current-buffer standard-output
      (insert
       (substitute-command-keys
        "It has been detected that you want to modify a file that someone else has
already started modifying in Emacs.

You can <\\`s'>teal the file; the other user becomes the
  intruder if (s)he ever unmodifies the file and then changes it again.
You can <\\`p'>roceed; you edit at your own (and the other user's) risk.
You can <\\`q'>uit; don't modify this file."))
      (help-mode))))

(define-error 'file-supersession nil 'file-error)

(defun userlock--check-content-unchanged (filename)
  (with-demoted-errors "Unchanged content check: %S"
    ;; Even tho we receive `filename', we know that `filename' refers
    ;; to the current buffer's file.
    (cl-assert (or (null buffer-file-truename) ; temporary buffer
                   (equal (expand-file-name filename)
                          (expand-file-name buffer-file-truename))))
    ;; Note: rather than read the file and compare to the buffer, we could save
    ;; the buffer and compare to the file, but for encrypted data this
    ;; wouldn't work well (and would risk exposing the data).
    (save-restriction
      (widen)
      (let ((buf (current-buffer))
            (cs buffer-file-coding-system)
            (start (point-min))
            (end (point-max)))
        ;; FIXME: To avoid a slow `insert-file-contents' on large or
        ;; remote files, it'd be good to include file size in the
        ;; "visited-modtime" check.
        (when (with-temp-buffer
                (let ((coding-system-for-read cs)
                      (non-essential t))
                  (insert-file-contents filename))
                (when (= (buffer-size) (- end start)) ;Minor optimization.
                  (= 0 (let ((case-fold-search nil))
                         (compare-buffer-substrings
                          buf start end
                          (current-buffer) (point-min) (point-max))))))
          ;; We know that some buffer visits FILENAME, because our
          ;; caller (see lock_file) verified that.  Thus, we set the
          ;; modtime in that buffer, to cater to use case where the
          ;; file is about to be written to from some buffer that
          ;; doesn't visit any file, like a temporary buffer.
          (let ((buf (get-file-buffer (file-truename filename))))
            (when buf  ; If we cannot find the visiting buffer, punt.
              (with-current-buffer buf
                (set-visited-file-modtime))))
          'unchanged)))))

;;;###autoload
(defun userlock--ask-user-about-supersession-threat (filename)
  ;; Called from filelock.c.
  (unless (userlock--check-content-unchanged filename)
    (ask-user-about-supersession-threat filename)))

;;;###autoload
(defun ask-user-about-supersession-threat (filename)
  "Ask a user who is about to modify an obsolete buffer what to do.
This function has two choices: it can return, in which case the modification
of the buffer will proceed, or it can (signal \\='file-supersession (file)),
in which case the proposed buffer modification will not be made.

You can rewrite this to use any criterion you like to choose which one to do.
The buffer in question is current when this function is called."
  (discard-input)
  (save-window-excursion
    (let ((prompt
           ;; FIXME: Why do we use "C-h" here and "?" above?
           (format (substitute-command-keys
                    "%s changed on disk; \
really edit the buffer? (\\`y', \\`n', \\`r' or \\`C-h') ")
                   (file-name-nondirectory filename)))
	  (choices '(?y ?n ?r ?? ?\C-h))
	  answer)
      (when noninteractive
	(message "%s" prompt)
	(error "Cannot resolve conflict in batch mode"))
      (while (null answer)
	(setq answer (read-char-choice prompt choices))
	(cond ((memq answer '(?? ?\C-h))
	       (ask-user-about-supersession-help)
	       (setq answer nil))
	      ((eq answer ?r)
	       ;; Ask for confirmation if buffer modified
	       (revert-buffer nil (not (buffer-modified-p)))
	       (signal 'file-supersession
		       (list "File reverted" filename)))
	      ((eq answer ?n)
	       (signal 'file-supersession
		       (list "File changed on disk" filename)))
	      ((eq answer ?y))
	      (t (setq answer nil))))
      (message
       "File on disk now will become a backup file if you save these changes.")
      (setq buffer-backed-up nil))))

(defun ask-user-about-supersession-help ()
  (with-output-to-temp-buffer "*Help*"
    (with-current-buffer standard-output
      (insert
       (substitute-command-keys
        "You want to modify a buffer whose disk file has changed
since you last read it in or saved it with this buffer.

If you say \\`y' to go ahead and modify this buffer,
you risk ruining the work of whoever rewrote the file.
If you say \\`r' to revert, the contents of the buffer are refreshed
from the file on disk.
If you say \\`n', the change you started to make will be aborted.

Usually, you should type \\`r' to get the latest version of the
file, then make the change again."))
      (help-mode))))

;;;###autoload
(defun userlock--handle-unlock-error (error)
  "Report an ERROR that occurred while unlocking a file."
  (when create-lockfiles
    (display-warning
     '(unlock-file)
     ;; There is no need to explain that this is an unlock error because
     ;; ERROR is a `file-error' condition, which explains this.
     (message "%s, ignored" (error-message-string error))
     :warning)))

;;; userlock.el ends here
