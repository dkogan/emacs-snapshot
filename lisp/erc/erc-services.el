;;; erc-services.el --- Identify to NickServ  -*- lexical-binding:t -*-

;; Copyright (C) 2002-2004, 2006-2025 Free Software Foundation, Inc.

;; Maintainer: Amin Bandali <bandali@gnu.org>, F. Jason Park <jp@neverwas.me>
;; URL: https://www.emacswiki.org/emacs/ErcNickserv

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

;; As of ERC 5.6, this library's main module, `services', mainly
;; concerns itself with authenticating to legacy IRC servers.  If your
;; server supports SASL or CERTFP, please use one of those instead.
;; See (info "(erc) client-certificate") and (info "(erc) SASL") for
;; details.  Note that this library also contains the local module
;; `services-regain' as well as standalone utility functions.

;; There are two ways to go about identifying yourself automatically to
;; NickServ with this module.  The more secure way is to listen for identify
;; requests from the user NickServ.  Another way is to identify yourself to
;; NickServ directly after a successful connection and every time you change
;; your nickname.  This method is rather insecure, though, because no checks
;; are made to test if NickServ is the real NickServ for a given network or
;; server.

;; As a default, ERC has the data for the official nickname services
;; on the networks Austnet, BrasNET, Dalnet, freenode, GalaxyNet,
;; GRnet, Libera.Chat, and Slashnet.  You can add more by using
;; M-x customize-variable RET erc-nickserv-alist.

;; Usage:
;;
;; Customize the option `erc-modules' to include `services'.
;;
;; Add your nickname and NickServ password to `erc-nickserv-passwords'.
;; Using the Libera.Chat network as an example:
;;
;; (setq erc-nickserv-passwords
;;       '((Libera.Chat (("nickname" . "password")))))
;;
;; The default automatic identification mode is autodetection of NickServ
;; identify requests.  Set the variable `erc-nickserv-identify-mode' if
;; you'd like to change this behavior.
;;
;; If you'd rather not identify yourself automatically but would like access
;; to the functions contained in this file, just load this file without
;; enabling `erc-services-mode'.
;;

;;; Code:

(require 'erc)
(require 'erc-networks)
(eval-when-compile (require 'cl-lib))

;; Customization:

(defgroup erc-services nil
  "Configuration for IRC services.

On some networks, there exists a special type of automated irc bot,
called Services.  Those usually allow you to register your nickname,
post/read memos to other registered users who are currently offline,
and do various other things.

This group allows you to set variables to somewhat automate
communication with those Services."
  :group 'erc)

(defcustom erc-nickserv-identify-mode 'both
  "The mode which is used when identifying to Nickserv.

Possible settings are:.

`autodetect'  - Identify when the real Nickserv sends an identify request.
`nick-change' - Identify when you log in or change your nickname.
`both'        - Do the former if the network supports it, otherwise do the
                latter.
nil           - Disables automatic Nickserv identification.

You can also use \\[erc-nickserv-identify-mode] to change modes."
  :type '(choice (const autodetect)
		 (const nick-change)
		 (const both)
		 (const nil))
  :set (lambda (sym val)
	 (set sym val)
	 ;; avoid recursive load at startup
	 (when (featurep 'erc-services)
	   (erc-nickserv-identify-mode val))))

