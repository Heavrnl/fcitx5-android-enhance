;; anthy-unicode-dic.el -- Anthy

;; Copyright (C) 2001 - 2005
;; Author: Yusuke Tabata<yusuke@w5.dion.ne.jp>
;;       : Tomoharu Ugawa

;; This file is part of Anthy

;;; Commentary:
;;
;; tooo experimental
;;
;;
;; Funded by IPA未踏ソフトウェア創造事業 2001 11/10

;;; Code
(defvar anthy-dic-util-command "anthy-dic-tool-unicode")
(defvar anthy-dic-buffer-name " *anthy-dic*")

(defun anthy-add-word-compose-paramlist (param)
  (let ((str ""))
    (while param
      (let* ((cur (car param))
	     (var (car cur))
	     (val (if (stringp (car (cdr cur)))
		      (car (cdr cur))
		    (if (car (cdr cur)) "y" "n"))))
	(setq str (concat str
			  var "\t=\t" val "\n")))
      (setq param (cdr param)))
    str))

(defun anthy-add-word (yomi freq word paramlist)
  (let ((proc))
    (setq proc (start-process "anthy-dic" anthy-dic-buffer-name
			      anthy-dic-util-command "--append"))
    (if proc
	(progn
;;	  (if anthy-xemacs
;;	      (if (coding-system-p (find-coding-system 'euc-japan))
;;		  (set-process-coding-system proc 'euc-japan 'euc-japan))
;;	    (cond ((coding-system-p 'euc-japan)
;;		   (set-process-coding-system proc 'euc-japan 'euc-japan))
;;		  ((coding-system-p '*euc-japan*)
;;		   (set-process-coding-system proc '*euc-japan* '*euc-japan*))))
	  (process-send-string proc
			       (concat yomi " " (int-to-string freq) " " word "\n"))
	  (process-send-string proc
			       (anthy-add-word-compose-paramlist paramlist))
	  (process-send-string proc "\n")
	  (process-send-eof proc)
	  t)
      nil)))

(defun anthy-dic-get-noun-category (word)
  (let
      ((res '(("品詞" "名詞")))
       (na (y-or-n-p (concat "「" word "な」と言いますか? ")))
       (sa (y-or-n-p (concat "「" word "さ」と言いますか? ")))
       (suru (y-or-n-p (concat "「" word "する」と言いますか? ")))
       (ind (y-or-n-p (concat "「" word "」は単独で文節になりますか? ")))
       (kaku (y-or-n-p (concat "「" word "と」と言いますか? "))))
    (setq res (cons `("な接続" ,na) res))
    (setq res (cons `("さ接続" ,sa) res))
    (setq res (cons `("する接続" ,suru) res))
    (setq res (cons `("語幹のみで文節" ,ind) res))
    (setq res (cons `("格助詞接続" ,kaku) res))
    res))

(defun anthy-dic-get-special-noun-category (word)
  (let 
      ((res '())
       (cat (floor (string-to-number
		     (read-from-minibuffer "1:人名 2:地名: ")))))
    (cond ((= cat 1)
	   (setq res '(("品詞" "人名"))))
	  ((= cat 2)
	   (setq res '(("品詞" "地名")))))
    res))

(defun anthy-dic-get-adjective-category (word)
  '(("品詞" "形容詞")))

(defun anthy-dic-get-av-category (word)
  (let
      ((res '(("品詞" "副詞")))
       (to (y-or-n-p (concat "「" word "と」と言いますか?")))
       (taru (y-or-n-p (concat "「" word "たる」と言いますか?")))
       (suru (y-or-n-p (concat "「" word "する」と言いますか?")))
       (ind (y-or-n-p (concat "「" word "」は単独で文節になりますか?"))))
    (setq res (cons `("と接続" ,to) res))
    (setq res (cons `("たる接続" ,taru) res))
    (setq res (cons `("する接続" ,suru) res))
    (setq res (cons `("語幹のみで文節" ,ind) res))
    res))

;; taken from tooltip.el
(defmacro anthy-region-active-p ()
  "Value is non-nil if the region is currently active."
  (if (string-match "^GNU" (emacs-version))
      `(and transient-mark-mode mark-active)
    `(region-active-p)))

(defun anthy-add-word-interactive ()
  ""
  (interactive)
  (let
      ((param '()) (res '())
       (word (if (anthy-region-active-p)
		 (buffer-substring (region-beginning) (region-end))
	       ""))
       yomi cat)
    (and (string= word "")
	 (setq word (read-from-minibuffer "単語(語幹のみ): ")))
    (setq yomi (read-from-minibuffer (concat "読み (" word "): ")))
    (setq cat (floor (string-to-number
		       (read-from-minibuffer
			"カテゴリー 1:一般名詞 2:その他の名詞 3:形容詞 4:副詞: "))))
    (cond ((= cat 1)
	   (setq param (anthy-dic-get-noun-category word)))
	  ((= cat 2)
	   (setq param (anthy-dic-get-special-noun-category word)))
	  ((= cat 3)
	   (setq param (anthy-dic-get-adjective-category word)))
	  ((= cat 4)
	   (setq param (anthy-dic-get-av-category word))))
    (if param
	(setq res (anthy-add-word yomi 500 word param)))
    (if res
	(message (concat word "(" yomi ")を登録しました")))))

(provide 'anthy-unicode-dic)
