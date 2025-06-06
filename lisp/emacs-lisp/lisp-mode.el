;;; lisp-mode.el --- Lisp mode, and its idiosyncratic commands  -*- lexical-binding:t -*-

;; Copyright (C) 1985-1986, 1999-2025 Free Software Foundation, Inc.

;; Maintainer: emacs-devel@gnu.org
;; Keywords: lisp, languages
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

;; The base major mode for editing Lisp code (used also for Emacs Lisp).
;; This mode is documented in the Emacs manual.

;;; Code:

(eval-when-compile (require 'cl-lib))
(eval-when-compile (require 'subr-x))

(define-abbrev-table 'lisp-mode-abbrev-table ()
  "Abbrev table for Lisp mode.")

(defvar lisp-data-mode-syntax-table
  (let ((table (make-syntax-table))
        (i 0))
    (while (< i ?0)
      (modify-syntax-entry i "_   " table)
      (setq i (1+ i)))
    (setq i (1+ ?9))
    (while (< i ?A)
      (modify-syntax-entry i "_   " table)
      (setq i (1+ i)))
    (setq i (1+ ?Z))
    (while (< i ?a)
      (modify-syntax-entry i "_   " table)
      (setq i (1+ i)))
    (setq i (1+ ?z))
    (while (< i 128)
      (modify-syntax-entry i "_   " table)
      (setq i (1+ i)))
    (modify-syntax-entry ?\s "    " table)
    ;; Non-break space acts as whitespace.
    (modify-syntax-entry ?\xa0 "    " table)
    (modify-syntax-entry ?\t "    " table)
    (modify-syntax-entry ?\f "    " table)
    (modify-syntax-entry ?\n ">   " table)
    (modify-syntax-entry ?\; "<   " table)
    (modify-syntax-entry ?` "'   " table)
    (modify-syntax-entry ?' "'   " table)
    (modify-syntax-entry ?, "'   " table)
    (modify-syntax-entry ?@ "_ p" table)
    ;; Used to be singlequote; changed for flonums.
    (modify-syntax-entry ?. "_   " table)
    (modify-syntax-entry ?# "'   " table)
    (modify-syntax-entry ?\" "\"    " table)
    (modify-syntax-entry ?\\ "\\   " table)
    (modify-syntax-entry ?\( "()  " table)
    (modify-syntax-entry ?\) ")(  " table)
    (modify-syntax-entry ?\[ "(]" table)
    (modify-syntax-entry ?\] ")[" table)
    table)
  "Parent syntax table used in Lisp modes.")

(defvar lisp-mode-syntax-table
  (let ((table (make-syntax-table lisp-data-mode-syntax-table)))
    (modify-syntax-entry ?\[ "_   " table)
    (modify-syntax-entry ?\] "_   " table)
    (modify-syntax-entry ?# "' 14" table)
    (modify-syntax-entry ?| "\" 23bn" table)
    table)
  "Syntax table used in `lisp-mode'.")

(rx-define lisp-mode-symbol (+ (| (syntax word)
                                  (syntax symbol)
                                  (: "\\" nonl))))

(eval-and-compile
  (defconst lisp-mode-symbol-regexp (rx lisp-mode-symbol)))

(defvar lisp-imenu-generic-expression
  (list
   (list nil
         (concat "^\\s-*("
                 (regexp-opt
                  '("defun" "defmacro"
                    ;; Elisp.
                    "defun*" "defsubst" "define-inline"
                    "define-advice" "defadvice" "define-skeleton"
                    "define-compilation-mode" "define-minor-mode"
                    "define-globalized-minor-mode"
                    "define-derived-mode" "define-generic-mode"
                    "ert-deftest"
                    "cl-defun" "cl-defsubst" "cl-defmacro"
                    "cl-define-compiler-macro" "cl-defgeneric"
                    "cl-defmethod"
                    ;; CL.
                    "define-compiler-macro" "define-modify-macro"
                    "defsetf" "define-setf-expander"
                    "define-method-combination"
                    ;; CLOS and EIEIO
                    "defgeneric" "defmethod")
                  t)
                 "\\s-+\\(" (rx lisp-mode-symbol) "\\)")
	 2)
   ;; Like the previous, but uses a quoted symbol as the name.
   (list nil
         (concat "^\\s-*("
                 (regexp-opt
                  '("defalias" "define-obsolete-function-alias")
                  t)
                 "\\s-+'\\(" (rx lisp-mode-symbol) "\\)")
	 2)
   (list "Variables"
         (concat "^\\s-*("
                 (regexp-opt
                  '(;; Elisp
                    "defconst" "defcustom" "defvar-keymap"
                    ;; CL
                    "defconstant"
                    "defparameter" "define-symbol-macro")
                  t)
                 "\\s-+\\(" (rx lisp-mode-symbol) "\\)")
	 2)
   ;; For `defvar'/`defvar-local', we ignore (defvar FOO) constructs.
   (list "Variables"
         (concat "^\\s-*(defvar\\(?:-local\\)?\\s-+\\("
                 (rx lisp-mode-symbol) "\\)"
                 "[[:space:]\n]+[^)]")
	 1)
   (list "Types"
         (concat "^\\s-*("
                 (regexp-opt
                  '(;; Elisp
                    "defgroup" "deftheme"
                    "define-widget" "define-error"
                    "defface" "cl-deftype" "cl-defstruct" "oclosure-define"
                    ;; CL
                    "deftype" "defstruct"
                    "define-condition" "defpackage"
                    ;; CLOS and EIEIO
                    "defclass")
                  t)
                 "\\s-+'?\\(" (rx lisp-mode-symbol) "\\)")
	 2))

  "Imenu generic expression for Lisp mode.  See `imenu-generic-expression'.")

(defconst lisp-mode-autoload-regexp
  "^;;;###\\(\\([-[:alnum:]]+?\\)-\\)?\\(autoload\\)"
  "Regexp to match autoload cookies.
The second group matches package names used to redirect autoloads
to a package-local <package>-loaddefs.el file.")

;; This was originally in autoload.el and is still used there.
(put 'autoload 'doc-string-elt 3)
(put 'defmethod 'doc-string-elt 3)
(put 'defvar   'doc-string-elt 3)
(put 'defconst 'doc-string-elt 3)
(put 'defalias 'doc-string-elt 3)
(put 'defvaralias 'doc-string-elt 3)
(put 'define-category 'doc-string-elt 2)
;; CL
(put 'defconstant 'doc-string-elt 3)
(put 'define-compiler-macro 'doc-string-elt 3)
(put 'define-setf-expander 'doc-string-elt 3)
(put 'defparameter 'doc-string-elt 3)
(put 'defstruct 'doc-string-elt 2)
(put 'deftype 'doc-string-elt 3)

(defvar lisp-doc-string-elt-property 'doc-string-elt
  "The symbol property that holds the docstring position info.")

(defconst lisp-prettify-symbols-alist '(("lambda"  . ?λ))
  "Alist of symbol/\"pretty\" characters to be displayed.")

;;;; Font-lock support.

(defun lisp--match-hidden-arg (limit)
  (let ((res nil))
    (forward-line 0)
    (while
        (let ((ppss (parse-partial-sexp (point)
                                        (line-end-position)
                                        -1)))
          (skip-syntax-forward " )")
          (if (or (>= (car ppss) 0)
                  (eolp)
                  (looking-at ";")
                  (nth 8 (syntax-ppss))) ;Within a string or comment.
              (progn
                (forward-line 1)
                (< (point) limit))
            (looking-at ".*")           ;Set the match-data.
	    (forward-line 1)
            (setq res (point))
            nil)))
    res))

(defun lisp--el-non-funcall-position-p (pos)
  "Heuristically determine whether POS is an evaluated position."
  (declare (obsolete lisp--el-funcall-position-p "28.1"))
  (not (lisp--el-funcall-position-p pos)))

(defun lisp--el-funcall-position-p (pos)
  "Heuristically determine whether POS is an evaluated position."
  (save-match-data
    (save-excursion
      (ignore-errors
        (goto-char pos)
        ;; '(lambda ..) is not a funcall position, but #'(lambda ...) is.
        (if (eql (char-before) ?\')
            (eql (char-before (1- (point))) ?#)
          (let* ((ppss (syntax-ppss))
                 (paren-posns (nth 9 ppss))
                 (parent
                  (when paren-posns
                    (goto-char (car (last paren-posns))) ;(up-list -1)
                    (cond
                     ((ignore-errors
                        (and (eql (char-after) ?\()
                             (when (cdr paren-posns)
                               (goto-char (car (last paren-posns 2)))
                               (looking-at "(\\_<let\\*?\\_>"))))
                      (goto-char (match-end 0))
                      'let)
                     ((looking-at
                       (rx "("
                           (group-n 1 (+ (or (syntax w) (syntax _))))
                           symbol-end))
                      (prog1 (intern-soft (match-string-no-properties 1))
                        (goto-char (match-end 1))))))))
            (pcase parent
              ('declare nil)
              ('let
                (forward-sexp 1)
                (>= pos (point)))
              ((or 'defun 'defmacro 'cl-defmethod 'cl-defun)
                (forward-sexp 2)
                (>= pos (point)))
              ('condition-case
                  ;; If (cdr paren-posns), then we're in the BODY
                  ;; of HANDLERS.
                  (or (cdr paren-posns)
                      (progn
                        (forward-sexp 1)
                        ;; If we're in the second form, then we're in
                        ;; a funcall position.
                        (< (point) pos (progn (forward-sexp 1)
                                              (point))))))
              (_ t))))))))

(defun lisp--el-match-keyword (limit)
  ;; FIXME: Move to elisp-mode.el.
  (catch 'found
    (while (re-search-forward
            (concat "(\\(" (rx lisp-mode-symbol) "\\)\\_>")
            limit t)
      (let ((sym (intern-soft (match-string 1))))
	(when (and (or (special-form-p sym) (macrop sym))
                   (not (get sym 'no-font-lock-keyword))
                   (lisp--el-funcall-position-p (match-beginning 0)))
	  (throw 'found t))))))

(defmacro let-when-compile (bindings &rest body)
  "Like `let*', but allow for compile time optimization.
Use BINDINGS as in regular `let*', but in BODY each usage should
be wrapped in `eval-when-compile'.
This will generate compile-time constants from BINDINGS."
  (declare (indent 1) (debug let))
  (letrec ((loop
            (lambda (bindings)
              (if (null bindings)
                  (macroexpand-all (macroexp-progn body)
                                   macroexpand-all-environment)
                (let ((binding (pop bindings)))
                  (cl-progv (list (car binding))
                      (list (eval (nth 1 binding) t))
                    (funcall loop bindings)))))))
    (funcall loop bindings)))

(defun elisp--font-lock-backslash ()
  (let* ((beg0 (match-beginning 0))
         (end0 (match-end 0))
         (ppss (save-excursion (syntax-ppss beg0))))
    (and (nth 3 ppss)                  ;Inside a string.
         (not (nth 5 ppss))            ;The \ is not itself \-escaped.
         ;; Don't highlight the \( introduced because of
         ;; `open-paren-in-column-0-is-defun-start'.
         (not (eq ?\n (char-before beg0)))
         (equal (ignore-errors
                  (car (read-from-string
                        (format "\"%s\""
                                (buffer-substring-no-properties
                                 beg0 end0)))))
                (buffer-substring-no-properties (1+ beg0) end0))
         '(face font-lock-warning-face
                help-echo "This \\ has no effect"))))

(defun lisp--match-confusable-symbol-character  (limit)
  ;; Match a confusable character within a Lisp symbol.
  (catch 'matched
    (while t
      (if (re-search-forward help-uni-confusables-regexp limit t)
          ;; Skip confusables which are backslash escaped, or inside
          ;; strings or comments.
          (save-match-data
            (unless (or (eq (char-before (match-beginning 0)) ?\\)
                        (nth 8 (syntax-ppss)))
              (throw 'matched t)))
        (throw 'matched nil)))))

(defun lisp-mode--search-key (char bound)
  (catch 'found
    (while (re-search-forward
            (concat "\\_<" char (rx lisp-mode-symbol) "\\_>")
            bound t)
      (when (or (< (match-beginning 0) (+ (point-min) 2))
                ;; A quoted white space before the &/: means that this
                ;; is not the start of a :keyword or an &option.
                (not (eql (char-after (- (match-beginning 0) 2))
                          ?\\))
                (not (memq (char-after (- (match-beginning 0) 1))
                           '(?\s ?\n ?\t))))
        (throw 'found t)))))

(let-when-compile
    ((lisp-fdefs '("defmacro" "defun"))
     (lisp-vdefs '("defvar"))
     (lisp-kw '("cond" "if" "while" "let" "let*" "progn" "prog1"
                "prog2" "lambda" "unwind-protect" "condition-case"
                "when" "unless" "with-output-to-string" "handler-bind"
                "ignore-errors" "dotimes" "dolist" "declare"))
     (lisp-errs '("warn" "error" "signal"))
     ;; Elisp constructs.  Now they are update dynamically
     ;; from obarray but they are also used for setting up
     ;; the keywords for Common Lisp.
     (el-fdefs '("defsubst" "cl-defsubst" "define-inline"
                 "define-advice" "defadvice" "defalias"
                 "define-derived-mode" "define-minor-mode"
                 "define-generic-mode"
                 "define-globalized-minor-mode" "define-skeleton"
                 "define-widget" "ert-deftest"))
     (el-vdefs '("defconst" "defcustom" "defvaralias" "defvar-local"
                 "defface" "define-error"))
     (el-tdefs '("defgroup" "deftheme"))
     (el-errs '("user-error"))
     ;; Common-Lisp constructs supported by EIEIO.  FIXME: namespace.
     (eieio-fdefs '("defgeneric" "defmethod"))
     (eieio-tdefs '("defclass"))
     ;; Common-Lisp constructs supported by cl-lib.
     (cl-lib-fdefs '("defmacro" "defsubst" "defun" "defmethod" "defgeneric"))
     (cl-lib-tdefs '("defstruct" "deftype"))
     (cl-lib-errs '("assert" "check-type"))
     ;; Common-Lisp constructs not supported by cl-lib.
     (cl-fdefs '("defsetf" "define-method-combination"
                 "define-condition" "define-setf-expander"
                 ;; "define-function"??
                 "define-compiler-macro" "define-modify-macro"))
     (cl-vdefs '("define-symbol-macro" "defconstant" "defparameter"))
     (cl-tdefs '("defpackage" "defstruct" "deftype"))
     (cl-kw '("block" "break" "case" "ccase" "compiler-let" "ctypecase"
              "declaim" "destructuring-bind" "do" "do*"
              "ecase" "etypecase" "eval-when" "flet" "flet*"
              "go" "handler-case" "in-package" ;; "inline"
              "labels" "letf" "locally" "loop"
              "macrolet" "multiple-value-bind" "multiple-value-prog1"
              "proclaim" "prog" "prog*" "progv"
              "restart-case" "restart-bind" "return" "return-from"
              "symbol-macrolet" "tagbody" "the" "typecase"
              "with-accessors" "with-compilation-unit"
              "with-condition-restarts" "with-hash-table-iterator"
              "with-input-from-string" "with-open-file"
              "with-open-stream" "with-package-iterator"
              "with-simple-restart" "with-slots" "with-standard-io-syntax"))
     (cl-errs '("abort" "cerror")))
  (let ((vdefs (eval-when-compile
                 (append lisp-vdefs el-vdefs cl-vdefs)))
        (tdefs (eval-when-compile
                 (append el-tdefs eieio-tdefs cl-tdefs cl-lib-tdefs
                         (mapcar (lambda (s) (concat "cl-" s)) cl-lib-tdefs))))
        ;; Elisp and Common Lisp definers.
        (el-defs-re (eval-when-compile
                      (regexp-opt (append lisp-fdefs lisp-vdefs
                                          el-fdefs el-vdefs el-tdefs
                                          (mapcar (lambda (s) (concat "cl-" s))
                                                  (append cl-lib-fdefs cl-lib-tdefs))
                                          eieio-fdefs eieio-tdefs)
                                  t)))
        (cl-defs-re (eval-when-compile
                      (regexp-opt (append lisp-fdefs lisp-vdefs
                                          cl-lib-fdefs cl-lib-tdefs
                                          eieio-fdefs eieio-tdefs
                                          cl-fdefs cl-vdefs cl-tdefs)
                                  t)))
        ;; Common Lisp keywords (Elisp keywords are handled dynamically).
        (cl-kws-re (eval-when-compile
                     (regexp-opt (append lisp-kw cl-kw) t)))
        ;; Elisp and Common Lisp "errors".
        (el-errs-re (eval-when-compile
                      (regexp-opt (append (mapcar (lambda (s) (concat "cl-" s))
                                                  cl-lib-errs)
                                          lisp-errs el-errs)
                                  t)))
        (cl-errs-re (eval-when-compile
                      (regexp-opt (append lisp-errs cl-lib-errs cl-errs) t))))
    (dolist (v vdefs)
      (put (intern v) 'lisp-define-type 'var))
    (dolist (v tdefs)
      (put (intern v) 'lisp-define-type 'type))

    (define-obsolete-variable-alias 'lisp-font-lock-keywords-1
        'lisp-el-font-lock-keywords-1 "24.4")
    (defconst lisp-el-font-lock-keywords-1
      `( ;; Definitions.
        (,(concat "(" el-defs-re "\\_>"
                  ;; Any whitespace and defined object.
                  "[ \t']*"
                  "\\(([ \t']*\\)?" ;; An opening paren.
                  "\\(\\(setf\\)[ \t]+" (rx lisp-mode-symbol)
                  "\\|" (rx lisp-mode-symbol) "\\)?")
          (1 font-lock-keyword-face)
          (3 (let ((type (get (intern-soft (match-string 1)) 'lisp-define-type)))
               (cond ((eq type 'var) font-lock-variable-name-face)
                     ((eq type 'type) font-lock-type-face)
                     ;; If match-string 2 is non-nil, we encountered a
                     ;; form like (defalias (intern (concat s "-p"))),
                     ;; unless match-string 4 is also there.  Then its a
                     ;; defmethod with (setf foo) as name.
                     ((or (not (match-string 2)) ;; Normal defun.
                          (and (match-string 2)  ;; Setf method.
                               (match-string 4)))
                      font-lock-function-name-face)))
             nil t))
        ;; Emacs Lisp autoload cookies.  Supports the slightly different
        ;; forms used by mh-e, calendar, etc.
        (,lisp-mode-autoload-regexp (3 font-lock-warning-face prepend)
                                    (2 font-lock-function-name-face prepend t)))
      "Subdued level highlighting for Emacs Lisp mode.")

    (defconst lisp-cl-font-lock-keywords-1
      `( ;; Definitions.
        (,(concat "(" cl-defs-re "\\_>"
                  ;; Any whitespace and defined object.
                  "[ \t']*"
                  "\\(([ \t']*\\)?" ;; An opening paren.
                  "\\(\\(setf\\)[ \t]+" (rx lisp-mode-symbol)
                  "\\|" (rx lisp-mode-symbol) "\\)?")
          (1 font-lock-keyword-face)
          (3 (let ((type (get (intern-soft (match-string 1)) 'lisp-define-type)))
               (cond ((eq type 'var) font-lock-variable-name-face)
                     ((eq type 'type) font-lock-type-face)
                     ((or (not (match-string 2)) ;; Normal defun.
                          (and (match-string 2)  ;; Setf function.
                               (match-string 4)))
                      font-lock-function-name-face)))
             nil t)))
      "Subdued level highlighting for Lisp modes.")

    (define-obsolete-variable-alias 'lisp-font-lock-keywords-2
        'lisp-el-font-lock-keywords-2 "24.4")
    (defconst lisp-el-font-lock-keywords-2
      (append
       lisp-el-font-lock-keywords-1
       `( ;; Regexp negated char group.
         ("\\[\\(\\^\\)" 1 font-lock-negation-char-face prepend)
         ;; Erroneous structures.
         (,(concat "(" el-errs-re "\\_>")
          (1 font-lock-warning-face))
         ;; Control structures.  Common Lisp forms.
         (lisp--el-match-keyword . 1)
         ;; Exit/Feature symbols as constants.
         (,(concat "(\\(catch\\|throw\\|featurep\\|provide\\|require\\)\\_>"
                   "[ \t']*\\(" (rx lisp-mode-symbol) "\\)?")
           (1 font-lock-keyword-face)
           (2 font-lock-constant-face nil t))
         ;; Words inside \\[], \\<>, \\{} or \\`' tend to be for
         ;; `substitute-command-keys'.
         (,(rx "\\\\" (or (seq "["
                               (group-n 1 (seq lisp-mode-symbol (not "\\"))) "]")
                          (seq "`" (group-n 1
                                     ;; allow multiple words, e.g. "C-x a"
                                     lisp-mode-symbol (* " " lisp-mode-symbol))
                               "'")))
          (1 font-lock-constant-face prepend))
         (,(rx "\\\\" (or (seq "<"
                               (group-n 1 (seq lisp-mode-symbol (not "\\"))) ">")
                          (seq "{"
                               (group-n 1 (seq lisp-mode-symbol (not "\\"))) "}")))
          (1 font-lock-variable-name-face prepend))
         ;; Ineffective backslashes (typically in need of doubling).
         ("\\(\\\\\\)\\([^\"\\]\\)"
          (1 (elisp--font-lock-backslash) prepend))
         ;; Words inside ‘’, '' and `' tend to be symbol names.
         (,(concat "[`‘']\\(" (rx lisp-mode-symbol) "\\)['’]")
          (1 font-lock-constant-face prepend))
         ;; \\= tends to be an escape in doc strings.
         (,(rx "\\\\=")
          (0 font-lock-builtin-face prepend))
         ;; Constant values.
         (,(lambda (bound) (lisp-mode--search-key ":" bound))
          (0 font-lock-builtin-face))
         ;; Elisp and Common Lisp `&' keywords as types.
         (,(lambda (bound) (lisp-mode--search-key "&" bound))
          (0 font-lock-type-face))
         ;; Elisp regexp grouping constructs
         (,(lambda (bound)
             (catch 'found
               ;; The following loop is needed to continue searching after matches
               ;; that do not occur in strings.  The associated regexp matches one
               ;; of `\\\\' `\\(' `\\(?:' `\\|' `\\)'.  `\\\\' has been included to
               ;; avoid highlighting, for example, `\\(' in `\\\\('.
               (while (re-search-forward "\\(\\\\\\\\\\)\\(?:\\(\\\\\\\\\\)\\|\\((\\(?:\\?[0-9]*:\\)?\\|[|)]\\)\\)" bound t)
                 (unless (match-beginning 2)
                   (let ((face (get-text-property (1- (point)) 'face)))
                     (when (or (and (listp face)
                                    (memq 'font-lock-string-face face))
                               (eq 'font-lock-string-face face))
                       (throw 'found t)))))))
           (1 'font-lock-regexp-grouping-backslash prepend)
           (3 'font-lock-regexp-grouping-construct prepend))
         (lisp--match-hidden-arg
          (0 '(face font-lock-warning-face
               help-echo "Easy to misread; consider moving the element to the next line")
             prepend))
         (lisp--match-confusable-symbol-character
          0 '(face font-lock-warning-face
                    help-echo "Confusable character"))
         ))
      "Gaudy level highlighting for Emacs Lisp mode.")

    (defconst lisp-cl-font-lock-keywords-2
      (append
       lisp-cl-font-lock-keywords-1
       `( ;; Regexp negated char group.
         ("\\[\\(\\^\\)" 1 font-lock-negation-char-face prepend)
         ;; Control structures.  Common Lisp forms.
         (,(concat "(" cl-kws-re "\\_>") . 1)
         ;; Exit/Feature symbols as constants.
         (,(concat "(\\(catch\\|throw\\|provide\\|require\\)\\_>"
                   "[ \t']*\\(" (rx lisp-mode-symbol) "\\)?")
           (1 font-lock-keyword-face)
           (2 font-lock-constant-face nil t))
         ;; Erroneous structures.
         (,(concat "(" cl-errs-re "\\_>")
           (1 font-lock-warning-face))
         ;; Words inside ‘’ and `' tend to be symbol names.
         (,(concat "[`‘]\\("
                   (rx (* lisp-mode-symbol (+ space)) lisp-mode-symbol)
                   "\\)['’]")
          (1 font-lock-constant-face prepend))
         ;; Uninterned symbols, e.g., (defpackage #:my-package ...)
         ;; must come before keywords below to have effect
         (,(concat "#:" (rx lisp-mode-symbol) "") 0 font-lock-builtin-face)
         ;; Constant values.
         (,(lambda (bound) (lisp-mode--search-key ":" bound))
          (0 font-lock-builtin-face))
         ;; Elisp and Common Lisp `&' keywords as types.
         (,(lambda (bound) (lisp-mode--search-key "&" bound))
          (0 font-lock-type-face))
         ;; Elisp regexp grouping constructs
         ;; This is too general -- rms.
         ;; A user complained that he has functions whose names start with `do'
         ;; and that they get the wrong color.
         ;; That user has violated the https://www.cliki.net/Naming+conventions:
         ;; CL (but not EL!) `with-' (context) and `do-' (iteration)
         (,(concat "(\\(\\(do-\\|with-\\)" (rx lisp-mode-symbol) "\\)")
           (1 font-lock-keyword-face))
         (lisp--match-hidden-arg
          (0 '(face font-lock-warning-face
               help-echo "Easy to misread; consider moving the element to the next line")
             prepend))
         ))
      "Gaudy level highlighting for Lisp modes.")))

(define-obsolete-variable-alias 'lisp-font-lock-keywords
  'lisp-el-font-lock-keywords "24.4")
(defvar lisp-el-font-lock-keywords lisp-el-font-lock-keywords-1
  "Default expressions to highlight in Emacs Lisp mode.")
(defvar lisp-cl-font-lock-keywords lisp-cl-font-lock-keywords-1
  "Default expressions to highlight in Lisp modes.")

;; Support backtrace mode.
(defconst lisp-el-font-lock-keywords-for-backtraces lisp-el-font-lock-keywords
  "Default highlighting from Emacs Lisp mode used in Backtrace mode.")
(defconst lisp-el-font-lock-keywords-for-backtraces-1 lisp-el-font-lock-keywords-1
  "Subdued highlighting from Emacs Lisp mode used in Backtrace mode.")
(defconst lisp-el-font-lock-keywords-for-backtraces-2
  (remove (assoc 'lisp--match-hidden-arg lisp-el-font-lock-keywords-2)
          lisp-el-font-lock-keywords-2)
  "Gaudy highlighting from Emacs Lisp mode used in Backtrace mode.")

(defun lisp-string-in-doc-position-p (listbeg startpos)
  "Return non-nil if a doc string may occur at STARTPOS inside a list.
LISTBEG is the position of the start of the innermost list
containing STARTPOS."
  (let* ((firstsym (and listbeg
                        (save-excursion
                          (goto-char listbeg)
                          (and (looking-at
                                (concat "([ \t\n]*\\("
                                        (rx lisp-mode-symbol) "\\)"))
                               (match-string 1)))))
         (docelt (and firstsym
                      (function-get (intern-soft firstsym)
                                    lisp-doc-string-elt-property))))
    (and docelt
         ;; It's a string in a form that can have a docstring.
         ;; Check whether it's in docstring position.
         (save-excursion
           (when (functionp docelt)
             (goto-char (match-end 1))
             (setq docelt (funcall docelt)))
           (goto-char listbeg)
           (forward-char 1)
           (condition-case nil
               (while (and (> docelt 0) (< (point) startpos)
                           (progn (forward-sexp 1) t))
                 (setq docelt (1- docelt)))
             (error nil))
           (and (zerop docelt) (<= (point) startpos)
                (progn (forward-comment (point-max)) t)
                (= (point) startpos))))))

(defun lisp-string-after-doc-keyword-p (listbeg startpos)
  "Return non-nil if `:documentation' symbol ends at STARTPOS inside a list.
`:doc' can also be used.

LISTBEG is the position of the start of the innermost list
containing STARTPOS."
  (and listbeg                          ; We are inside a Lisp form.
       (save-excursion
         (goto-char startpos)
         (ignore-errors
           (progn (backward-sexp 1)
                  (looking-at ":documentation\\_>\\|:doc\\_>"))))))

(defun lisp-font-lock-syntactic-face-function (state)
  "Return syntactic face function for the position represented by STATE.
STATE is a `parse-partial-sexp' state, and the returned function is the
Lisp font lock syntactic face function."
  (if (nth 3 state)
      ;; This might be a (doc)string or a |...| symbol.
      (let ((startpos (nth 8 state)))
        (if (eq (char-after startpos) ?|)
            ;; This is not a string, but a |...| symbol.
            nil
          (let ((listbeg (nth 1 state)))
            (if (or (lisp-string-in-doc-position-p listbeg startpos)
                    (lisp-string-after-doc-keyword-p listbeg startpos))
                'font-lock-doc-face
              'font-lock-string-face))))
    'font-lock-comment-face))

(defun lisp-adaptive-fill ()
  "Return fill prefix found at point.
Value for `adaptive-fill-function'."
  ;; Adaptive fill mode gets the fill wrong for a one-line paragraph made of
  ;; a single docstring.  Let's fix it here.
  (if (looking-at "\\s-+\"[^\n\"]+\"\\s-*$") ""))

;; Maybe this should be discouraged/obsoleted and users should be
;; encouraged to use 'lisp-data-mode' instead.
(defun lisp-mode-variables (&optional lisp-syntax keywords-case-insensitive
                                      elisp)
  "Common initialization routine for Lisp modes.
The LISP-SYNTAX argument is used by code in inf-lisp.el and is
\(uselessly) passed from pp.el, chistory.el, gnus-kill.el and
score-mode.el.  KEYWORDS-CASE-INSENSITIVE non-nil means that for
font-lock keywords will not be case sensitive."
  (when lisp-syntax
    (set-syntax-table lisp-mode-syntax-table))
  (setq-local paragraph-ignore-fill-prefix t)
  (setq-local fill-paragraph-function 'lisp-fill-paragraph)
  (setq-local adaptive-fill-function #'lisp-adaptive-fill)
  ;; Adaptive fill mode gets in the way of auto-fill,
  ;; and should make no difference for explicit fill
  ;; because lisp-fill-paragraph should do the job.
  ;;  I believe that newcomment's auto-fill code properly deals with it  -stef
  ;;(setq-local adaptive-fill-mode nil)
  (setq-local indent-line-function 'lisp-indent-line)
  (setq-local indent-region-function 'lisp-indent-region)
  (setq-local comment-indent-function #'lisp-comment-indent)
  (setq-local outline-regexp (concat ";;;;* [^ \t\n]\\|(\\|\\("
                                     lisp-mode-autoload-regexp
                                     "\\)"))
  (setq-local outline-level 'lisp-outline-level)
  (setq-local add-log-current-defun-function #'lisp-current-defun-name)
  (setq-local comment-start ";")
  (setq-local comment-start-skip ";+ *")
  (setq-local comment-add 1)		;default to `;;' in comment-region
  (setq-local comment-column 40)
  (setq-local comment-use-syntax t)
  (setq-local imenu-generic-expression lisp-imenu-generic-expression)
  (setq-local multibyte-syntax-as-symbol t)
  ;; (setq-local syntax-begin-function 'beginning-of-defun)  ;;Bug#16247.
  (setq font-lock-defaults
	`(,(if elisp '(lisp-el-font-lock-keywords
                       lisp-el-font-lock-keywords-1
                       lisp-el-font-lock-keywords-2)
             '(lisp-cl-font-lock-keywords
               lisp-cl-font-lock-keywords-1
               lisp-cl-font-lock-keywords-2))
	  nil ,keywords-case-insensitive nil nil
	  (font-lock-mark-block-function . mark-defun)
          (font-lock-extra-managed-props help-echo)
	  (font-lock-syntactic-face-function
	   . lisp-font-lock-syntactic-face-function)))
  (setq-local prettify-symbols-alist lisp-prettify-symbols-alist)
  (setq-local electric-pair-skip-whitespace 'chomp)
  (setq-local electric-pair-open-newline-between-pairs nil))

;;;###autoload
(define-derived-mode lisp-data-mode prog-mode "Lisp-Data"
  "Major mode for buffers holding data written in Lisp syntax."
  :group 'lisp
  (lisp-mode-variables nil t nil)
  (setq-local electric-quote-string t)
  (setq imenu-case-fold-search nil))

(defun lisp-outline-level ()
  "Lisp mode `outline-level' function."
  ;; Expects outline-regexp is ";;;\\(;* [^ \t\n]\\|###autoload\\)\\|("
  ;; and point is at the beginning of a matching line.
  (let ((len (- (match-end 0) (match-beginning 0))))
    (cond ((or (looking-at-p "(")
               (looking-at-p lisp-mode-autoload-regexp))
           1000)
          ((looking-at ";;\\(;+\\) ")
           (- (match-end 1) (match-beginning 1)))
          ;; Above should match everything but just in case.
          (t
           len))))

(defun lisp-current-defun-name ()
  "Return the name of the defun at point, or nil."
  (save-excursion
    (let ((location (point)))
      ;; If we are now precisely at the beginning of a defun, make sure
      ;; beginning-of-defun finds that one rather than the previous one.
      (or (eobp) (forward-char 1))
      (beginning-of-defun)
      ;; Make sure we are really inside the defun found, not after it.
      (when (and (looking-at "\\s(")
		 (progn (end-of-defun)
			(< location (point)))
		 (progn (forward-sexp -1)
			(>= location (point))))
	(if (looking-at "\\s(")
	    (forward-char 1))
	;; Skip the defining construct name, typically "defun" or
	;; "defvar".
	(forward-sexp 1)
	;; The second element is usually a symbol being defined.  If it
	;; is not, use the first symbol in it.
	(skip-chars-forward " \t\n'(")
	(buffer-substring-no-properties (point)
					(progn (forward-sexp 1)
					       (point)))))))

(defvar-keymap lisp-mode-shared-map
  :doc "Keymap for commands shared by all sorts of Lisp modes."
  :parent prog-mode-map
  "C-M-q" #'indent-sexp
  "DEL"   #'backward-delete-char-untabify
  ;; This gets in the way when viewing a Lisp file in view-mode.  As
  ;; long as [backspace] is mapped into DEL via the
  ;; function-key-map, this should remain disabled!!
  ;;;"<backspace>" #'backward-delete-char-untabify
  )

(defcustom lisp-mode-hook nil
  "Hook run when entering Lisp mode."
  :options '(imenu-add-menubar-index)
  :type 'hook
  :group 'lisp)

(defcustom lisp-interaction-mode-hook nil
  "Hook run when entering Lisp Interaction mode."
  :options '(eldoc-mode)
  :type 'hook
  :group 'lisp)

;;; Generic Lisp mode.

(defvar-keymap lisp-mode-map
  :doc "Keymap for ordinary Lisp mode.
All commands in `lisp-mode-shared-map' are inherited by this map."
  :parent lisp-mode-shared-map
  "C-M-x"   #'lisp-eval-defun
  "C-c C-z" #'run-lisp)

(easy-menu-define lisp-mode-menu lisp-mode-map
  "Menu for ordinary Lisp mode."
  '("Lisp"
    ["Indent sexp" indent-sexp
     :help "Indent each line of the list starting just after point"]
    ["Eval defun" lisp-eval-defun
     :help "Send the current defun to the Lisp process made by M-x run-lisp"]
    ["Run inferior Lisp" run-lisp
     :help "Run an inferior Lisp process, input and output via buffer `*inferior-lisp*'"]))

(define-derived-mode lisp-mode lisp-data-mode "Lisp"
  "Major mode for editing programs in Common Lisp and other similar Lisps.
Commands:
Delete converts tabs to spaces as it moves back.
Blank lines separate paragraphs.  Semicolons start comments.

\\{lisp-mode-map}
Note that `run-lisp' may be used either to start an inferior Lisp job
or to switch back to an existing one."
  (setq-local lisp-indent-function 'common-lisp-indent-function)
  (setq-local find-tag-default-function 'lisp-find-tag-default)
  (setq-local comment-start-skip
	      "\\(\\(^\\|[^\\\n]\\)\\(\\\\\\\\\\)*\\)\\(;+\\|#|\\) *")
  (setq-local comment-end-skip "[ \t]*\\(\\s>\\||#\\)")
  (setq-local font-lock-comment-end-skip "|#")
  (setq imenu-case-fold-search t))

(defun lisp-find-tag-default ()
  (let ((default (find-tag-default)))
    (when (stringp default)
      (if (string-match ":+" default)
          (substring default (match-end 0))
	default))))

;; Used in old LispM code.
(defalias 'common-lisp-mode 'lisp-mode)

(autoload 'lisp-eval-defun "inf-lisp" nil t)

(defun lisp-comment-indent ()
  "Like `comment-indent-default', but don't put space after open paren."
  (or (when (looking-at "\\s<\\s<")
        (let ((pt (point)))
          (skip-syntax-backward " ")
          (if (eq (preceding-char) ?\()
              (cons (current-column) (current-column))
            (goto-char pt)
            nil)))
      (comment-indent-default)))

(defcustom lisp-indent-offset nil
  "If non-nil, indent second line of expressions that many more columns."
  :group 'lisp
  :type '(choice (const nil) integer)
  :safe (lambda (x) (or (null x) (integerp x))))

(defcustom lisp-indent-function 'lisp-indent-function
  "A function to be called by `calculate-lisp-indent'.
It indents the arguments of a Lisp function call.  This function
should accept two arguments: the indent-point, and the
`parse-partial-sexp' state at that position.  One option for this
function is `common-lisp-indent-function'."
  :type 'function
  :group 'lisp)

(defun lisp-ppss (&optional pos)
  "Return Parse-Partial-Sexp State at POS, defaulting to point.
Like `syntax-ppss' but includes the character address of the last
complete sexp in the innermost containing list at position
2 (counting from 0).  This is important for Lisp indentation."
  (unless pos (setq pos (point)))
  (let ((pss (syntax-ppss pos)))
    (if (and (not (nth 2 pss)) (nth 9 pss))
        (let ((sexp-start (car (last (nth 9 pss)))))
          (parse-partial-sexp sexp-start pos nil nil (syntax-ppss sexp-start)))
      pss)))

(cl-defstruct (lisp-indent-state
               (:constructor nil)
               (:constructor lisp-indent-initial-state
                             (&aux (ppss (lisp-ppss))
                                   (ppss-point (point))
                                   (stack (make-list (1+ (car ppss)) nil)))))
  stack ;; Cached indentation, per depth.
  ppss
  ppss-point)

(defun lisp-indent-calc-next (state)
  "Move to next line and return calculated indent for it.
STATE is updated by side effect, the first state should be
created by `lisp-indent-initial-state'.  This function may move
by more than one line to cross a string literal."
  (pcase-let* (((cl-struct lisp-indent-state
                           (stack indent-stack) ppss ppss-point)
                state)
               (indent-depth (car ppss)) ; Corresponding to indent-stack.
               (depth indent-depth))
    ;; Parse this line so we can learn the state to indent the
    ;; next line.
    (while (let ((last-sexp (nth 2 ppss)))
             (setq ppss (parse-partial-sexp
                         ppss-point (progn (end-of-line) (point))
                         nil nil ppss))
             ;; Preserve last sexp of state (position 2) for
             ;; `calculate-lisp-indent', if we're at the same depth.
             (if (and (not (nth 2 ppss)) (= depth (car ppss)))
                 (setf (nth 2 ppss) last-sexp)
               (setq last-sexp (nth 2 ppss)))
             (setq depth (car ppss))
             ;; Skip over newlines within strings.
             (and (not (eobp)) (nth 3 ppss)))
      (let ((string-start (nth 8 ppss)))
        (setq ppss (parse-partial-sexp (point) (point-max)
                                       nil nil ppss 'syntax-table))
        (setf (nth 2 ppss) string-start) ; Finished a complete string.
        (setq depth (car ppss)))
      (setq ppss-point (point)))
    (setq ppss-point (point))
    (let* ((depth-delta (- depth indent-depth)))
      (cond ((< depth-delta 0)
             (setq indent-stack (nthcdr (- depth-delta) indent-stack)))
            ((> depth-delta 0)
             (setq indent-stack (nconc (make-list depth-delta nil)
                                       indent-stack)))))
    (prog1
        (let (indent)
          (cond ((= (forward-line 1) 1)
                 ;; Can't move to the next line, apparently end of buffer.
                 nil)
                ((null indent-stack)
                 ;; Negative depth, probably some kind of syntax
                 ;; error.  Reset the state.
                 (setq ppss (parse-partial-sexp (point) (point))))
                ((car indent-stack))
                ((integerp (setq indent (calculate-lisp-indent ppss)))
                 (setf (car indent-stack) indent))
                ((consp indent)       ; (COLUMN CONTAINING-SEXP-START)
                 (car indent))
                ;; This only happens if we're in a string, but the
                ;; loop should always skip over strings (unless we hit
                ;; end of buffer, which is taken care of by the first
                ;; clause).
                (t (error "This shouldn't happen"))))
      (setf (lisp-indent-state-stack state) indent-stack)
      (setf (lisp-indent-state-ppss-point state) ppss-point)
      (setf (lisp-indent-state-ppss state) ppss))))

(defun lisp-indent-region (start end)
  "Indent region as Lisp code, efficiently."
  (save-excursion
    (setq end (copy-marker end))
    (goto-char start)
    (beginning-of-line)
    ;; The default `indent-region-line-by-line' doesn't hold a running
    ;; parse state, which forces each indent call to reparse from the
    ;; beginning.  That has O(n^2) complexity.
    (let* ((parse-state (lisp-indent-initial-state))
           (pr (unless (minibufferp)
                 (make-progress-reporter "Indenting region..." (point) end))))
      (let ((ppss (lisp-indent-state-ppss parse-state)))
        (unless (or (and (bolp) (eolp)) (nth 3 ppss))
          (lisp-indent-line (calculate-lisp-indent ppss))))
      (let ((indent nil))
        (while (progn (setq indent (lisp-indent-calc-next parse-state))
                      (< (point) end))
          (unless (or (and (bolp) (eolp)) (not indent))
            (lisp-indent-line indent))
          (and pr (progress-reporter-update pr (point)))))
      (and pr (progress-reporter-done pr))
      (move-marker end nil))))

(defun lisp-indent-line (&optional indent)
  "Indent current line as Lisp code."
  (interactive)
  (let ((pos (- (point-max) (point)))
        (indent (progn (beginning-of-line)
                       (or indent (calculate-lisp-indent (lisp-ppss))))))
    (skip-chars-forward " \t")
    (if (or (null indent) (looking-at "\\s<\\s<\\s<"))
	;; Don't alter indentation of a ;;; comment line
	;; or a line that starts in a string.
        ;; FIXME: inconsistency: comment-indent moves ;;; to column 0.
	(goto-char (- (point-max) pos))
      (if (and (looking-at "\\s<") (not (looking-at "\\s<\\s<")))
	  ;; Single-semicolon comment lines should be indented
	  ;; as comment lines, not as code.
	  (progn (indent-for-comment) (forward-char -1))
	(if (listp indent) (setq indent (car indent)))
        (indent-line-to indent))
      ;; If initial point was within line's indentation,
      ;; position after the indentation.  Else stay at same point in text.
      (if (> (- (point-max) pos) (point))
	  (goto-char (- (point-max) pos))))))

(defvar calculate-lisp-indent-last-sexp)

(defun calculate-lisp-indent (&optional parse-start)
  "Return appropriate indentation for current line as Lisp code.
In usual case returns an integer: the column to indent to.
If the value is nil, that means don't change the indentation
because the line starts inside a string.

PARSE-START may be a buffer position to start parsing from, or a
parse state as returned by calling `parse-partial-sexp' up to the
beginning of the current line.

The value can also be a list of the form (COLUMN CONTAINING-SEXP-START).
This means that following lines at the same level of indentation
should not necessarily be indented the same as this line.
Then COLUMN is the column to indent to, and CONTAINING-SEXP-START
is the buffer position of the start of the containing expression."
  (save-excursion
    (beginning-of-line)
    (let ((indent-point (point))
          state
          ;; setting this to a number inhibits calling hook
          (desired-indent nil)
          (retry t)
          whitespace-after-open-paren
          calculate-lisp-indent-last-sexp containing-sexp)
      (cond ((or (markerp parse-start) (integerp parse-start))
             (goto-char parse-start))
            ((null parse-start) (beginning-of-defun))
            (t (setq state parse-start)))
      (unless state
        ;; Find outermost containing sexp
        (while (< (point) indent-point)
          (setq state (parse-partial-sexp (point) indent-point 0))))
      ;; Find innermost containing sexp
      (while (and retry
		  state
                  (> (elt state 0) 0))
        (setq retry nil)
        (setq calculate-lisp-indent-last-sexp (elt state 2))
        (setq containing-sexp (elt state 1))
        ;; Position following last unclosed open.
        (goto-char (1+ containing-sexp))
        ;; Is there a complete sexp since then?
        (if (and calculate-lisp-indent-last-sexp
		 (> calculate-lisp-indent-last-sexp (point)))
            ;; Yes, but is there a containing sexp after that?
            (let ((peek (parse-partial-sexp calculate-lisp-indent-last-sexp
					    indent-point 0)))
              (if (setq retry (car (cdr peek))) (setq state peek)))))
      (if retry
          nil
        ;; Innermost containing sexp found
        (goto-char (1+ containing-sexp))
        (setq whitespace-after-open-paren (looking-at (rx whitespace)))
        (if (not calculate-lisp-indent-last-sexp)
	    ;; indent-point immediately follows open paren.
	    ;; Don't call hook.
            (setq desired-indent (current-column))
	  ;; Find the start of first element of containing sexp.
	  (parse-partial-sexp (point) calculate-lisp-indent-last-sexp 0 t)
	  (cond ((looking-at "\\s(")
		 ;; First element of containing sexp is a list.
		 ;; Indent under that list.
		 )
		((> (save-excursion (forward-line 1) (point))
		    calculate-lisp-indent-last-sexp)
		 ;; This is the first line to start within the containing sexp.
		 ;; It's almost certainly a function call.
		 (if (or (= (point) calculate-lisp-indent-last-sexp)
                         whitespace-after-open-paren)
		     ;; Containing sexp has nothing before this line
		     ;; except the first element, or the first element is
                     ;; preceded by whitespace.  Indent under that element.
		     nil
		   ;; Skip the first element, find start of second (the first
		   ;; argument of the function call) and indent under.
		   (progn (forward-sexp 1)
			  (parse-partial-sexp (point)
					      calculate-lisp-indent-last-sexp
					      0 t)))
		 (backward-prefix-chars))
		(t
		 ;; Indent beneath first sexp on same line as
		 ;; `calculate-lisp-indent-last-sexp'.  Again, it's
		 ;; almost certainly a function call.
		 (goto-char calculate-lisp-indent-last-sexp)
		 (beginning-of-line)
		 (parse-partial-sexp (point) calculate-lisp-indent-last-sexp
				     0 t)
		 (backward-prefix-chars)))))
      ;; Point is at the point to indent under unless we are inside a string.
      ;; Call indentation hook except when overridden by lisp-indent-offset
      ;; or if the desired indentation has already been computed.
      (let ((normal-indent (current-column)))
        (cond ((elt state 3)
               ;; Inside a string, don't change indentation.
	       nil)
              ((and (integerp lisp-indent-offset) containing-sexp)
               ;; Indent by constant offset
               (goto-char containing-sexp)
               (+ (current-column) lisp-indent-offset))
              ;; in this case calculate-lisp-indent-last-sexp is not nil
              (calculate-lisp-indent-last-sexp
               (or
                ;; try to align the parameters of a known function
                (and lisp-indent-function
                     (not retry)
                     (funcall lisp-indent-function indent-point state))
                ;; If the function has no special alignment
		;; or it does not apply to this argument,
		;; try to align a constant-symbol under the last
                ;; preceding constant symbol, if there is such one of
                ;; the last 2 preceding symbols, in the previous
                ;; uncommented line.
                (and (save-excursion
                       (goto-char indent-point)
                       (skip-chars-forward " \t")
                       (looking-at ":"))
                     ;; The last sexp may not be at the indentation
                     ;; where it begins, so find that one, instead.
                     (save-excursion
                       (goto-char calculate-lisp-indent-last-sexp)
		       ;; Handle prefix characters and whitespace
		       ;; following an open paren.  (Bug#1012)
                       (backward-prefix-chars)
                       (while (not (save-excursion
                                     (skip-chars-backward " \t")
                                     (or (= (point) (line-beginning-position))
                                         (and containing-sexp
                                              (= (point) (1+ containing-sexp))))))
                         (forward-sexp -1)
                         (backward-prefix-chars))
                       (setq calculate-lisp-indent-last-sexp (point)))
                     (> calculate-lisp-indent-last-sexp
                        (save-excursion
                          (goto-char (1+ containing-sexp))
                          (parse-partial-sexp (point) calculate-lisp-indent-last-sexp 0 t)
                          (point)))
                     (let ((parse-sexp-ignore-comments t)
                           indent)
                       (goto-char calculate-lisp-indent-last-sexp)
                       (or (and (looking-at ":")
                                (setq indent (current-column)))
                           (and (< (line-beginning-position)
                                   (prog2 (backward-sexp) (point)))
                                (looking-at ":")
                                (setq indent (current-column))))
                       indent))
                ;; another symbols or constants not preceded by a constant
                ;; as defined above.
                normal-indent))
              ;; in this case calculate-lisp-indent-last-sexp is nil
              (desired-indent)
              (t
               normal-indent))))))

(defun lisp--local-defform-body-p (state)
  "Return non-nil when at local definition body according to STATE.
STATE is the `parse-partial-sexp' state for current position."
  (when-let* ((start-of-innermost-containing-list (nth 1 state)))
    (let* ((parents (nth 9 state))
           (first-cons-after (cdr parents))
           (second-cons-after (cdr first-cons-after))
           first-order-parent second-order-parent)
      (while second-cons-after
        (when (= start-of-innermost-containing-list
                 (car second-cons-after))
          (setq second-order-parent (pop parents)
                first-order-parent (pop parents)
                ;; Leave the loop.
                second-cons-after nil))
        (pop second-cons-after)
        (pop parents))
      (when second-order-parent
        (let (local-definitions-starting-point)
          (and (save-excursion
                 (goto-char (1+ second-order-parent))
                 (when-let* ((head (ignore-errors
                                     ;; FIXME: This does not distinguish
                                     ;; between reading nil and a read error.
                                     ;; We don't care but still, better fix this.
                                     (read (current-buffer)))))
                   (when (memq head '( cl-flet cl-labels cl-macrolet cl-flet*
                                       cl-symbol-macrolet))
                     ;; In what follows, we rely on (point) returning non-nil.
                     (setq local-definitions-starting-point
                           (progn
                             (parse-partial-sexp
                              (point) first-order-parent nil
                              ;; From docstring of `parse-partial-sexp':
                              ;; Fourth arg non-nil means stop
                              ;; when we come to any character
                              ;; that starts a sexp.
                              t)
                             (point))))))
               (save-excursion
                 (when (ignore-errors
                         ;; We rely on `backward-up-list' working
                         ;; even when sexp is incomplete “to the right”.
                         (backward-up-list 2)
                         t)
                   (= local-definitions-starting-point (point))))))))))

(defun lisp-indent-function (indent-point state)
  "This function is the normal value of the variable `lisp-indent-function'.
The function `calculate-lisp-indent' calls this to determine
if the arguments of a Lisp function call should be indented specially.

INDENT-POINT is the position at which the line being indented begins.
Point is located at the point to indent under (for default indentation);
STATE is the `parse-partial-sexp' state for that position.

If the current line is in a call to a Lisp function that has a non-nil
property `lisp-indent-function' (or the deprecated `lisp-indent-hook'),
it specifies how to indent.  The property value can be:

* `defun', meaning indent `defun'-style
  (this is also the case if there is no property and the function
  has a name that begins with \"def\", and three or more arguments);

* an integer N, meaning indent the first N arguments specially
  (like ordinary function arguments), and then indent any further
  arguments like a body;

* a function to call that returns the indentation (or nil).
  `lisp-indent-function' calls this function with the same two arguments
  that it itself received.

This function returns either the indentation to use, or nil if the
Lisp function does not specify a special indentation."
  (let ((normal-indent (current-column)))
    (goto-char (1+ (elt state 1)))
    (parse-partial-sexp (point) calculate-lisp-indent-last-sexp 0 t)
    (if (and (elt state 2)
             (not (looking-at "\\sw\\|\\s_")))
        ;; car of form doesn't seem to be a symbol
        (if (lisp--local-defform-body-p state)
            ;; We nevertheless check whether we are in flet-like form
            ;; as we presume local function names could be non-symbols.
            (lisp-indent-defform state indent-point)
          (if (not (> (save-excursion (forward-line 1) (point))
                      calculate-lisp-indent-last-sexp))
	      (progn (goto-char calculate-lisp-indent-last-sexp)
		     (beginning-of-line)
		     (parse-partial-sexp (point)
					 calculate-lisp-indent-last-sexp 0 t)))
	  ;; Indent under the list or under the first sexp on the same
	  ;; line as calculate-lisp-indent-last-sexp.  Note that first
	  ;; thing on that line has to be complete sexp since we are
          ;; inside the innermost containing sexp.
          (backward-prefix-chars)
          (current-column))
      (let ((function (buffer-substring (point)
					(progn (forward-sexp 1) (point))))
	    method)
	(setq method (or (function-get (intern-soft function)
                                       'lisp-indent-function)
			 (get (intern-soft function) 'lisp-indent-hook)))
	(cond ((or (eq method 'defun)
                   ;; Check whether we are in flet-like form.
                   (lisp--local-defform-body-p state))
	       (lisp-indent-defform state indent-point))
	      ((integerp method)
	       (lisp-indent-specform method state
				     indent-point normal-indent))
	      (method
	       (funcall method indent-point state)))))))

(defcustom lisp-body-indent 2
  "Number of columns to indent the second line of a `(def...)' form."
  :group 'lisp
  :type 'integer
  :safe #'integerp)

(defun lisp-indent-specform (count state indent-point normal-indent)
  (let ((containing-form-start (elt state 1))
        (i count)
        body-indent containing-form-column)
    ;; Move to the start of containing form, calculate indentation
    ;; to use for non-distinguished forms (> count), and move past the
    ;; function symbol.  lisp-indent-function guarantees that there is at
    ;; least one word or symbol character following open paren of containing
    ;; form.
    (goto-char containing-form-start)
    (setq containing-form-column (current-column))
    (setq body-indent (+ lisp-body-indent containing-form-column))
    (forward-char 1)
    (forward-sexp 1)
    ;; Now find the start of the last form.
    (parse-partial-sexp (point) indent-point 1 t)
    (while (and (< (point) indent-point)
                (condition-case ()
                    (progn
                      (setq count (1- count))
                      (forward-sexp 1)
                      (parse-partial-sexp (point) indent-point 1 t))
                  (error nil))))
    ;; Point is sitting on first character of last (or count) sexp.
    (if (> count 0)
        ;; A distinguished form.  If it is the first or second form use double
        ;; lisp-body-indent, else normal indent.  With lisp-body-indent bound
        ;; to 2 (the default), this just happens to work the same with if as
        ;; the older code, but it makes unwind-protect, condition-case,
        ;; with-output-to-temp-buffer, et. al. much more tasteful.  The older,
        ;; less hacked, behavior can be obtained by replacing below with
        ;; (list normal-indent containing-form-start).
        (if (<= (- i count) 1)
            (list (+ containing-form-column (* 2 lisp-body-indent))
                  containing-form-start)
            (list normal-indent containing-form-start))
      ;; A non-distinguished form.  Use body-indent if there are no
      ;; distinguished forms and this is the first undistinguished form,
      ;; or if this is the first undistinguished form and the preceding
      ;; distinguished form has indentation at least as great as body-indent.
      (if (or (and (= i 0) (= count 0))
              (and (= count 0) (<= body-indent normal-indent)))
          body-indent
          normal-indent))))

(defun lisp-indent-defform (state _indent-point)
  (goto-char (car (cdr state)))
  (forward-line 1)
  (if (> (point) (car (cdr (cdr state))))
      (progn
	(goto-char (car (cdr state)))
	(+ lisp-body-indent (current-column)))))


;; (put 'progn 'lisp-indent-function 0), say, causes progn to be indented
;; like defun if the first form is placed on the next line, otherwise
;; it is indented like any other form (i.e. forms line up under first).

(put 'autoload 'lisp-indent-function 'defun) ;Elisp
(put 'progn 'lisp-indent-function 0)
(put 'defvar 'lisp-indent-function 'defun)
(put 'defalias 'lisp-indent-function 'defun)
(put 'defvaralias 'lisp-indent-function 'defun)
(put 'defconst 'lisp-indent-function 'defun)
(put 'define-category 'lisp-indent-function 'defun)
(put 'define-charset-internal 'lisp-indent-function 'defun)
(put 'define-fringe-bitmap 'lisp-indent-function 'defun)
(put 'prog1 'lisp-indent-function 1)
(put 'save-excursion 'lisp-indent-function 0)      ;Elisp
(put 'save-restriction 'lisp-indent-function 0)    ;Elisp
(put 'save-current-buffer 'lisp-indent-function 0) ;Elisp
(put 'let 'lisp-indent-function 1)
(put 'let* 'lisp-indent-function 1)
(put 'while 'lisp-indent-function 1)
(put 'if 'lisp-indent-function 2)
(put 'catch 'lisp-indent-function 1)
(put 'condition-case 'lisp-indent-function 2)
(put 'handler-case 'lisp-indent-function 1) ;CL
(put 'unwind-protect 'lisp-indent-function 1)

(defun indent-sexp (&optional endpos)
  "Indent each line of the list starting just after point.
If optional arg ENDPOS is given, indent each line, stopping when
ENDPOS is encountered."
  (interactive)
  (let* ((parse-state (lisp-indent-initial-state)))
    ;; We need a marker because we modify the buffer
    ;; text preceding endpos.
    (setq endpos (copy-marker
                  (if endpos endpos
                    ;; Get error now if we don't have a complete sexp
                    ;; after point.
                    (save-excursion
                      (forward-sexp 1)
                      (let ((eol (line-end-position)))
                        ;; We actually look for a sexp which ends
                        ;; after the current line so that we properly
                        ;; indent things like #s(...).  This might not
                        ;; be needed if Bug#15998 is fixed.
                        (when (and (< (point) eol)
                                   ;; Check if eol is within a sexp.
                                   (> (nth 0 (save-excursion
                                               (parse-partial-sexp
                                                (point) eol)))
                                      0))
                          (condition-case ()
                              (while (< (point) eol)
                                (forward-sexp 1))
                            ;; But don't signal an error for incomplete
                            ;; sexps following the first complete sexp
                            ;; after point.
                            (scan-error nil))))
                      (point)))))
    (save-excursion
      (while (let ((indent (lisp-indent-calc-next parse-state))
                   (ppss (lisp-indent-state-ppss parse-state)))
               ;; If the line contains a comment indent it now with
               ;; `indent-for-comment'.
               (when (and (nth 4 ppss) (<= (nth 8 ppss) endpos))
                 (save-excursion
                   (goto-char (lisp-indent-state-ppss-point parse-state))
                   (indent-for-comment)
                   (setf (lisp-indent-state-ppss-point parse-state)
                         (line-end-position))))
               (when (< (point) endpos)
                 ;; Indent the next line, unless it's blank, or just a
                 ;; comment (we will `indent-for-comment' the latter).
                 (skip-chars-forward " \t")
                 (unless (or (eolp) (not indent)
                             (eq (char-syntax (char-after)) ?<))
                   (indent-line-to indent))
                 t))))
    (move-marker endpos nil)))

(defun indent-pp-sexp (&optional arg)
  "Indent each line of the list starting just after point, or prettyprint it.
A prefix argument specifies pretty-printing."
  (interactive "P")
  (if arg
      (save-excursion
        (save-restriction
          (narrow-to-region (point) (progn (forward-sexp 1) (point)))
          (pp-buffer)
          (goto-char (point-max))
          (if (eq (char-before) ?\n)
              (delete-char -1)))))
  (indent-sexp))

;;;; Lisp paragraph filling commands.

(defcustom emacs-lisp-docstring-fill-column 72
  "Value of `fill-column' to use when filling a docstring.
Any non-integer value means do not use a different value of
`fill-column' when filling docstrings."
  :type '(choice (integer)
                 (const :tag "Use the current `fill-column'" t))
  :safe (lambda (x) (or (eq x t) (integerp x)))
  :group 'lisp
  :version "30.1")

(defvar lisp-fill-paragraphs-as-doc-string t
  "Whether `lisp-fill-paragraph' should fill strings as Elisp doc strings.
The default behavior of `lisp-fill-paragraph' is tuned for filling Emacs
Lisp doc strings, with their special treatment for the first line.
Specifically, strings are filled in a narrowed context to avoid filling
surrounding code, which means any leading indent is disregarded, which
can cause the filled string to extend passed the configured
`fill-column' variable value.  If you would rather fill the string in
its original context, disregarding the special conventions of Elisp doc
strings, and want to ensure the `fill-column' value is more strictly
respected, set this variable to nil.  Doing so makes
`lisp-fill-paragraph' behave as it used to in Emacs 27 and prior
versions.")

(defun lisp-fill-paragraph (&optional justify)
  "Like \\[fill-paragraph], but handle Emacs Lisp comments and docstrings.
If any of the current line is a comment, fill the comment or the
paragraph of it that point is in, preserving the comment's indentation
and initial semicolons."
  (interactive "P")
  (or (fill-comment-paragraph justify)
      ;; Since fill-comment-paragraph returned nil, that means we're not in
      ;; a comment: Point is on a program line; we are interested
      ;; particularly in docstring lines.
      ;;
      ;; FIXME: The below bindings are probably mostly irrelevant
      ;; since we're now narrowing to a region before filling.
      ;;
      ;; We bind `paragraph-start' and `paragraph-separate' temporarily.  They
      ;; are buffer-local, but we avoid changing them so that they can be set
      ;; to make `forward-paragraph' and friends do something the user wants.
      ;;
      ;; `paragraph-start': The `(' in the bracket expression and the
      ;; left-singlequote plus `(' sequence after the \\| alternative prevent
      ;; sexps and backquoted sexps that follow a docstring from being filled
      ;; with the docstring.  This setting has the consequence of inhibiting
      ;; filling many program lines that are not docstrings, which is sensible,
      ;; because the user probably asked to fill program lines by accident, or
      ;; expecting indentation (perhaps we should try to do indenting in that
      ;; case).  The `;' and `:' stop the paragraph being filled at following
      ;; comment lines and at keywords (e.g., in `defcustom').  Left parens are
      ;; escaped to keep font-locking, filling, & paren matching in the source
      ;; file happy.  The `:' must be preceded by whitespace so that keywords
      ;; inside of the docstring don't start new paragraphs (Bug#7751).
      ;;
      ;; `paragraph-separate': A clever regexp distinguishes the first line of
      ;; a docstring and identifies it as a paragraph separator, so that it
      ;; won't be filled.  (Since the first line of documentation stands alone
      ;; in some contexts, filling should not alter the contents the author has
      ;; chosen.)  Only the first line of a docstring begins with whitespace
      ;; and a quotation mark and ends with a period or (rarely) a comma.
      ;;
      ;; The `fill-column' is temporarily bound to
      ;; `emacs-lisp-docstring-fill-column' if that value is an integer.
      (let ((paragraph-start
             (concat paragraph-start
                     "\\|\\s-*\\([(;\"]\\|\\s-:\\|`(\\|#'(\\)"))
	    (paragraph-separate
	     (concat paragraph-separate "\\|\\s-*\".*[,\\.]$"))
            (fill-column (if (and (integerp emacs-lisp-docstring-fill-column)
                                  (derived-mode-p 'emacs-lisp-mode))
                             emacs-lisp-docstring-fill-column
                           fill-column)))
        (let* ((ppss (syntax-ppss))
               (start (point))
               ;; Avoid recursion if we're being called directly with
               ;; `M-x lisp-fill-paragraph' in an `emacs-lisp-mode' buffer.
               (fill-paragraph-function t)
               (string-start (ppss-comment-or-string-start ppss)))
          (save-excursion
            (save-restriction
              ;; If we're not inside a string, then do very basic
              ;; filling.  This avoids corrupting embedded strings in
              ;; code.
              (if (not string-start)
                  (lisp--fill-line-simple)
                (when lisp-fill-paragraphs-as-doc-string
                  ;; If we're in a string, then narrow (roughly) to that
                  ;; string before filling.  This avoids filling Lisp
                  ;; statements that follow the string.
                  (when (ppss-string-terminator ppss)
                    (goto-char string-start)
                    ;; The string may be unterminated -- in that case, don't
                    ;; narrow.
                    (when (ignore-errors
                            (progn
                              (forward-sexp 1)
                              t))
                      (narrow-to-region (1+ string-start)
                                        (1- (point)))))
                  ;; Move back to where we were.
                  (goto-char start)
                  ;; We should fill the first line of a string
                  ;; separately (since it's usually a doc string).
                  (if (= (line-number-at-pos) 1)
                      (narrow-to-region (line-beginning-position)
                                        (line-beginning-position 2))
                    (save-excursion
                      (goto-char (point-min))
                      (forward-line 1)
                      (narrow-to-region (point) (point-max)))))
	        (fill-paragraph justify)))))))
  ;; Never return nil.
  t)

(defun lisp--fill-line-simple ()
  (narrow-to-region (line-beginning-position) (line-end-position))
  (goto-char (point-min))
  (while (and (not (eobp))
              (re-search-forward "\\_>" nil t))
    (when (> (current-column) fill-column)
      (let ((start (point)))
        (backward-sexp)
        (if (looking-back "[[(]" (point-min))
            (goto-char start)
          (skip-chars-backward " \t")
          (insert "\n")
          (forward-sexp))))
    (unless (eobp)
      (forward-char 1))))

(defun indent-code-rigidly (start end arg &optional nochange-regexp)
  "Indent all lines of code, starting in the region, sideways by ARG columns.
Does not affect lines starting inside comments or strings, assuming that
the start of the region is not inside them.

Called from a program, takes args START, END, COLUMNS and NOCHANGE-REGEXP.
The last is a regexp which, if matched at the beginning of a line,
means don't indent that line."
  (interactive "r\np")
  (let (state)
    (save-excursion
      (goto-char end)
      (setq end (point-marker))
      (goto-char start)
      (or (bolp)
	  (setq state (parse-partial-sexp (point)
					  (progn
					    (forward-line 1) (point))
					  nil nil state)))
      (while (< (point) end)
	(or (car (nthcdr 3 state))
	    (and nochange-regexp
		 (looking-at nochange-regexp))
	    ;; If line does not start in string, indent it
	    (let ((indent (current-indentation)))
	      (delete-region (point) (progn (skip-chars-forward " \t") (point)))
	      (or (eolp)
		  (indent-to (max 0 (+ indent arg)) 0))))
	(setq state (parse-partial-sexp (point)
					(progn
					  (forward-line 1) (point))
					nil nil state))))))

(provide 'lisp-mode)

;;; lisp-mode.el ends here