;;;###autoload(put 'nickserv 'erc--module 'services)
;;;###autoload(autoload 'erc-services-mode "erc-services" nil t)
(define-erc-module services nickserv
  "This mode automates communication with services."
  ((erc-nickserv-identify-mode erc-nickserv-identify-mode))
  ((remove-hook 'erc-server-NOTICE-functions
		#'erc-nickserv-identify-autodetect)
   (remove-hook 'erc-after-connect
		#'erc-nickserv-identify-on-connect)
   (remove-hook 'erc-nick-changed-functions
		#'erc-nickserv-identify-on-nick-change)
   (remove-hook 'erc-server-NOTICE-functions
		#'erc-nickserv-identification-autodetect)))

;;;###autoload
(defun erc-nickserv-identify-mode (mode)
  "Set up hooks according to which MODE the user has chosen."
  (interactive
   (list (intern (completing-read
		  "Choose Nickserv identify mode (RET to disable): "
		  '(("autodetect") ("nick-change") ("both")) nil t))))
  (add-hook 'erc-server-NOTICE-functions
	    #'erc-nickserv-identification-autodetect)
  (unless erc-networks-mode
    ;; Force-enable networks module, because we need it to set
    ;; erc-network for us.
    (erc-networks-enable))
  (cond ((eq mode 'autodetect)
	 (setq erc-nickserv-identify-mode 'autodetect)
	 (add-hook 'erc-server-NOTICE-functions
		   #'erc-nickserv-identify-autodetect)
	 (remove-hook 'erc-nick-changed-functions
		      #'erc-nickserv-identify-on-nick-change)
	 (remove-hook 'erc-after-connect
		      #'erc-nickserv-identify-on-connect))
	((eq mode 'nick-change)
	 (setq erc-nickserv-identify-mode 'nick-change)
	 (add-hook 'erc-after-connect
		   #'erc-nickserv-identify-on-connect)
	 (add-hook 'erc-nick-changed-functions
		   #'erc-nickserv-identify-on-nick-change)
	 (remove-hook 'erc-server-NOTICE-functions
		      #'erc-nickserv-identify-autodetect))
	((eq mode 'both)
	 (setq erc-nickserv-identify-mode 'both)
	 (add-hook 'erc-server-NOTICE-functions
		   #'erc-nickserv-identify-autodetect)
	 (add-hook 'erc-after-connect
		   #'erc-nickserv-identify-on-connect)
	 (add-hook 'erc-nick-changed-functions
		   #'erc-nickserv-identify-on-nick-change))
	(t
	 (setq erc-nickserv-identify-mode nil)
	 (remove-hook 'erc-server-NOTICE-functions
		      #'erc-nickserv-identify-autodetect)
	 (remove-hook 'erc-after-connect
		      #'erc-nickserv-identify-on-connect)
	 (remove-hook 'erc-nick-changed-functions
		      #'erc-nickserv-identify-on-nick-change)
	 (remove-hook 'erc-server-NOTICE-functions
		      #'erc-nickserv-identification-autodetect))))

(defcustom erc-prompt-for-nickserv-password t
  "Ask for the password when identifying to NickServ."
  :type 'boolean)

(defcustom erc-use-auth-source-for-nickserv-password nil
  "Query auth-source for a password when identifying to NickServ.
Passwords from `erc-nickserv-passwords' take precedence.  See
function `erc-nickserv-get-password'."
  :version "28.1"
  :type 'boolean)

(defcustom erc-auth-source-services-function #'erc-auth-source-search
  "Function to retrieve NickServ password from auth-source.
Called with a subset of keyword parameters known to
`auth-source-search' and relevant to authenticating to nickname
services.  In return, ERC expects a string to send as the
password, or nil, to fall through to the next method, such as
prompting.  See Info node `(erc) auth-source' for details."
  :package-version '(ERC . "5.5")
  :type '(choice (function-item erc-auth-source-search)
                 (const nil)
                 function))

(defcustom erc-nickserv-passwords nil
  "Passwords used when identifying to NickServ automatically.

Example of use:
  (setq erc-nickserv-passwords
        \\='((Libera.Chat ((\"nick-one\" . \"password\")
                        (\"nick-two\" . \"password\")))
          (DALnet ((\"nick\" . \"password\")))))"
  :type '(repeat
	  (list :tag "Network"
		(choice :tag "Network name"
			(const Ars)
			(const Austnet)
			(const Azzurra)
			(const BitlBee)
			(const BRASnet)
			(const DALnet)
			(const freenode)
			(const GalaxyNet)
			(const GRnet)
			(const iip)
                        (const Libera.Chat)
			(const OFTC)
			(const QuakeNet)
			(const Rizon)
			(const SlashNET)
                        (symbol :tag "Network name or session ID"))
		(repeat :tag "Nickname and password"
			(cons :tag "Identity"
			      (string :tag "Nick")
			      (string :tag "Password"
                                      :secret ?*))))))

;; Variables:

(defcustom erc-nickserv-alist
  '((Ars
     nil nil
     "Census"
     "IDENTIFY" nil nil nil)
    (Austnet
     "NickOP!service@austnet.org"
     "/msg\\s-NickOP@austnet.org\\s-identify\\s-<password>"
     "nickop@austnet.org"
     "identify" nil nil nil)
    (Azzurra
     "NickServ!service@azzurra.org"
     "\^B/ns\\s-IDENTIFY\\s-password\^B"
     "NickServ"
     "IDENTIFY" nil nil nil)
    (BitlBee
     nil nil
     "&bitlbee"
     "identify" nil nil nil)
    (BRASnet
     "NickServ!services@brasnet.org"
     "\^B/NickServ\\s-IDENTIFY\\s-\^_senha\^_\^B"
     "NickServ"
     "IDENTIFY" nil "" nil)
    (DALnet
     "NickServ!service@dal.net"
     "/msg\\s-NickServ@services.dal.net\\s-IDENTIFY\\s-<password>"
     "NickServ@services.dal.net"
     "IDENTIFY" nil nil nil)
    (freenode
     "NickServ!NickServ@services."
     ;; freenode also accepts a password at login, see the `erc'
     ;; :password argument.
     "This\\s-nickname\\s-is\\s-registered.\\s-Please\\s-choose"
     "NickServ"
     "IDENTIFY" nil nil
     ;; See also the 901 response code message.
     "You\\s-are\\s-now\\s-identified\\s-for\\s-")
    (GalaxyNet
     "NS!nickserv@galaxynet.org"
     "Please\\s-change\\s-nicks\\s-or\\s-authenticate."
     "NS@services.galaxynet.org"
     "AUTH" t nil nil)
    (GRnet
     "NickServ!service@irc.gr"
     "This\\s-nickname\\s-is\\s-registered\\s-and\\s-protected."
     "NickServ"
     "IDENTIFY" nil nil
     "Password\\s-accepted\\s--\\s-you\\s-are\\s-now\\s-recognized.")
    (iip
     "Trent@anon.iip"
     "type\\s-/squery\\s-Trent\\s-identify\\s-<password>"
     "Trent@anon.iip"
     "IDENTIFY" nil "SQUERY" nil)
    (Libera.Chat
     "NickServ!NickServ@services.libera.chat"
     ;; Libera.Chat also accepts a password at login, see the `erc'
     ;; :password argument.
     "This\\s-nickname\\s-is\\s-registered.\\s-Please\\s-choose"
     "NickServ"
     "IDENTIFY" nil nil
     ;; See also the 901 response code message.
     "You\\s-are\\s-now\\s-identified\\s-for\\s-")
    (OFTC
     "NickServ!services@services.oftc.net"
     ;; OFTC's NickServ doesn't ask you to identify anymore.
     nil
     "NickServ"
     "IDENTIFY" nil nil
     "You\\s-are\\s-successfully\\s-identified\\s-as\\s-\^B")
    (Rizon
     "NickServ!service@rizon.net"
     "This\\s-nickname\\s-is\\s-registered\\s-and\\s-protected."
     "NickServ"
     "IDENTIFY" nil nil
     "Password\\s-accepted\\s--\\s-you\\s-are\\s-now\\s-recognized.")
    (QuakeNet
     nil nil
     "Q@CServe.quakenet.org"
     "auth" t nil nil)
    (SlashNET
     "NickServ!services@services.slashnet.org"
     "/msg\\s-NickServ\\s-IDENTIFY\\s-\^_password"
     "NickServ@services.slashnet.org"
     "IDENTIFY" nil nil nil))
  "Alist of NickServer details, sorted by network.
Every element in the list has the form
  (NETWORK SENDER INSTRUCT-RX NICK SUBCMD YOUR-NICK-P ANSWER SUCCESS-RX)

NETWORK is a network identifier, a symbol, as used in `erc-networks-alist'.
SENDER is the exact nick!user@host \"source\" for \"NOTICE\" messages
indicating success or requesting that the user identify.
INSTRUCT-RX is a regular expression matching a \"NOTICE\" from the
  services bot instructing the user to identify.  It must be non-null
  when the option `erc-nickserv-identify-mode' is set to `autodetect'.
  When it's `both', and this field is non-null, ERC will forgo
  identifying on nick changes and after connecting.
NICK is the nickname of the services bot to use when issuing commands.
SUBCMD is the bot command for identifying, typically \"IDENTIFY\".
YOUR-NICK-P indicates whether to send the user's current nickname before
  their password when identifying.
ANSWER is the command to use for the answer.  The default is \"PRIVMSG\".
SUCCESS-RX is a regular expression matching the message NickServ sends
  when you've successfully identified.
The last two elements are optional, as are others, where implied."
   :type '(repeat
	   (list :tag "Nickserv data"
		 (symbol :tag "Network name")
		 (choice (string :tag "Nickserv's nick!user@host")
			 (const :tag "No message sent by Nickserv" nil))
		 (choice (regexp :tag "Identify request sent by Nickserv")
			 (const :tag "No message sent by Nickserv" nil))
		 (string :tag "Identify to")
		 (string :tag "Identify keyword")
		 (boolean :tag "Use current nick in identify message?")
		 (choice :tag "Command to use (optional)"
		  (string :tag "Command")
		  (const :tag "No special command necessary" nil))
		 (choice :tag "Detect Success"
			 (regexp :tag "Pattern to match")
			 (const :tag "Do not try to detect success" nil)))))


(define-inline erc-nickserv-alist-sender (network &optional entry)
  (inline-letevals (network entry)
    (inline-quote (nth 1 (or ,entry (assoc ,network erc-nickserv-alist))))))

(define-inline erc-nickserv-alist-regexp (network &optional entry)
  (inline-letevals (network entry)
    (inline-quote (nth 2 (or ,entry (assoc ,network erc-nickserv-alist))))))

(define-inline erc-nickserv-alist-nickserv (network &optional entry)
  (inline-letevals (network entry)
    (inline-quote (nth 3 (or ,entry (assoc ,network erc-nickserv-alist))))))

(define-inline erc-nickserv-alist-ident-keyword (network &optional entry)
  (inline-letevals (network entry)
    (inline-quote (nth 4 (or ,entry (assoc ,network erc-nickserv-alist))))))

(define-inline erc-nickserv-alist-use-nick-p (network &optional entry)
  (inline-letevals (network entry)
    (inline-quote (nth 5 (or ,entry (assoc ,network erc-nickserv-alist))))))

(define-inline erc-nickserv-alist-ident-command (network &optional entry)
  (inline-letevals (network entry)
    (inline-quote (nth 6 (or ,entry (assoc ,network erc-nickserv-alist))))))

(define-inline erc-nickserv-alist-identified-regexp (network &optional entry)
  (inline-letevals (network entry)
    (inline-quote (nth 7 (or ,entry (assoc ,network erc-nickserv-alist))))))

;; Functions:

(defcustom erc-nickserv-identified-hook nil
  "Run this hook when NickServ acknowledged successful identification.
Hooks are called with arguments (NETWORK NICK)."
  :type 'hook)

(defun erc-nickserv-identification-autodetect (_proc parsed)
  "Check for NickServ's successful identification notice.
Make sure it is the real NickServ for this network and that it has
specifically confirmed a successful identification attempt.
If this is the case, run `erc-nickserv-identified-hook'."
  (let* ((network (erc-network))
	 (sender (erc-nickserv-alist-sender network))
	 (success-regex (erc-nickserv-alist-identified-regexp network))
	 (sspec (erc-response.sender parsed))
	 (nick (car (erc-response.command-args parsed)))
	 (msg (erc-response.contents parsed)))
    ;; continue only if we're sure it's the real nickserv for this network
    ;; and it's told us we've successfully identified
    (when (and sender (equal sspec sender)
	       success-regex
	       (string-match success-regex msg))
      (erc-log "NickServ IDENTIFY success notification detected")
      (run-hook-with-args 'erc-nickserv-identified-hook network nick)
      nil)))

(defun erc-nickserv-identify-autodetect (_proc parsed)
  "Identify to NickServ when an identify request is received.
If both the sender and the body of the PARSED message match an entry in
`erc-nickserv-alist', ask `erc-nickserv-identify' to authenticate."
  (unless (and (null erc-nickserv-passwords)
               (null erc-prompt-for-nickserv-password)
               (null erc-use-auth-source-for-nickserv-password))
    (let* ((network (erc-network))
	   (sender (erc-nickserv-alist-sender network))
	   (identify-regex (erc-nickserv-alist-regexp network))
	   (sspec (erc-response.sender parsed))
	   (nick (car (erc-response.command-args parsed)))
	   (msg (erc-response.contents parsed)))
      ;; continue only if we're sure it's the real nickserv for this network
      ;; and it's asked us to identify
      (when (and sender (equal sspec sender)
		 identify-regex
		 (string-match identify-regex msg))
	(erc-log "NickServ IDENTIFY request detected")
        (erc-nickserv-identify nil nick)
	nil))))

(defun erc-nickserv-identify-on-connect (_server nick)
  "Identify to Nickserv after the connection to the server is established."
  (unless (and (eq erc-nickserv-identify-mode 'both)
               (erc-nickserv-alist-regexp (erc-network)))
    (erc-nickserv-identify nil nick)))

(defun erc-nickserv-identify-on-nick-change (nick _old-nick)
  "Identify to Nickserv whenever your nick changes."
  (unless (and (eq erc-nickserv-identify-mode 'both)
               (erc-nickserv-alist-regexp (erc-network)))
    (erc-nickserv-identify nil nick)))

(defun erc-nickserv-get-password (nick)
  "Return the password for NICK from configured sources.
First, a password for NICK is looked up in
`erc-nickserv-passwords'.  Then, it is looked up in auth-source
if `erc-use-auth-source-for-nickserv-password' is not nil.
Finally, interactively prompt the user, if
`erc-prompt-for-nickserv-password' is true.

As soon as some source returns a password, the sequence of
lookups stops and this function returns it (or returns nil if it
is empty).  Otherwise, no corresponding password was found, and
it returns nil."
  (when-let*
      ((nid (erc-networks--id-symbol erc-networks--id))
       (ret (or (when erc-nickserv-passwords
                  (assoc-default nick
                                 (cadr (assq nid erc-nickserv-passwords))))
                (when (and erc-use-auth-source-for-nickserv-password
                           erc-auth-source-services-function)
                  (funcall erc-auth-source-services-function :user nick))
                (when erc-prompt-for-nickserv-password
                  (read-passwd
                   (format "NickServ password for %s on %s (RET to cancel): "
                           nick nid)))))
       ((not (string-empty-p (erc--unfun ret)))))
    ret))

(defvar erc-auto-discard-away)

(defun erc-nickserv-send-identify (nick password)
  "Send an \"identify <PASSWORD>\" message to NickServ.
Returns t if the message could be sent, nil otherwise."
  (let* ((erc-auto-discard-away nil)
         (network (erc-network))
         (nickserv-info (assoc network erc-nickserv-alist))
         (nickserv (or (erc-nickserv-alist-nickserv nil nickserv-info)
                       "NickServ"))
         (identify-word (or (erc-nickserv-alist-ident-keyword
                             nil nickserv-info)
                            "IDENTIFY"))
         (nick (if (erc-nickserv-alist-use-nick-p nil nickserv-info)
                   (concat nick " ")
                 ""))
         (msgtype (or (erc-nickserv-alist-ident-command nil nickserv-info)
                      "PRIVMSG")))
    (erc-message msgtype
                 (concat nickserv " " identify-word " " nick
                         (erc--unfun password)))))

(defun erc-nickserv-call-identify-function (nickname)
  "Call `erc-nickserv-identify' with NICKNAME."
  (declare (obsolete erc-nickserv-identify "28.1"))
  (erc-nickserv-identify nil nickname))

;;;###autoload
(defun erc-nickserv-identify (&optional password nick)
  "Identify to NickServ immediately.
For authenticating, use NICK or `erc-current-nick' and PASSWORD or one
obtained via `erc-nickserv-get-password'.  If a password can't be found,
tell `erc-error'.  Return t if a message was sent, nil otherwise.
Interactively, prompt for NICK, interpreting an empty string as the
current nick."
  (interactive
   (list
    nil
    (read-from-minibuffer "Nickname: " nil nil nil
                          'erc-nick-history-list (erc-current-nick))))
  (unless (and nick (not (string= nick "")))
    (setq nick (erc-current-nick)))
  (unless password
    (setq password (erc-nickserv-get-password nick)))
  (if password
      (progn (erc-nickserv-send-identify nick password) t)
    (erc-error "Cannot find a password for nickname %s"
               nick)
    nil))


;;;; Regaining nicknames

(defcustom erc-services-regain-alist nil
  "Alist mapping networks to nickname-regaining functions.
This option depends on the `services-regain' module being loaded.
Keys can also be symbols for user-provided \"context IDs\" (see
Info node `Network Identifier').  Functions run once, when first
establishing a logical IRC connection.  Although ERC currently
calls them with one argument, the desired but rejected nickname,
robust user implementations should leave room for later additions
by defining an &rest _ parameter, as well.

The simplest value is `erc-services-retry-nick-on-connect', which
attempts to kill off stale connections without engaging services
at all.  Others, like `erc-services-issue-regain', and
`erc-services-issue-ghost-and-retry-nick', only speak a
particular flavor of NickServ.  See their respective doc strings
for details and use cases."
  :package-version '(ERC . "5.6")
  :group 'erc-hooks
  :type '(alist :key-type (symbol :tag "Network")
                :value-type
                (choice :tag "Strategy function"
                        (function-item erc-services-retry-nick-on-connect)
                        (function-item erc-services-issue-regain)
                        (function-item erc-services-issue-ghost-and-retry-nick)
                        function)))

(defvar erc-services-regain-timeout-seconds 5
  "Seconds after which to run callbacks if necessary.")

(defun erc-services-retry-nick-on-connect (want)
  "Try at most once to grab nickname WANT after reconnecting.
Expect to be used when automatically reconnecting to servers
that are slow to abandon the previous connection.

Note that this strategy may only work under certain conditions,
such as when a user's account name matches their nick."
  (erc-cmd-NICK want))

(defun erc-services-issue-regain (want)
  "Ask NickServ to regain nickname WANT.
Assume WANT belongs to the user and that the services suite
offers a \"REGAIN\" sub-command."
  (erc-cmd-MSG (concat "NickServ REGAIN " want)))

(defun erc-services-issue-ghost-and-retry-nick (want)
  "Ask NickServ to \"GHOST\" nickname WANT.
After which, attempt to grab WANT before the contending party
reconnects.  Assume the ERC user owns WANT and that the server's
services suite lacks a \"REGAIN\" command.

Note that this function will only work for a specific services
implementation and is meant primarily as an example for adapting
as needed."
  ;; While heuristics based on error text may seem brittle, consider
  ;; the fact that \"is not online\" has been present in Atheme's
  ;; \"GHOST\" responses since at least 2005.
  (letrec ((attempts 3)
           (on-notice
            (lambda (_proc parsed)
              (when-let* ((nick (erc-extract-nick
                                 (erc-response.sender parsed)))
                          ((erc-nick-equal-p nick "nickserv"))
                          (contents (erc-response.contents parsed))
                          (case-fold-search t)
                          ((string-match (rx (or "ghost" "is not online"))
                                         contents)))
                (setq attempts 1)
                (erc-server-send (concat "NICK " want) 'force))
              (when (zerop (cl-decf attempts))
                (remove-hook 'erc-server-NOTICE-functions on-notice t))
              nil)))
    (add-hook 'erc-server-NOTICE-functions on-notice nil t)
    (erc-message "PRIVMSG" (concat "NickServ GHOST " want))))

;;;###autoload(put 'services-regain 'erc--feature 'erc-services)
(define-erc-module services-regain nil
  "Reacquire a nickname from your past self or some interloper.
This module only concerns itself with initial nick rejections
that occur during connection registration in response to an
opening \"NICK\" command.  More specifically, the following
conditions must be met for ERC to activate this mechanism and
consider its main option, `erc-services-regain-alist':

  - the server must reject the opening \"NICK\" request
  - ERC must request a temporary nickname
  - the user must successfully authenticate

In practical terms, this means that this module, which is still
somewhat experimental, is likely only useful in conjunction with
SASL authentication or CertFP rather than the traditional approach
provided by the `services' module it shares a library with (see Info
node `(erc) SASL' for more).

This local module's minor mode is only active in server buffers."
  ((when erc--target (erc-services-regain-mode -1))) nil localp)

(cl-defmethod erc--nickname-in-use-make-request
  ((want string) temp &context (erc-server-connected null)
   (erc-services-regain-mode (eql t))
   (erc-services-regain-alist cons))
  "Schedule possible regain attempt upon establishing connection.
Expect WANT to be the desired nickname and TEMP to be the current
one."
  (letrec
      ((after-connect
        (lambda (_ nick)
          (remove-hook 'erc-after-connect after-connect t)
          (when-let*
              (((equal temp nick))
               (conn (or (erc-networks--id-given erc-networks--id)
                         (erc-network)))
               (found (alist-get conn erc-services-regain-alist)))
            (funcall found want))))
       (on-900
        (lambda (_ parsed)
          (cancel-timer timer)
          (remove-hook 'erc-server-900-functions on-900 t)
          (unless (equal want (erc-current-nick))
            (if erc-server-connected
                (funcall after-connect nil temp)
              (when (or (eq parsed 'forcep)
                        (equal (car (erc-response.command-args parsed)) temp))
                (add-hook 'erc-after-connect after-connect nil t))))
          nil))
       (timer (run-at-time erc-services-regain-timeout-seconds
                           nil (lambda (buffer)
                                 (when (buffer-live-p buffer)
                                   (with-current-buffer buffer
                                     (funcall on-900 nil 'forcep))))
                           (current-buffer))))
    (add-hook 'erc-server-900-functions on-900 nil t))
  (cl-call-next-method))

(provide 'erc-services)


;;; erc-services.el ends here
;;
;; Local Variables:
;; generated-autoload-file: "erc-loaddefs.el"
;; End:
