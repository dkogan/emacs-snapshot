;;; em-xtra.el --- extra alias functions  -*- lexical-binding:t -*-

;; Copyright (C) 1999-2025 Free Software Foundation, Inc.

;; Author: John Wiegley <johnw@gnu.org>

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

(require 'cl-lib)
(require 'esh-util)

;; There are no items in this custom group, but eshell modules (ab)use
;; custom groups.
;;;###esh-module-autoload
(progn
(defgroup eshell-xtra nil
  "This module defines some extra alias functions which are entirely
optional.  They can be viewed as samples for how to write Eshell alias
functions, or as aliases which make some of Emacs's behavior more
naturally accessible within Emacs."
  :tag "Extra alias functions"
  :group 'eshell-module))

;;; Functions:

(defun eshell/expr (&rest args)
  "Implementation of expr, using the calc package."
  (calc-eval (eshell-flatten-and-stringify args)))

(defun eshell/substitute (new old seq &rest args)
  "Easy front-end to `cl-substitute', for comparing lists of strings."
  (apply #'cl-substitute new old seq :test #'equal args))

(defun eshell/count (item seq &rest args)
  "Easy front-end to `cl-count', for comparing lists of strings."
  (apply #'cl-count item seq :test #'equal args))

(defun eshell/mismatch (seq1 seq2 &rest args)
  "Easy front-end to `cl-mismatch', for comparing lists of strings."
  (apply #'cl-mismatch seq1 seq2 :test #'equal args))

(defun eshell/union (list1 list2 &rest args)
  "Easy front-end to `cl-union', for comparing lists of strings."
  (apply #'cl-union list1 list2 :test #'equal args))

(defun eshell/intersection (list1 list2 &rest args)
  "Easy front-end to `cl-intersection', for comparing lists of strings."
  (apply #'cl-intersection list1 list2 :test #'equal args))

(defun eshell/set-difference (list1 list2 &rest args)
  "Easy front-end to `cl-set-difference', for comparing lists of strings."
  (apply #'cl-set-difference list1 list2 :test #'equal args))

(defun eshell/set-exclusive-or (list1 list2 &rest args)
  "Easy front-end to `cl-set-exclusive-or', for comparing lists of strings."
  (apply #'cl-set-exclusive-or list1 list2 :test #'equal args))

(defalias 'eshell/ff #'find-name-dired)
(defalias 'eshell/gf #'find-grep-dired)

(provide 'em-xtra)
;;; em-xtra.el ends here
