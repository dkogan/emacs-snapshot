;;; dom.el --- XML/HTML (etc.) DOM manipulation and searching functions -*- lexical-binding: t -*-

;; Copyright (C) 2014-2025 Free Software Foundation, Inc.

;; Author: Lars Magne Ingebrigtsen <larsi@gnus.org>
;; Keywords: xml, html

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
(require 'subr-x)

(defsubst dom-tag (node)
  "Return the NODE tag."
  ;; Called on a list of nodes.  Use the first.
  (car (if (consp (car node)) (car node) node)))

(defsubst dom-attributes (node)
  "Return the NODE attributes."
  ;; Called on a list of nodes.  Use the first.
  (cadr (if (consp (car node)) (car node) node)))

(defsubst dom-children (node)
  "Return the NODE children."
  ;; Called on a list of nodes.  Use the first.
  (cddr (if (consp (car node)) (car node) node)))

(defun dom-non-text-children (node)
  "Return all non-text-node children of NODE."
  (cl-loop for child in (dom-children node)
	   unless (stringp child)
	   collect child))

(defun dom-set-attributes (node attributes)
  "Set the attributes of NODE to ATTRIBUTES."
  (setq node (dom-ensure-node node))
  (setcar (cdr node) attributes))

(defun dom-set-attribute (node attribute value)
  "Set ATTRIBUTE in NODE to VALUE."
  (setq node (dom-ensure-node node))
  (let* ((attributes (cadr node))
         (old (assoc attribute attributes)))
    (if old
	(setcdr old value)
      (setcar (cdr node) (cons (cons attribute value) attributes)))))

(defun dom-remove-attribute (node attribute)
  "Remove ATTRIBUTE from NODE."
  (setq node (dom-ensure-node node))
  (when-let* ((old (assoc attribute (cadr node))))
    (setcar (cdr node) (delq old (cadr node)))))

(defmacro dom-attr (node attr)
  "Return the attribute ATTR from NODE.
A typical attribute is `href'."
  `(cdr (assq ,attr (dom-attributes ,node))))

(defun dom-text (node)
  "Return all the text bits in the current node concatenated."
  (declare (obsolete 'dom-inner-text "31.1"))
  (mapconcat #'identity (cl-remove-if-not #'stringp (dom-children node)) " "))

(defun dom-texts (node &optional separator)
  "Return all textual data under NODE concatenated with SEPARATOR in-between."
  (declare (obsolete 'dom-inner-text "31.1"))
  (if (eq (dom-tag node) 'script)
      ""
    (mapconcat
     (lambda (elem)
       (cond
        ((stringp elem)
         elem)
        ((eq (dom-tag elem) 'script)
         "")
        (t
         (dom-texts elem separator))))
     (dom-children node)
     (or separator " "))))

(defun dom-inner-text--1 (node)
  (dolist (child (dom-children node))
    (cond
     ((stringp child) (insert child))
     ((memq (dom-tag child) '(script comment)))
     (t (dom-inner-text--1 child)))))

(defun dom-inner-text (node)
  "Return all textual data under NODE as a single string."
  (let ((children (dom-children node)))
    (if (and (length= children 1)
             (stringp (car children)))
        ;; Copy the string content when returning to be consistent with
        ;; the other branch of this `if' expression.
        (copy-sequence (car children))
    (with-work-buffer
      (dom-inner-text--1 node)
      (buffer-string)))))

(defun dom-child-by-tag (dom tag)
  "Return the first child of DOM that is of type TAG."
  (assoc tag (dom-children dom)))

(defun dom-by-tag (dom tag)
  "Return elements in DOM that is of type TAG.
A name is a symbol like `td'."
  (let ((matches (cl-loop for child in (dom-children dom)
			  for matches = (and (not (stringp child))
					     (dom-by-tag child tag))
			  when matches
			  append matches)))
    (if (equal (dom-tag dom) tag)
	(cons dom matches)
      matches)))

(defun dom-search (dom predicate)
  "Return elements in DOM where PREDICATE is non-nil.
PREDICATE is called with the node as its only parameter."
  (let ((matches (cl-loop for child in (dom-children dom)
			  for matches = (and (not (stringp child))
					     (dom-search child predicate))
			  when matches
			  append matches)))
    (if (funcall predicate dom)
	(cons dom matches)
      matches)))

(defun dom-strings (dom)
  "Return elements in DOM that are strings."
  (cl-loop for child in (dom-children dom)
	   if (stringp child)
	   collect child
	   else
	   append (dom-strings child)))

(defun dom-by-class (dom match)
  "Return elements in DOM that have a class name that matches regexp MATCH."
  (dom-elements dom 'class match))

(defun dom-by-style (dom match)
  "Return elements in DOM that have a style that matches regexp MATCH."
  (dom-elements dom 'style match))

(defun dom-by-id (dom match)
  "Return elements in DOM that have an ID that matches regexp MATCH."
  (dom-elements dom 'id match))

(defun dom-elements (dom attribute match)
  "Find elements matching MATCH (a regexp) in ATTRIBUTE.
ATTRIBUTE would typically be `class', `id' or the like."
  (let ((matches (cl-loop for child in (dom-children dom)
			  for matches = (and (not (stringp child))
					     (dom-elements child attribute
							   match))
			  when matches
			  append matches))
	(attr (dom-attr dom attribute)))
    (if (and attr
	     (string-match match attr))
	(cons dom matches)
      matches)))

(defun dom-remove-node (dom node)
  "Remove NODE from DOM."
  ;; If we're removing the top level node, just return nil.
  (dolist (child (dom-children dom))
    (cond
     ((eq node child)
      (delq node dom))
     ((not (stringp child))
      (dom-remove-node child node)))))

(defun dom-parent (dom node)
  "Return the parent of NODE in DOM."
  (if (memq node (dom-children dom))
      dom
    (let ((result nil))
      (dolist (elem (dom-children dom))
	(when (and (not result)
		   (not (stringp elem)))
	  (setq result (dom-parent elem node))))
      result)))

(defun dom-previous-sibling (dom node)
  "Return the previous sibling of NODE in DOM."
  (when-let* ((parent (dom-parent dom node)))
    (let ((siblings (dom-children parent))
	  (previous nil))
      (while siblings
	(when (eq (cadr siblings) node)
	  (setq previous (car siblings)))
	(pop siblings))
      previous)))

(defun dom-node (tag &optional attributes &rest children)
  "Return a DOM node with TAG and ATTRIBUTES."
  `(,tag ,attributes ,@children))

(defun dom-append-child (node child)
  "Append CHILD to the end of NODE's children."
  (setq node (dom-ensure-node node))
  (nconc node (list child)))

(defun dom-add-child-before (node child &optional before)
  "Add CHILD to NODE's children before child BEFORE.
If BEFORE is nil, make CHILD NODE's first child."
  (setq node (dom-ensure-node node))
  (let ((children (dom-children node)))
    (when (and before
	       (not (memq before children)))
      (error "%s does not exist as a child" before))
    (let ((pos (if before
		   (cl-position before children)
		 0)))
      (push child (nthcdr (+ 2 pos) node))))
  node)

(defun dom-ensure-node (node)
  "Ensure that NODE is a proper DOM node."
  ;; Add empty attributes, if none.
  (when (consp (car node))
    (setq node (car node)))
  (when (= (length node) 1)
    (setcdr node (list nil)))
  node)

(defun dom-pp (dom &optional remove-empty)
  "Pretty-print DOM at point.
If REMOVE-EMPTY, ignore textual nodes that contain just
white-space."
  (let ((column (current-column)))
    (insert (format "(%S " (dom-tag dom)))
    (let* ((attr (dom-attributes dom))
	   (times (length attr))
	   (column (1+ (current-column))))
      (if (null attr)
	  (insert "nil")
	(insert "(")
	(dolist (elem attr)
	  (insert (format "(%S . %S)" (car elem) (cdr elem)))
          (if (zerop (decf times))
	      (insert ")")
	    (insert "\n" (make-string column ?\s))))))
    (let* ((children (if remove-empty
			 (cl-remove-if
			  (lambda (child)
			    (and (stringp child)
				 (string-match "\\`[\n\r\t  ]*\\'" child)))
			  (dom-children dom))
		       (dom-children dom)))
	   (times (length children)))
      (if (null children)
	  (insert ")")
	(insert "\n" (make-string (1+ column) ?\s))
	(dolist (child children)
	  (if (stringp child)
	      (if (not (and remove-empty
		            (string-match "\\`[\n\r\t  ]*\\'" child)))
		  (insert (format "%S" child)))
	    (dom-pp child remove-empty))
          (if (zerop (decf times))
	      (insert ")")
	    (insert "\n" (make-string (1+ column) ?\s))))))))

(define-inline dom--html-boolean-attribute-p (attr)
  "Return non-nil if ATTR is an HTML boolean attribute."
  (inline-quote
   (memq ,attr
         ;; Extracted from the HTML Living Standard list of attributes
         ;; at <https://html.spec.whatwg.org/#attributes-3>.
         '( allowfullscreen alpha async autofocus autoplay checked
            controls default defer disabled formnovalidate inert ismap
            itemscope loop multiple muted nomodule novalidate open
            playsinline readonly required reversed selected
            shadowrootclonable shadowrootdelegatesfocus
            shadowrootserializable))))

(defun dom-print (dom &optional pretty xml)
  "Print DOM at point as HTML/XML.
If PRETTY, indent the HTML/XML logically.
If XML, generate XML instead of HTML."
  (let ((column (current-column))
        (indent-tabs-mode nil)) ;; Indent with spaces
    (insert (format "<%s" (dom-tag dom)))
    (pcase-dolist (`(,attr . ,value) (dom-attributes dom))
      ;; Don't print attributes without a value.
      (when value
        (insert
         ;; HTML boolean attributes should not have an = value.  The
         ;; presence of a boolean attribute on an element represents
         ;; the true value, and the absence of the attribute
         ;; represents the false value.
         (if (and (not xml) (dom--html-boolean-attribute-p attr))
             (format " %s" attr)
           (format " %s=%S" attr (url-insert-entities-in-string
                                  (format "%s" value)))))))
    (let* ((children (dom-children dom))
	   (non-text nil)
           (indent (+ column 2)))
      (if (null children)
	  (insert " />")
	(insert ">")
        (dolist (child children)
	  (if (stringp child)
	      (insert (url-insert-entities-in-string child))
	    (setq non-text t)
	    (when pretty
              (insert "\n")
              (indent-line-to indent))
	    (dom-print child pretty xml)))
	;; If we inserted non-text child nodes, or a text node that
	;; ends with a newline, then we indent the end tag.
        (when (and pretty (or (bolp) non-text))
	  (or (bolp) (insert "\n"))
	  (indent-line-to column))
        (insert (format "</%s>" (dom-tag dom)))))))

(provide 'dom)

;;; dom.el ends here
